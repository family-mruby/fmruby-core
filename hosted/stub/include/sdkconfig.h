/*
 * Minimal sdkconfig.h for the hosted (wasm and native) FreeRTOS port.
 *
 * The vendored kernel is ESP-IDF's copy, so its sources and the esp_additions
 * we vendor with it read CONFIG_* directly. There is no Kconfig here: this file
 * pins the handful of switches those sources actually look at, to the values the
 * Linux simulation build resolves them to (config/sdkconfig.defaults.linux plus
 * the IDF defaults it inherits).
 *
 * Anything not listed here is deliberately absent: the kernel treats an
 * undefined CONFIG_* as 0, which is what we want for every IDF service the
 * hosted targets do not have.
 */
#pragma once

/* Symbols that are *off* are left undefined on purpose: ESP-IDF sources test
 * some of them with #ifdef, so defining one to 0 would turn the feature on. */

/* The core sources gate POSIX-vs-ESP32 behavior on CONFIG_IDF_TARGET_LINUX,
 * and both hosts (Emscripten, glibc) are POSIX enough to take the same side
 * everywhere those branches reach (no IDF drivers, libc time, plain files).
 * The places where the linux side means "the 3-process simulation" and is
 * wrong for a one-process build are overridden with FMRB_PLATFORM_HOSTED
 * (boot.c, the display/input/audio paths); what only the browser needs, with
 * FMRB_PLATFORM_WASM -- see doc/wasm/report/p4a.md and
 * doc/linux_native/report/l1.md. */
#define CONFIG_IDF_TARGET_LINUX                   1
#define CONFIG_FREERTOS_UNICORE                   1
#define CONFIG_FREERTOS_NUMBER_OF_CORES           1
#define CONFIG_FREERTOS_HZ                        1000
#define CONFIG_FREERTOS_NO_AFFINITY               0x7FFFFFFF
#define CONFIG_FREERTOS_MAX_TASK_NAME_LEN         16
#define CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS 3
#define CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS   1
#define CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES 1
#define CONFIG_FREERTOS_QUEUE_REGISTRY_SIZE       0
#define CONFIG_FREERTOS_IDLE_TASK_STACKSIZE       4096
#define CONFIG_FREERTOS_CHECK_MUTEX_GIVEN_BY_OWNER 1
#define CONFIG_FREERTOS_ENABLE_TASK_SNAPSHOT      1
#define CONFIG_FREERTOS_USE_TRACE_FACILITY        1
