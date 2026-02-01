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

// FS Proxy UART (UART0 - ESP32-S3 default console pins)
#define FMRB_PIN_FSPROXY_TX    GPIO_NUM_42
#define FMRB_PIN_FSPROXY_RX    GPIO_NUM_41

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
