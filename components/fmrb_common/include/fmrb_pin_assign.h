#pragma once

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"

// ============================================================
// Pin assignments for Modern boards (ESP32-P4): TAB5 / NARYAv4
// The two P4 boards share no pin but the microSD slot's, so they get a block
// each. Both blocks must define the same macro names -- a board section that
// leaves one out does not fail here, it fails in the pin manager (which is
// how the ATOM section came to be unbuildable).
// NOTE: in the Tab5 block, only the Keyboard / touch / backlight pins are
// confirmed. Pins marked TODO are GPIO_NUM_NC placeholders until verified
// against the Tab5 schematic; register_system_pin / HAL gpio calls skip NC
// safely.
// ============================================================
#if defined(FMRB_HW_TAB5)

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
// I2C2 = Tab5 Grove port (GPIO53/54). Same dual-role convention as Retro's
// GROVE port 2: either an I2C bus or serial MIDI out (TX rides SDA); the
// pin manager arbitrates at open time.
#define FMRB_PIN_I2C2_SDA      GPIO_NUM_53
#define FMRB_PIN_I2C2_SCL      GPIO_NUM_54

// Restricted pins (TODO: set real ESP32-P4 flash/PSRAM/strap pins)
#define FMRB_PIN_RESTRICTED_BOOT    GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_JTAG    GPIO_NUM_NC
// USB-Serial-JTAG data pins (P4 default: D-=GPIO24, D+=GPIO25). This is
// the USB-C flash/console link; reserving them keeps a user app from
// acquiring the pins and killing the serial console. The USB-A host port
// uses the HS OTG PHY's dedicated pads (not GPIO-muxed), so it needs no
// entry here. Note the OTG-FS alternative pins GPIO26/27 are already
// taken by I2S audio on this board -- USB must stay on the HS PHY.
#define FMRB_PIN_RESTRICTED_USB_DN  GPIO_NUM_24
#define FMRB_PIN_RESTRICTED_USB_DP  GPIO_NUM_25
#define FMRB_PIN_RESTRICTED_PSRAM0  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_PSRAM1  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_PSRAM2  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_STRAP1  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_STRAP2  GPIO_NUM_NC

// microSD. Modern wires the slot to the P4's SDMMC host (slot 0), not to a
// SPI bus, so the SPI pins stay NC and FMRB_SD_HOST_SDMMC selects the other
// bring-up path in fmrb_hal_file_esp32.c. There is no card-detect line to
// the P4: the mount code treats "no detect pin" as "assume a card is in".
#define FMRB_SD_HOST_SDMMC     1
#define FMRB_PIN_SD_CLK        GPIO_NUM_43
#define FMRB_PIN_SD_CMD        GPIO_NUM_44
#define FMRB_PIN_SD_D0         GPIO_NUM_39
#define FMRB_PIN_SD_D1         GPIO_NUM_40
#define FMRB_PIN_SD_D2         GPIO_NUM_41
#define FMRB_PIN_SD_D3         GPIO_NUM_42

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
// Pin assignments for Narya board v4 (ESP32-P4, HDMI)
//
// The board is still being designed; these are the pins of its bring-up
// hardware, the Waveshare ESP32-P4-Nano (the final board's reference
// design), read off the published schematic -- see doc/naryav4/report/p0.md.
// Expect to swap values here, and only here, when the real board arrives.
//
// Pins occupied on the board but never driven by us:
//   GPIO28-31, 34, 35, 49-52  100M Ethernet PHY (IP101GRI, RMII). Ethernet
//                             is out of scope for this machine, but the
//                             traces exist, so treat them as reserved.
//                             GPIO35 doubles as the BOOT strap/button.
//   USB-A host                HS OTG PHY dedicated pads, not GPIO-muxed.
//   MIPI-DSI                  dedicated pads (2 data lanes + clock).
// ============================================================
#elif defined(FMRB_HW_NARYAV4)

// No keyboard accessory and no touch panel: this machine drives an HDMI
// monitor and takes its input from USB. The backlight is the monitor's
// business too.
#define FMRB_PIN_KBD_I2C_SDA   GPIO_NUM_NC
#define FMRB_PIN_KBD_I2C_SCL   GPIO_NUM_NC
#define FMRB_PIN_KBD_INT       GPIO_NUM_NC
#define FMRB_PIN_TOUCH_I2C_SDA GPIO_NUM_NC
#define FMRB_PIN_TOUCH_I2C_SCL GPIO_NUM_NC
#define FMRB_PIN_TOUCH_INT     GPIO_NUM_NC
#define FMRB_PIN_LCD_BACKLIGHT GPIO_NUM_NC

// One I2C bus for the whole board: the DSI connector (LT8912B bridge on the
// Olimex adapter), the CSI connector, the ES8311 codec (0x18) and the pin
// header all hang off it. Pull-ups are fitted on the board. The display
// owns the bus and audio borrows the handle, exactly as on Tab5 -- only the
// pins differ (31/32 there, 7/8 here).
#define FMRB_PIN_I2C1_SDA      GPIO_NUM_7
#define FMRB_PIN_I2C1_SCL      GPIO_NUM_8
// No second bus: the pins Tab5 gives its Grove port (53/54) drive the
// speaker amplifier and the C6's EN line on this board.
#define FMRB_PIN_I2C2_SDA      GPIO_NUM_NC
#define FMRB_PIN_I2C2_SCL      GPIO_NUM_NC

// Restricted pins
// BOOT strap / button (shared with the Ethernet PHY block).
#define FMRB_PIN_RESTRICTED_BOOT    GPIO_NUM_35
#define FMRB_PIN_RESTRICTED_JTAG    GPIO_NUM_NC
// The USB-C console. Same role as Tab5's entry in these two slots -- keep a
// user app from acquiring the pins and killing the serial console -- but a
// different link: this board bridges UART0 (TX=37 / RX=38) through a CH343P
// rather than using the P4's USB-Serial-JTAG. The USB-A host port is on the
// HS OTG PHY's dedicated pads and needs no entry.
#define FMRB_PIN_RESTRICTED_USB_DN  GPIO_NUM_37
#define FMRB_PIN_RESTRICTED_USB_DP  GPIO_NUM_38
// PSRAM is in package on the P4; no GPIO to reserve.
#define FMRB_PIN_RESTRICTED_PSRAM0  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_PSRAM1  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_PSRAM2  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_STRAP1  GPIO_NUM_NC
#define FMRB_PIN_RESTRICTED_STRAP2  GPIO_NUM_NC

// microSD on the P4's SDMMC host (slot 0), same wiring as Tab5 -- both
// follow the Espressif reference design. No card-detect line either, so the
// mount code assumes a card is in.
#define FMRB_SD_HOST_SDMMC     1
#define FMRB_PIN_SD_CLK        GPIO_NUM_43
#define FMRB_PIN_SD_CMD        GPIO_NUM_44
#define FMRB_PIN_SD_D0         GPIO_NUM_39
#define FMRB_PIN_SD_D1         GPIO_NUM_40
#define FMRB_PIN_SD_D2         GPIO_NUM_41
#define FMRB_PIN_SD_D3         GPIO_NUM_42

// Audio: one ES8311 codec (I2C 0x18 on the bus above) for both playback and
// the on-board microphone, plus an NS4150B speaker amplifier whose enable is
// a plain GPIO here (Tab5 reaches its amplifier through a PI4IO expander).
// The I2S peripheral is still configured from audio_p4_hw.c's own constants;
// P3 moves it onto these (doc/naryav4/plan.md).
#define FMRB_PIN_I2S_MCLK      GPIO_NUM_13
#define FMRB_PIN_I2S_SCLK      GPIO_NUM_12
#define FMRB_PIN_I2S_LRCK      GPIO_NUM_10
#define FMRB_PIN_I2S_DOUT      GPIO_NUM_9   // P4 -> codec DAC (DSDIN)
#define FMRB_PIN_I2S_DIN       GPIO_NUM_11  // codec ADC -> P4 (ASDOUT)
#define FMRB_PIN_AUDIO_AMP_EN  GPIO_NUM_53

// Radio: ESP32-C6 over SDIO. The data lines belong to esp_hosted and are set
// in config/sdkconfig.defaults.naryav4; only the EN line is ours to name.
#define FMRB_PIN_RADIO_EN      GPIO_NUM_54

// Not applicable on this board (no WROVER child chip, no LEDs, no buttons
// beyond BOOT, no SPI SD).
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
//
// SUSPENDED (see CLAUDE.md): this section did not follow the P4-era
// hardware-branch rework and the target no longer builds -- it is missing
// FMRB_PIN_RESTRICTED_BOOT / _JTAG, which fmrb_hal_pin_manager_esp32.c
// requires. Fix when ATOM support resumes; do not treat it as a live bug.
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
