# Evrahim S3 — Custom Bruce Board

ESP32-S3 (N16R8: 16&nbsp;MB Flash / 8&nbsp;MB PSRAM) + 2.8" ST7789 TFT + XPT2046
resistive touch + CC1101 sub-GHz + NRF24L01+ 2.4&nbsp;GHz. Touch-only UI
(no physical buttons).

PlatformIO environment: `evrahim-s3`

```sh
pio run -e evrahim-s3          # builds Bruce-evrahim-s3.bin at the repo root
pio run -e evrahim-s3 -t upload
```

Flash manually with:

```sh
esptool.py --port /dev/ttyACM0 write_flash 0x00000 Bruce-evrahim-s3.bin
```

No local toolchain? Push to GitHub and the `build-evrahim-s3` workflow
(`.github/workflows/build-evrahim-s3.yml`) compiles the firmware and uploads
`Bruce-evrahim-s3.bin` as an artifact. Or trigger a cloud build on demand and
auto-download the result with (requires the GitHub CLI, `gh auth login`):

```sh
./tools/run_firmware_build.sh [--branch BRANCH] [--out DIR] [--no-wait]
```

## Wiring

### Shared HSPI bus (TFT + SD + CC1101 + NRF24)

| Signal | GPIO |
| ------ | ---- |
| SCK    | 12   |
| MOSI   | 11   |
| MISO   | 13   |

### TFT — ST7789 240x320 (landscape, `ROTATION 1`)

| TFT pin | GPIO |
| ------- | ---- |
| CS      | 10   |
| DC      | 9    |
| RST     | 14   |
| BL      | 7    |
| SCK/MOSI/MISO | 12 / 11 / 13 (shared HSPI) |

### Touch — XPT2046 (dedicated FSPI bus, 2&nbsp;MHz)

| XPT2046 pin | GPIO |
| ----------- | ---- |
| SCK  | 38 |
| MOSI | 39 |
| MISO | 40 |
| CS   | 41 |
| IRQ  | 42 |

### SD card (shares HSPI with TFT)

| SD pin | GPIO |
| ------ | ---- |
| CS | 5 |
| SCK/MOSI/MISO | 12 / 11 / 13 |

### CC1101 sub-GHz (shares HSPI with TFT)

| CC1101 pin | GPIO | Bruce role |
| ---------- | ---- | ---------- |
| CSN (SS) | 21 | `CC1101_SS_PIN` |
| GDO0 | 4  | RF TX (`rfTx`) |
| GDO2 | 6  | RF RX (`rfRx`) |
| SCK/MOSI/MISO | 12 / 11 / 13 | shared HSPI |

### NRF24L01+ 2.4&nbsp;GHz, e.g. E01-MLO1DP5 (shares HSPI with TFT)

| NRF24 pin | GPIO |
| --------- | ---- |
| CE  | 47 |
| CSN | 48 |
| SCK/MOSI/MISO | 12 / 11 / 13 |

> ⚠️ High-power NRF24 PA modules (E01-MLO1DP5) draw far more current than the
> ESP32-S3 3V3 pin can supply. Power the radio from a dedicated 3.3&nbsp;V
> regulator (common GND) and add a 10–100&nbsp;µF capacitor across VCC/GND
> close to the module, otherwise range collapses and the ESP32 can brown out.

### Everything else

| Function | Pins |
| -------- | ---- |
| USB serial (flashing / monitor) + BadUSB HID | native USB (UART0 on TX=43 / RX=44) |
| User I2C (Grove) | SDA=15, SCL=16 (no system I2C devices on this board) |
| IR, mic, speaker, RGB LED, GPS, buttons | not connected |

All SPI chip-selects are actively deselected in `_setup_gpio()` before any bus
traffic starts.

## Tuning notes

- **Touch calibration / pressure**: `evrahim-s3.ini` sets
  `CYD28_TouchR_CAL_*` and `CYD28_TouchR_Z_THRESH`. The Z threshold is raised
  to 500 to reject ghost touches from long unshielded wires — lower it if
  light touches don't register, raise it if you get phantom touches.
- **Colors look wrong**: if red and blue are swapped, change `TFT_RGB_ORDER`
  in `pins_arduino.h` from `TFT_RGB` to `TFT_BGR`. If the whole image looks
  like a photo negative, flip the forced `colorInverted` value in
  `interface.cpp` (`_post_setup_gpio()`).
- **Touch rotated/mirrored**: the rotation mapping in `InputHandler()` follows
  the CYD convention (`tftHeight` reserves a 20&nbsp;px touch strip). Rotation
  1 (landscape) is the default; 0/2/3 are converted from it.
- **No IR hardware**: the IR pin lists contain a single `Disabled` entry, so
  the IR menus stay visible but have no output pin. Wire an IR LED + receiver
  to free GPIOs and extend `IR_TX_PINS` / `IR_RX_PINS` in the `.ini` to enable.
