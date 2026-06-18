# Family mruby Modern - ESP32-P4 (M5Stack Tab5 equivalent: 16MB flash, 32MB PSRAM)
#
# NOTE: ESP32-P4 has NO built-in radio. Wireless (WiFi/BLE) is provided by an
# ESP32-C6 coprocessor (like Tab5), so BT/WiFi are disabled here for now.
# BLE-over-C6 (esp_wifi_remote / esp_hosted) is future Modern work.
#
# This file gets the build system wired for esp32p4; values still need tuning
# with menuconfig against real hardware (PSRAM speed, partition layout, etc.).

# Performance
CONFIG_FREERTOS_HZ=1000

# Log timestamp: system time (HH:MM:SS.mmm) instead of boot milliseconds
CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM=y
CONFIG_OPTIMIZATION_LEVEL_RELEASE=y

CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y

# ESP32-P4 HP core frequency
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360

# Partition
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/partitions_p4.csv"
CONFIG_PARTITION_TABLE_FILENAME="config/partitions_p4.csv"

# for task info
CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS=2

# Wireless: P4 has no radio (handled by ESP32-C6 coprocessor). Disable for now.
CONFIG_ESP_WIFI_ENABLED=n
# BT is intentionally left disabled (no CONFIG_BT_ENABLED).

# SPI-RAM (Tab5: 32MB PSRAM on ESP32-P4)
# Tab5 uses 16-line (HEX) PSRAM at 200MHz; M5GFX/the DSI framebuffer require
# 200MHz. These match the IDF esp32p4 defaults but are set explicitly to make
# the Tab5 requirement intentional.
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
# CONFIG_SPIRAM_USE_MALLOC is not set
# for large static global mem
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y

# Flash (16MB)
CONFIG_ESPTOOLPY_FLASHMODE="qio"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

CONFIG_PM_ENABLE=n

# Performance optimizations
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE=y

# Logging configuration
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_DEFAULT_LEVEL=3

# FreeRTOS
CONFIG_FREERTOS_IDLE_TASK_STACKSIZE=1024
CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=n

# Task Watchdog: stop monitoring IDLE tasks (see Retro config rationale).
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n

# SPI driver optimizations for real-time performance
CONFIG_SPI_MASTER_ISR_IN_IRAM=y
CONFIG_SPI_SLAVE_ISR_IN_IRAM=y

# USB Hub Support
CONFIG_USB_HOST_HUBS_SUPPORTED=y

# Avoid LittleFS Crash in app task
CONFIG_LITTLEFS_MALLOC_STRATEGY_DEFAULT=n
CONFIG_LITTLEFS_MALLOC_STRATEGY_INTERNAL=y
