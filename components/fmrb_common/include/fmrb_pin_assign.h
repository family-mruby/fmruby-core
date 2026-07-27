#pragma once

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"

// ============================================================
// Pin assignments for Modern boards (ESP32-P4): TAB5 / NARYAv4
// NARYAv4 (future dedicated P4 board) is not designed yet and shares the
// Tab5 assignment as a placeholder; give it its own #elif block once the
// schematic lands.
// NOTE: only the Tab5 Keyboard / touch / backlight pins are confirmed.
// Pins marked TODO are GPIO_NUM_NC placeholders until verified against the
// Tab5 schematic; register_system_pin / HAL gpio calls skip NC safely.
// ============================================================
#if defined(FMRB_HW_TAB5) || defined(FMRB_HW_NARYAV4)

// Tab5 Keyboard accessory (STM32F030 @ I2C 0x6D)
#define FMRB_PIN_KBD_I2C_SDA   GPIO_NUM_0
#define FMRB_PIN_KBD_I2C_SCL   GPIO_NUM_1
#define FMRB_PIN_KBD_INT       GPIO_NUM_50

// Body capacitive touch (GT911/ST7123 @ I2C 0x55) + display backlight.
// The DSI panel and touch are driven via LovyanGFX; these are informational
// (LovyanGFX configures the bus/INT itself from its panel config).
#define FMRB_PIN_TOUCH_I2C_SDA GPIO_NUM_31
#define FMRB_PIN_TOUCH_I2C_SCL GPIO_NUM_32
#define FMRB_PIN_TOUCH_INT     GPIO_NUM_23
#define FMRB_PIN_LCD_BACKLIGHT GPIO_NUM_22

// Generic I2C (reuse the body touch bus as I2C1 for now)
#define FMRB_PIN_I2C1_SDA      GPIO_NUM_31
#define FMRB_PIN_I2C1_SCL      GPIO_NUM_32
#define FMRB_PIN_I2C2_SDA      GPIO_NUM_NC
#define FMRB_PIN_I2C2_SCL      GPIO_NUM_NC

// Restricted pins (TODO: set real ESP32-P4 flash/PSRAM/USB/strap pins)
#define FMRB_PIN_RESTRICTED_BOOT    GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_JTAG    GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_USB_DN  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_USB_DP  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_PSRAM0  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_PSRAM1  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_PSRAM2  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_STRAP1  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_STRAP2  GPIO_NUM_NC

// Not applicable / not yet mapped on Tab5 (no WROVER child chip). NC = unused.
#define FMRB_PIN_SD_CS         GPIO_NUM_NC
#define FMRB_PIN_SD_MOSI       GPIO_NUM_NC
#define FMRB_PIN_SD_SCLK       GPIO_NUM_NC
#define FMRB_PIN_SD_MISO       GPIO_NUM_NC
#define FMRB_PIN_SD_DETECT     GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_MOSI  GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_MISO  GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_SCLK  GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_CS    GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_INTR  GPIO_NUM_NC
#define FMRB_PIN_GFX_UART_TX   GPIO_NUM_NC
#define FMRB_PIN_GFX_UART_RX   GPIO_NUM_NC
#define FMRB_PIN_GFX_UART_RTS  GPIO_NUM_NC
#define FMRB_PIN_GFX_UART_CTS  GPIO_NUM_NC
#define FMRB_PIN_USB_POWER     GPIO_NUM_NC
#define FMRB_PIN_STATUS_LED    GPIO_NUM_NC
#define FMRB_PIN_ERROR_LED     GPIO_NUM_NC
#define FMRB_PIN_WROVER_RESET  GPIO_NUM_NC
#define FMRB_PIN_BUTTON_UP     GPIO_NUM_NC
#define FMRB_PIN_BUTTON_DOWN   GPIO_NUM_NC
#define FMRB_PIN_BUTTON_ENTER  GPIO_NUM_NC

// ============================================================
// Pin assignments for Narya board v3 (N16R8, default)
// ============================================================
#elif !defined(FMRB_HW_ATOM_DISPLAY)

// ============================================================
// ESP32-S3 WROOM package: hardware-restricted pins
// These pins must not be used by user applications.
// ============================================================
#define FMRB_PIN_RESTRICTED_BOOT    GPIO_NUM_0   // UART reflash strapping pin
#define FMRB_PIN_RESTRICTED_JTAG    GPIO_NUM_3   // JTAG pull-down
#define FMRB_PIN_RESTRICTED_USB_DN  GPIO_NUM_19  // USB D-
#define FMRB_PIN_RESTRICTED_USB_DP  GPIO_NUM_20  // USB D+
#define FMRB_PIN_RESTRICTED_PSRAM0  GPIO_NUM_35  // PSRAM 8M (N8R8/N16R8)
#define FMRB_PIN_RESTRICTED_PSRAM1  GPIO_NUM_36  // PSRAM 8M
#define FMRB_PIN_RESTRICTED_PSRAM2  GPIO_NUM_37  // PSRAM 8M
#define FMRB_PIN_RESTRICTED_STRAP1  GPIO_NUM_45  // Strapping pin (VDD_SPI)
#define FMRB_PIN_RESTRICTED_STRAP2  GPIO_NUM_46  // Strapping pin (boot mode)

// SD Card SPI Interface (SPI3)
#define FMRB_PIN_SD_CS     GPIO_NUM_15
#define FMRB_PIN_SD_MOSI   GPIO_NUM_16
#define FMRB_PIN_SD_SCLK   GPIO_NUM_17
#define FMRB_PIN_SD_MISO   GPIO_NUM_18
#define FMRB_PIN_SD_DETECT GPIO_NUM_38

// SPI for Graphics-Audio board communication (SPI2 - Master)
#define FMRB_PIN_GFX_SPI_MOSI  GPIO_NUM_11
#define FMRB_PIN_GFX_SPI_MISO  GPIO_NUM_13
#define FMRB_PIN_GFX_SPI_SCLK  GPIO_NUM_12
#define FMRB_PIN_GFX_SPI_CS    GPIO_NUM_10
#define FMRB_PIN_GFX_SPI_INTR  GPIO_NUM_9

// UART for Graphics-Audio board communication (UART1 - reuses SPI wiring)
#define FMRB_PIN_GFX_UART_TX   GPIO_NUM_11  // -> WROVER RX (GPIO21)
#define FMRB_PIN_GFX_UART_RX   GPIO_NUM_13  // <- WROVER TX (GPIO18)
#define FMRB_PIN_GFX_UART_RTS  GPIO_NUM_12  // -> WROVER CTS (GPIO19)
#define FMRB_PIN_GFX_UART_CTS  GPIO_NUM_10  // <- WROVER RTS (GPIO22)

#define FMRB_PIN_USB_POWER  GPIO_NUM_1

#define FMRB_PIN_STATUS_LED  GPIO_NUM_4
#define FMRB_PIN_ERROR_LED   GPIO_NUM_39  // Red LED, shared with MTCK

#define FMRB_PIN_WROVER_RESET  GPIO_NUM_5

// Buttons
#define FMRB_PIN_BUTTON_UP      GPIO_NUM_6
#define FMRB_PIN_BUTTON_DOWN    GPIO_NUM_7
#define FMRB_PIN_BUTTON_ENTER   GPIO_NUM_8

// I2C
#define FMRB_PIN_I2C1_SDA      GPIO_NUM_14
#define FMRB_PIN_I2C1_SCL      GPIO_NUM_21
#define FMRB_PIN_I2C2_SDA      GPIO_NUM_47
#define FMRB_PIN_I2C2_SCL      GPIO_NUM_48

// ============================================================
// Pin assignments for AtomS3 + Atom Display (N8R8)
// ============================================================
#else // FMRB_HW_ATOM_DISPLAY

// Atom Display HDMI - SPI (SPI2, directly used by M5AtomDisplay)
#define FMRB_PIN_HDMI_SPI_MOSI  GPIO_NUM_6
#define FMRB_PIN_HDMI_SPI_MISO  GPIO_NUM_5
#define FMRB_PIN_HDMI_SPI_SCLK  GPIO_NUM_7
#define FMRB_PIN_HDMI_SPI_CS    GPIO_NUM_8

// Atom Display HDMI - I2C (for HDMI transmitter)
#define FMRB_PIN_HDMI_I2C_SDA   GPIO_NUM_38
#define FMRB_PIN_HDMI_I2C_SCL   GPIO_NUM_39

// AtomS3 built-in button
#define FMRB_PIN_BUTTON_ENTER   GPIO_NUM_41

// AtomS3 built-in LED (optional, IR LED on some models)
#define FMRB_PIN_STATUS_LED     GPIO_NUM_4
#define FMRB_PIN_ERROR_LED      GPIO_NUM_NC

// I2C (Grove port on AtomS3)
#define FMRB_PIN_I2C1_SDA      GPIO_NUM_2
#define FMRB_PIN_I2C1_SCL      GPIO_NUM_1

// Pins not available on AtomS3 (defined as GPIO_NUM_NC for compile compatibility)
#define FMRB_PIN_SD_CS          GPIO_NUM_NC
#define FMRB_PIN_SD_MOSI        GPIO_NUM_NC
#define FMRB_PIN_SD_SCLK        GPIO_NUM_NC
#define FMRB_PIN_SD_MISO        GPIO_NUM_NC
#define FMRB_PIN_SD_DETECT      GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_MOSI   GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_MISO   GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_SCLK   GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_CS     GPIO_NUM_NC
#define FMRB_PIN_GFX_SPI_INTR   GPIO_NUM_NC
#define FMRB_PIN_USB_POWER      GPIO_NUM_NC
#define FMRB_PIN_WROVER_RESET   GPIO_NUM_NC
#define FMRB_PIN_BUTTON_UP      GPIO_NUM_NC
#define FMRB_PIN_BUTTON_DOWN    GPIO_NUM_NC
#define FMRB_PIN_I2C2_SDA       GPIO_NUM_NC
#define FMRB_PIN_I2C2_SCL       GPIO_NUM_NC

#endif // FMRB_HW_ATOM_DISPLAY

#else

// SD Card SPI Interface (SPI3)
#define FMRB_PIN_SD_CS     0
#define FMRB_PIN_SD_MOSI   0
#define FMRB_PIN_SD_SCLK   0
#define FMRB_PIN_SD_MISO   0
#define FMRB_PIN_SD_DETECT 0

#define FMRB_PIN_WROVER_RESET  0
#define FMRB_PIN_STATUS_LED    0
#define FMRB_PIN_ERROR_LED     0
#endif
