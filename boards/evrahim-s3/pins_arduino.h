#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// ============================================================================
//  Evrahim S3 Custom Board -- ESP32-S3 N16R8
//  2.8" ST7789 (240x320) + XPT2046 Touch + CC1101 + NRF24L01+ (E01-MLO1DP5)
// ============================================================================

// ----- SERIAL -----
#define SERIAL_TX 43
#define SERIAL_RX 44
static const uint8_t TX = SERIAL_TX;
static const uint8_t RX = SERIAL_RX;
#define GPS_SERIAL_TX -1
#define GPS_SERIAL_RX -1

// ----- BAD USB -----
#define USB_as_HID 1

// ----- I2C (Grove / System) -----
// No dedicated I2C header defined; use defaults that don't collide
// with the SPI buses. Adjust if you wire I2C peripherals.
#define GROVE_SDA 15
#define GROVE_SCL 16
#define SYS_I2C_SDA -1
#define SYS_I2C_SCL -1
static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ========================================================================
//  HSPI Bus (SPI3) -- shared by TFT, SD Card, CC1101, NRF24
// ========================================================================
static const uint8_t SS   = 10;
static const uint8_t MOSI = 11;
static const uint8_t MISO = 13;
static const uint8_t SCK  = 12;

// ========================================================================
//  TFT Display -- ST7789 240x320 on HSPI
// ========================================================================
#define HAS_SCREEN 1
#define ROTATION 1
#define MINBRIGHT (uint8_t)1

#define USER_SETUP_LOADED 1
#define ST7789_DRIVER 1
// TFT_RGB = Red-Green-Blue order. If red and blue look swapped on your panel,
// change this to TFT_BGR.
#define TFT_RGB_ORDER TFT_RGB
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_BACKLIGHT_ON HIGH
#define TFT_BL 7
#define TFT_RST 14
#define TFT_DC 9
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO 13
#define TFT_CS 10
#define TOUCH_CS -1   // XPT2046 is on a separate SPI bus, not TFT_eSPI managed
#define SMOOTH_FONT 1
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000

// ----- Font Sizes -----
// NOTE: FP/FM/FG are intentionally NOT defined here. They come from
// include/precompiler_flags.h (via globals.h) with identical values (1/2/3).
// Defining FP in the Arduino variant header breaks third-party libraries
// (FastLED 3.10 uses `FP` as a C++ identifier in its animartrix code and every
// library TU includes pins_arduino.h through Arduino.h).

// ========================================================================
//  XPT2046 Touchscreen -- on FSPI (SPI2), separate from TFT
// ========================================================================
#define HAS_TOUCH 1
// NOTE: TOUCH_XPT2046_SPI is defined via -D in evrahim-s3.ini (same convention
// as the lilygo-t-hmi and marauder-touch boards).
#define XPT2046_SPI_BUS_SCLK_IO_NUM  38
#define XPT2046_SPI_BUS_MOSI_IO_NUM  39
#define XPT2046_SPI_BUS_MISO_IO_NUM  40
#define XPT2046_SPI_CONFIG_CS_GPIO_NUM 41
#define XPT2046_TOUCH_CONFIG_INT_GPIO_NUM 42

// ========================================================================
//  SD Card -- shares HSPI with TFT (dedicated CS pin 5)
// ========================================================================
#define SDCARD_CS 5
#define SDCARD_SCK 12
#define SDCARD_MISO 13
#define SDCARD_MOSI 11

// ========================================================================
//  CC1101 Sub-GHz Transceiver -- on HSPI, own CS
// ========================================================================
#define USE_CC1101_VIA_SPI
#define CC1101_GDO0_PIN 4    // TX / Modulation
#define CC1101_GDO2_PIN 6    // RX / Demodulation
#define CC1101_SS_PIN 21     // Chip Select
#define CC1101_MOSI_PIN 11
#define CC1101_SCK_PIN 12
#define CC1101_MISO_PIN 13

// Aux SPI bus pins (used by the firmware for CC1101/NRF24 bus arbitration)
#define SPI_SCK_PIN 12
#define SPI_MOSI_PIN 11
#define SPI_MISO_PIN 13
#define SPI_SS_PIN 21

// ========================================================================
//  NRF24L01+ (E01-MLO1DP5) 2.4GHz -- on HSPI, own CS
// ========================================================================
#define USE_NRF24_VIA_SPI
#define NRF24_CE_PIN 47      // Chip Enable
#define NRF24_SS_PIN 48      // Chip Select (CSN)
#define NRF24_MOSI_PIN 11
#define NRF24_SCK_PIN 12
#define NRF24_MISO_PIN 13

// ========================================================================
//  IR -- not connected
// ========================================================================
#define TXLED -1
#define RXLED -1
#define LED_ON HIGH
#define LED_OFF LOW

// ========================================================================
//  Buttons -- touch-only, no physical buttons
// ========================================================================
#define BTN_ALIAS "\"OK\""
#define HAS_BTN 0

// ========================================================================
//  Backlight PWM
// ========================================================================
#define BACKLIGHT 7

// ========================================================================
//  Deep Sleep
// ========================================================================
#define DEEPSLEEP_WAKEUP_PIN 0
#define DEEPSLEEP_PIN_ACT LOW

#endif /* Pins_Arduino_h */
