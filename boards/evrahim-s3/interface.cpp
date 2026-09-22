// ============================================================================
//  Evrahim S3 Custom Board -- interface.cpp
//  ESP32-S3 + 2.8" ST7789 (240x320) + XPT2046 Touch + CC1101 + NRF24L01+
//
//  Touch: XPT2046 on FSPI (SPI2) -- separate bus from TFT/SD/CC1101/NRF24
//  Navigation: Touch-only, no physical buttons
// ============================================================================

#include "CYD28_TouchscreenR.h"
#include "core/bus_HAL.h"
#include "core/powerSave.h"
#include "core/utils.h"
#include <Arduino.h>
#include <interface.h>

// XPT2046 resistive touch via the CYD28_TouchR library.
// Display is 320x240 in landscape (ROTATION 1).
CYD28_TouchR touch(320, 240);

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // Deselect XPT2046 so it doesn't interfere with FSPI bus at boot
    pinMode(XPT2046_SPI_CONFIG_CS_GPIO_NUM, OUTPUT);
    digitalWrite(XPT2046_SPI_CONFIG_CS_GPIO_NUM, HIGH);

    // Deselect TFT, SD, CC1101 and NRF24 so they don't interfere with HSPI bus
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(CC1101_SS_PIN, OUTPUT);
    digitalWrite(CC1101_SS_PIN, HIGH);
    pinMode(NRF24_SS_PIN, OUTPUT);
    digitalWrite(NRF24_SS_PIN, HIGH);

    bruceConfig.colorInverted = 0;
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // Initialize XPT2046 touch on its own FSPI bus (separate from TFT HSPI).
    // If no hardware controller is left for these pins, fall back to the
    // bit-banged path: the no-arg begin() configures the MOSI/MISO/CLK
    // pin modes that begin(SPIClass*) skips, so it must be used instead of
    // passing a nullptr bus handle.
    SPIClass *touchBus = acquireSPIBus(
        (gpio_num_t)XPT2046_SPI_BUS_SCLK_IO_NUM,
        (gpio_num_t)XPT2046_SPI_BUS_MISO_IO_NUM,
        (gpio_num_t)XPT2046_SPI_BUS_MOSI_IO_NUM
    );
    if (touchBus != nullptr) touch.begin(touchBus);
    else {
        Serial.println("XPT2046: no HW SPI bus for touch pins, using bit-banged SPI");
        touch.begin();
    }

    // Backlight PWM -- must be initialized after tft.init()
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 255);

    // Force sync color inversion: _setup_gpio() runs before bruceConf.json is
    // loaded from storage, so a stale saved value would otherwise override the
    // default this panel needs (same approach as the CYD boards).
    bruceConfig.colorInverted = 0;
    tft.invertDisplay(0);

    // Set default RF/IR pin config
    bruceConfigPins.gps_bus.rx = (gpio_num_t)GPS_SERIAL_RX;
    bruceConfigPins.gps_bus.tx = (gpio_num_t)GPS_SERIAL_TX;
    bruceConfigPins.gpsBaudrate = 9600;

    bool pinsChanged = false;
    if (bruceConfigPins.rfTx != CC1101_GDO0_PIN) {
        bruceConfigPins.rfTx = CC1101_GDO0_PIN;
        pinsChanged = true;
    }
    if (bruceConfigPins.rfRx != CC1101_GDO2_PIN) {
        bruceConfigPins.rfRx = CC1101_GDO2_PIN;
        pinsChanged = true;
    }
    if (pinsChanged) bruceConfigPins.saveFile();
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    return 100;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval > 100) brightval = 100;
    int dutyCycle;
    if (brightval == 100) dutyCycle = 255;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

    ledcWrite(TFT_BL, dutyCycle);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress
** and EscPress via XPT2046 touch input.
**********************************************************************/
void InputHandler(void) {
    static long d_tmp = 0;
    if (millis() - d_tmp > 200 || LongPress) {
        if (touch.touched()) {
            auto t = touch.getPointScaled();

            // Clamp raw scaled points before rotation transform to prevent negative edge overflow
            int px = constrain((int)t.x, 0, 320);
            int py = constrain((int)t.y, 0, 240);

            // Rotation adjustments for touch coordinates
            if (bruceConfigPins.rotation == 1) {
                // Default Landscape (320x240) - 1:1 mapping from CYD28_TouchR
            } else if (bruceConfigPins.rotation == 3) {
                // Inverted Landscape (320x240): 180-degree counterpart of
                // rotation 1, so both axes must be flipped (matches CYD).
                px = tftWidth - px;
                py = (tftHeight + 20) - py;
            } else if (bruceConfigPins.rotation == 0) {
                // Portrait (240x320)
                int tmp = px;
                px = tftWidth - py;
                py = tmp;
            } else if (bruceConfigPins.rotation == 2) {
                // Inverted Portrait (240x320)
                int tmp = px;
                px = py;
                py = (tftHeight + 20) - tmp;
            }

            // Final clamp to display boundary
            px = constrain(px, 0, tftWidth - 1);
            py = constrain(py, 0, tftHeight + 19);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else goto END;

            // Touch point global variable
            touchPoint.x = px;
            touchPoint.y = py;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        END:
            d_tmp = millis();
        } else {
            // Explicitly clear pressed state when no touch detected
            touchPoint.pressed = false;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turn off the device
**********************************************************************/
void checkReboot() {}
