#pragma once

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"

// SD Card SPI Interface (SPI3)
#define FMRB_PIN_SD_CS     GPIO_NUM_15
#define FMRB_PIN_SD_MOSI   GPIO_NUM_16
#define FMRB_PIN_SD_SCLK   GPIO_NUM_17
#define FMRB_PIN_SD_MISO   GPIO_NUM_18
#define FMRB_PIN_SD_DETECT GPIO_NUM_38

// UART for Filesyatem Proxy
#define FMRB_PIN_FSPROXY_TX  GPIO_NUM_43  //UART0 TX
#define FMRB_PIN_FSPROXY_RX  GPIO_NUM_44  //UART0 RX

#else

// SD Card SPI Interface (SPI3)
#define FMRB_PIN_SD_CS     0
#define FMRB_PIN_SD_MOSI   0
#define FMRB_PIN_SD_SCLK   0
#define FMRB_PIN_SD_MISO   0
#define FMRB_PIN_SD_DETECT 0

// UART for Filesyatem Proxy
#define FMRB_PIN_FSPROXY_TX  0  //UART0 TX
#define FMRB_PIN_FSPROXY_RX  0  //UART0 RX

#endif
