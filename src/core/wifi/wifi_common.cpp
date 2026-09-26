#include "core/wifi/wifi_common.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/powerSave.h"
#include "core/radio_mem.h"
#include "core/ram_profile.h"
#include "core/settings.h"
#include "core/utils.h"
#include "core/wifi/wifi_mac.h"
#include "esp_wifi.h"
#include "modules/ble/ble_common.h"
#include <esp_event.h>
#include <esp_netif.h>
#include <globals.h>

static TaskHandle_t timezoneTaskHandle = NULL;
static bool wifiTransitioning = false;

// Last STA disconnect reason captured for connect-failure diagnostics (0 = none yet).
static volatile uint8_t s_staDiscReason = 0;
static bool s_staDiscHookInstalled = false;

static void sta_disc_reason_handler(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        s_staDiscReason = info.wifi_sta_disconnected.reason;
    }
}

esp_err_t wifiRawTx(wifi_interface_t ifx, const void *frame, int len, uint8_t retries) {
    // Raw injection must go out over a live internal-radio interface. Attacks
    // normally bring up APSTA + softAP first, but if one started while the STA
    // interface was the only one up (e.g. already connected to a network), an
    // AP-interface frame would hit a dead interface and silently go nowhere.
    // Redirect to whichever interface is actually up in that case.
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        if (ifx == WIFI_IF_AP && !(mode & WIFI_MODE_AP) && (mode & WIFI_MODE_STA)) ifx = WIFI_IF_STA;
        else if (ifx == WIFI_IF_STA && !(mode & WIFI_MODE_STA) && (mode & WIFI_MODE_AP)) ifx = WIFI_IF_AP;
    }
    esp_err_t err = esp_wifi_80211_tx(ifx, frame, len, false);
    for (uint8_t i = 0; err == ESP_ERR_NO_MEM && i < retries; i++) {
        vTaskDelay(1); // let the driver drain TX buffers and retry
        err = esp_wifi_80211_tx(ifx, frame, len, false);
    }
    return err;
}

bool wifiSetPermissiveCountry() {
    wifi_country_t cc;
    memset(&cc, 0, sizeof(cc));
    memcpy(cc.cc, "JP", 3); // JP allows ch 1-14; MANUAL stops 802.11d shrinking it
    cc.schan = 1;
    cc.nchan = 14;
#ifdef CONFIG_ESP_PHY_MAX_TX_POWER
    cc.max_tx_power = CONFIG_ESP_PHY_MAX_TX_POWER;
#endif
    cc.policy = WIFI_COUNTRY_POLICY_MANUAL;

    esp_err_t err = esp_wifi_set_max_tx_power(80); // 20 dBm, as boot intends
    if (err != ESP_OK) Serial.printf("[WIFI] set_max_tx_power failed: %s\n", esp_err_to_name(err));

    err = esp_wifi_set_country(&cc);
    if (err != ESP_OK) {
        Serial.printf("[WIFI] set_country(JP/1-14) failed: %s\n", esp_err_to_name(err));
        return false;
    }
    wifi_country_t cur;
    memset(&cur, 0, sizeof(cur));
    if (esp_wifi_get_country(&cur) == ESP_OK) {
        Serial.printf("[WIFI] country now %.2s schan=%d nchan=%d\n", cur.cc, cur.schan, cur.nchan);
        return cur.nchan >= 13;
    }
    return true;
}

void ensureWifiPlatform() {
    static bool netifInitialized = false;
    static bool eventLoopCreated = false;
    static portMUX_TYPE platformMux = portMUX_INITIALIZER_UNLOCKED;

    portENTER_CRITICAL(&platformMux);
    bool needNetif = !netifInitialized;
    bool needLoop = !eventLoopCreated;
    portEXIT_CRITICAL(&platformMux);

    if (needNetif) {
        ESP_ERROR_CHECK(esp_netif_init());
        portENTER_CRITICAL(&platformMux);
        netifInitialized = true;
        portEXIT_CRITICAL(&platformMux);
    }

    if (needLoop) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_ERR_INVALID_STATE) { ESP_ERROR_CHECK(err); }
        portENTER_CRITICAL(&platformMux);
        eventLoopCreated = true;
        portEXIT_CRITICAL(&platformMux);
    }
}

bool _wifiConnect(const String &ssid, int encryption) {
    String password = bruceConfig.getWifiPassword(ssid);
    if (password == "" && encryption > 0) { password = keyboard(password, 63, "Network Password:", true); }
    if (password == "\x1B") return false;
    bool connected = _connectToWifiNetwork(ssid, password);
    bool retry = false;

    while (!connected) {
        wakeUpScreen();

        options = {
            {"Retry",  [&]() { retry = true; } },
            {"Cancel", [&]() { retry = false; }},
        };
        loopOptions(options);

        if (!retry) {
            wifiDisconnect();
            return false;
        }

        password = keyboard(password, 63, "Network Password:", true);
        if (password == "\x1B") {
            wifiDisconnect();
            return false;
        }
        connected = _connectToWifiNetwork(ssid, password);
    }

    if (connected) {
        wifiConnected = true;
        wifiIP = WiFi.localIP().toString();
        bruceConfig.addWifiCredential(ssid, password);

        // Start timezone update in background if not already running
        if (timezoneTaskHandle == NULL) {
            xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
        }
    }

    delay(200);
    return connected;
}

bool _connectToWifiNetwork(const String &ssid, const String &pwd) {
    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        if (BLEConnected) {
            displayWarning("Board with no PSRAM, closing BLE Stack");
            vTaskDelay(700 / portTICK_PERIOD_MS);
        }
        stopBLEStack();
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }

    RAM_LOG("wifi pre-mode"); // Wi-Fi is already up from the menu scan by this point
    drawMainBorderWithTitle("WiFi Connect");
    padprintln("");
    padprint("Connecting to: " + ssid + ".");
    WiFi.mode(WIFI_MODE_STA);
    RAM_LOG("wifi post-mode");
    vTaskDelay(10 / portTICK_PERIOD_MS);
    // Capture the 802.11 disconnect reason for this attempt (installed once).
    if (!s_staDiscHookInstalled) {
        WiFi.onEvent(sta_disc_reason_handler);
        s_staDiscHookInstalled = true;
    }
    s_staDiscReason = 0;
    WiFi.begin(ssid, pwd);

    int i = 1;
    while (!WiFi.isConnected()) {
        if (tft.getCursorX() >= tftWidth - 12) {
            padprintln("");
            padprint("");
        }
#ifdef HAS_SCREEN
        tft.print(".");
#else
        Serial.print(".");
#endif

        if (i > 30) {
            // Tell the user WHY it failed: stuck WiFi.status() first, then the
            // captured 802.11 disconnect reason (more specific) overrides it.
            // (Keep strings short: displayError renders a single stripe.)
            String why = "Wifi Offline"; // timeout: AP/DHCP not answering
            uint8_t reason = s_staDiscReason;
            switch (WiFi.status()) {
                case WL_CONNECT_FAILED: why = "Wrong password?"; break;
                case WL_NO_SSID_AVAIL:  why = "SSID not found"; break;
                case WL_CONNECTION_LOST:
                case WL_DISCONNECTED:   why = "Signal lost"; break;
                default:                break;
            }
            if (reason != 0) {
                switch (reason) {
                    case 15:  // 4-way handshake timeout (often wrong password)
                    case 16:  // group-key handshake timeout
                    case 23:  // 802.1X auth failed
                    case 24:  // cipher suite rejected
                    case 202: // auth fail
                    case 204: // handshake timeout
                        why = "Auth fail r" + String((int)reason);
                        break;
                    case 201: why = "AP gone r201"; break;  // no AP found
                    case 200: why = "Weak sig r200"; break; // beacon timeout
                    case 203: // assoc fail
                    case 205: // connection fail
                        why = "AP reject r" + String((int)reason);
                        break;
                    case 2: // auth expired
                    case 3: // auth leave
                    case 8: // assoc leave
                        why = "AP kicked r" + String((int)reason);
                        break;
                    case 5:      why = "AP full r5"; break; // assoc too many
                    default:     why = "Wifi err r" + String((int)reason); break;
                }
            }
            Serial.printf(
                "[wifi] connect '%s' failed, status=%d reason=%u\n",
                ssid.c_str(),
                (int)WiFi.status(),
                (unsigned)reason
            );
            displayError(why);
            vTaskDelay(500 / portTICK_RATE_MS);
            break;
        }

        vTaskDelay(500 / portTICK_RATE_MS);
        i++;
    }

    return WiFi.isConnected();
}

bool _setupAP() {
    IPAddress AP_GATEWAY(172, 0, 0, 1);
    WiFi.softAPConfig(AP_GATEWAY, AP_GATEWAY, IPAddress(255, 255, 255, 0));
    WiFi.softAP(bruceConfig.wifiAp.ssid, bruceConfig.wifiAp.pwd, 6, 0, 4, false);
    wifiIP = WiFi.softAPIP().toString(); // update global var
    Serial.println("IP: " + wifiIP);
    wifiConnected = true;
    return true;
}

void wifiDisconnect() {
    wifiTransitioning = true;

    wifi_mode_t mode = WiFi.getMode();
    if (mode & WIFI_MODE_AP) {
        WiFi.softAPdisconnect();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (mode & WIFI_MODE_STA) {
        WiFi.disconnect(false, true);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (mode != WIFI_MODE_NULL) {
        WiFi.mode(WIFI_OFF);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    wifiConnected = false;
    wifiTransitioning = false;
}

bool wifiConnectMenu(wifi_mode_t mode) {
    if (WiFi.isConnected()) return false; // safeguard

    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        stopBLEStack();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        displayTextLine("WiFi busy, please wait...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return false;
    }

    switch (mode) {
        case WIFI_AP: // access point
            WiFi.mode(WIFI_AP);
            return _setupAP();
            break;

        case WIFI_STA: { // station mode
            int nets;
            if (!radioHasMemForWifi()) {
                displayError("Low RAM: free BLE/SD first", true);
                return false;
            }
            WiFi.mode(WIFI_MODE_STA);

            // wifiMACMenu();
            applyConfiguredMAC();

            bool refresh_scan = false;
            do {
                displayTextLine("Scanning..");
                nets = WiFi.scanNetworks();

                String selSsid = "";
                int selEnc = 0;
                bool selHidden = false;

                options = {};
                for (int i = 0; i < nets; i++) {
                    if (options.size() < 250) {
                        String ssid = WiFi.SSID(i);
                        int encryptionType = WiFi.encryptionType(i);
                        int32_t rssi = WiFi.RSSI(i);
                        int32_t ch = WiFi.channel(i);
                        // Check if the network is secured
                        String encryptionPrefix = (encryptionType == WIFI_AUTH_OPEN) ? "" : "#";
                        String encryptionTypeStr;
                        switch (encryptionType) {
                            case WIFI_AUTH_OPEN: encryptionTypeStr = "Open"; break;
                            case WIFI_AUTH_WEP: encryptionTypeStr = "WEP"; break;
                            case WIFI_AUTH_WPA_PSK: encryptionTypeStr = "WPA/PSK"; break;
                            case WIFI_AUTH_WPA2_PSK: encryptionTypeStr = "WPA2/PSK"; break;
                            case WIFI_AUTH_WPA_WPA2_PSK: encryptionTypeStr = "WPA/WPA2/PSK"; break;
                            case WIFI_AUTH_WPA2_ENTERPRISE: encryptionTypeStr = "WPA2/Enterprise"; break;
                            case WIFI_AUTH_WPA3_PSK: encryptionTypeStr = "WPA3/PSK"; break;
                            case WIFI_AUTH_WPA2_WPA3_PSK: encryptionTypeStr = "WPA2/WPA3/PSK"; break;
                            default: encryptionTypeStr = "Unknown"; break;
                        }

                        String optionText = encryptionPrefix + ssid + "(" + String(rssi) + "|" +
                                            encryptionTypeStr + "|ch." + String(ch) + ")";

                        options.push_back({optionText.c_str(), [&selSsid, &selEnc, ssid, encryptionType]() {
                                               selSsid = ssid;
                                               selEnc = encryptionType;
                                           }});
                    }
                }
                WiFi.scanDelete();
                options.push_back({"Hidden SSID", [&selHidden]() { selHidden = true; }});
                addOptionToMainMenu();

                loopOptions(options);
                options.clear();

                if (returnToMenu) {
                    refresh_scan = false;
                } else if (selHidden) {
                    String __ssid = keyboard("", 32, "Your SSID");
                    if (__ssid != "\x1B") _wifiConnect(__ssid.c_str(), 8);
                    refresh_scan = false;
                } else if (selSsid != "") {
                    _wifiConnect(selSsid, selEnc);
                    refresh_scan = false;
                } else if (check(EscPress)) {
                    refresh_scan = true;
                } else {
                    refresh_scan = false;
                }
            } while (refresh_scan);
        } break;

        case WIFI_AP_STA: // repeater mode
                          // _setupRepeater();
            break;

        default: // error handling
            Serial.println("Unknown wifi mode: " + String(mode));
            break;
    }

    if (returnToMenu) {
        wifiDisconnect(); // Forced turning off the wifi module if exiting back to the menu
        return false;
    }
    return wifiConnected;
}

void wifiConnectTask(void *pvParameters) {
    if (WiFi.isConnected()) return;

    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        stopBLEStack();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        vTaskDelete(NULL);
        return;
    }

    // No-PSRAM guard: don't bring Wi-Fi up if the contiguous DMA block is too
    // small (e.g. BLE already active) — the scan would half-init the driver and
    // crash on teardown. Silent bail: this is a background auto-connect task.
    if (!radioHasMemForWifi()) {
        vTaskDelete(NULL);
        return;
    }

    WiFi.mode(WIFI_MODE_STA);
    int nets = WiFi.scanNetworks();
    String ssid;
    String pwd;

    for (int i = 0; i < nets; i++) {
        ssid = WiFi.SSID(i);
        pwd = bruceConfig.getWifiPassword(ssid);
        if (pwd == "") continue;

        WiFi.begin(ssid, pwd);
        for (int i = 0; i < 50; i++) {
            if (WiFi.isConnected()) {
                wifiConnected = true;
                wifiIP = WiFi.localIP().toString();

                // Start timezone update in background if not already running
                if (timezoneTaskHandle == NULL) {
                    xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
                }
                drawStatusBar();
                break;
            }
            vTaskDelay(100 / portTICK_RATE_MS);
        }
    }
    WiFi.scanDelete();

    vTaskDelete(NULL);
    return;
}

String checkMAC() { return String(WiFi.macAddress()); }

bool wifiConnecttoKnownNet(void) {
    if (WiFi.isConnected()) return true; // safeguard

    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        stopBLEStack();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        displayTextLine("WiFi busy, please wait...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return false;
    }

    // No-PSRAM guard: refuse before the scan brings Wi-Fi up in low memory.
    if (!radioHasMemForWifi()) {
        displayError("Low RAM: free BLE/SD first", true);
        return false;
    }

    bool result = false;
    int nets;
    // WiFi.mode(WIFI_MODE_STA);
    displayTextLine("Scanning Networks..");
    WiFi.disconnect(true, true);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    nets = WiFi.scanNetworks();
    for (int i = 0; i < nets; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        String ssid = WiFi.SSID(i);
        String password = bruceConfig.getWifiPassword(ssid);
        if (password != "") {
            Serial.println("Connecting to: " + ssid);
            result = _connectToWifiNetwork(ssid, password);
        }
        // Maybe it finds a known network and can't connect, then try the next
        // until it gets connected (or not)
        if (result) {
            Serial.println("Connected to: " + ssid);
            break;
        }
    }
    if (WiFi.isConnected()) {
        wifiConnected = true;
        wifiIP = WiFi.localIP().toString();

        // Start timezone update in background if not already running
        if (timezoneTaskHandle == NULL) {
            xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
        }
    }
    return result;
}

void updateTimezoneTask(void *pvParameters) {
    // Wait a bit for connection to stabilize before updating timezone
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // Only update timezone if WiFi is still connected
    if (WiFi.isConnected() && wifiConnected) { updateClockTimezone(); }

    // Clear the task handle before deleting
    timezoneTaskHandle = NULL;
    vTaskDelete(NULL);
}
