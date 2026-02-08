#pragma once

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"

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

// FS Proxy UART (UART0 - ESP32-S3 default console pins)
#define FMRB_PIN_FSPROXY_TX    GPIO_NUM_42
#define FMRB_PIN_FSPROXY_RX    GPIO_NUM_41

#define FMRB_PIN_USB_POWER  GPIO_NUM_1

#define FMRB_PIN_STATUS_LED  GPIO_NUM_4

#define FMRB_PIN_WROVER_RESET  GPIO_NUM_5

// Buttons
#define FMRB_PIN_BUTTON_UP      GPIO_NUM_6
#define FMRB_PIN_BUTTON_DOWN    GPIO_NUM_7
#define FMRB_PIN_BUTTON_ENTER   GPIO_NUM_8
//#define FMRB_PIN_BUTTON_CANCEL  GPIO_NUM_9

// I2C
#define FMRB_PIN_I2C1_SDA      GPIO_NUM_14
#define FMRB_PIN_I2C1_SCL      GPIO_NUM_21
#define FMRB_PIN_I2C2_SDA      GPIO_NUM_47
#define FMRB_PIN_I2C2_SCL      GPIO_NUM_48

#else

// SD Card SPI Interface (SPI3)
#define FMRB_PIN_SD_CS     0
#define FMRB_PIN_SD_MOSI   0
#define FMRB_PIN_SD_SCLK   0
#define FMRB_PIN_SD_MISO   0
#define FMRB_PIN_SD_DETECT 0

#define FMRB_PIN_FSPROXY_TX    0
#define FMRB_PIN_FSPROXY_RX    0
#endif
