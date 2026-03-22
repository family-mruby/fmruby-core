#pragma once

#include <stdint.h>
#include "fmrb_rtos.h"

// ============================================================
// Task Configuration
// ============================================================
// All task stack sizes, priorities, and creation flags are
// defined here for centralized management.
//
// FMRB_TASK_FLAG_PSRAM:    stack allocated on PSRAM (saves internal RAM)
// FMRB_TASK_FLAG_PINNED_0: pinned to core 0
// FMRB_TASK_FLAG_PINNED_1: pinned to core 1
// FMRB_TASK_FLAG_NONE:     internal RAM stack, any core
//
// WARNING: Tasks with PSRAM stacks must NOT pass stack-local
// buffers to SPI flash DMA (LittleFS, FAT, etc.).
// Use heap_caps_malloc(MALLOC_CAP_INTERNAL) for DMA buffers.
// ============================================================

// --- mruby App tasks (PSRAM stack) ---

// Kernel task (mruby VM for OS kernel)
#define FMRB_KERNEL_TASK_STACK_SIZE     (12 * 1024)
#define FMRB_KERNEL_TASK_PRIORITY       (9)
#define FMRB_KERNEL_TASK_FLAGS          FMRB_TASK_FLAG_PSRAM

// Host task (graphics/audio transport, no flash I/O)
#define FMRB_HOST_TASK_STACK_SIZE       (12 * 1024)
#define FMRB_HOST_TASK_PRIORITY         (10)
#define FMRB_HOST_TASK_FLAGS            FMRB_TASK_FLAG_PSRAM

// System App task (mruby VM for system GUI)
#define FMRB_SYSTEM_APP_TASK_STACK_SIZE (12 * 1024)
#define FMRB_SYSTEM_APP_TASK_PRIORITY   (8)
#define FMRB_SYSTEM_APP_TASK_FLAGS      FMRB_TASK_FLAG_PSRAM

// Shell App task (mruby VM for shell)
#define FMRB_SHELL_APP_TASK_STACK_SIZE  (12 * 1024)
#define FMRB_SHELL_APP_PRIORITY         (5)
#define FMRB_SHELL_APP_TASK_FLAGS       FMRB_TASK_FLAG_PSRAM

// User App task (mruby/lua/basic VM for user apps)
#define FMRB_USER_APP_TASK_STACK_SIZE   (12 * 1024)
#define FMRB_USER_APP_PRIORITY          (5)
#define FMRB_USER_APP_TASK_FLAGS        FMRB_TASK_FLAG_PSRAM

// --- Infrastructure tasks ---

// Filesystem proxy task (UART-based external FS access, uses flash I/O)
#define FMRB_FSPROXY_TASK_STACK_SIZE    (20 * 1024)
#define FMRB_FSPROXY_TASK_PRIORITY      (4)
#define FMRB_FSPROXY_TASK_FLAGS         FMRB_TASK_FLAG_NONE

// RTC task (I2C only, no flash I/O)
#define FMRB_RTC_TASK_STACK_SIZE        (4096)
#define FMRB_RTC_TASK_PRIORITY          (3)
#define FMRB_RTC_TASK_FLAGS             FMRB_TASK_FLAG_PSRAM

// Status LED task (GPIO only)
#define FMRB_STATUS_LED_TASK_STACK_SIZE (1024)
#define FMRB_STATUS_LED_TASK_PRIORITY   (2)
#define FMRB_STATUS_LED_TASK_FLAGS      FMRB_TASK_FLAG_NONE

// USB host library task (USB DMA needs internal RAM, pinned to core 0)
#define FMRB_USB_HOST_TASK_STACK_SIZE   (4096)
#define FMRB_USB_HOST_TASK_PRIORITY     (5)
#define FMRB_USB_HOST_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_0

// USB HID host task (pinned to core 0)
#define FMRB_USB_HID_TASK_STACK_SIZE    (4096)
#define FMRB_USB_HID_TASK_PRIORITY      (5)
#define FMRB_USB_HID_TASK_FLAGS         FMRB_TASK_FLAG_PINNED_0

// SPI connection check task (SPI slave, pinned to core 0)
#define FMRB_SPI_CONN_TASK_STACK_SIZE   (4096)
#define FMRB_SPI_CONN_TASK_PRIORITY     (5)
#define FMRB_SPI_CONN_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_1

// BLE task (managed by NimBLE, config referenced in sdkconfig)
#define FMRB_BLE_TASK_STACK_SIZE        (4096)
#define FMRB_BLE_TASK_PRIORITY          (4)

// ============================================================
// Maximum number of concurrent apps
// ============================================================
#define FMRB_MAX_APPS (PROC_ID_MAX)
#define FMRB_MAX_USER_APPS (PROC_ID_MAX - PROC_ID_USER_APP0)

// ============================================================
// Message Queue Lengths
// ============================================================

// HOST task: High-frequency graphics commands, HID events from USB
#define FMRB_HOST_MSG_QUEUE_LEN (128)

// HOST task message queue management
#define FMRB_HOST_HID_RESERVED_SLOTS (32)
#define FMRB_HOST_GFX_AVAILABLE_SLOTS (FMRB_HOST_MSG_QUEUE_LEN - FMRB_HOST_HID_RESERVED_SLOTS)

// KERNEL task: HID events, window management, app lifecycle
#define FMRB_KERNEL_MSG_QUEUE_LEN (32)

// SYSTEM_APP task: System-level application messages
#define FMRB_SYSTEM_APP_MSG_QUEUE_LEN (32)

// USER_APP tasks: User application messages
#define FMRB_USER_APP_MSG_QUEUE_LEN (32)

// Default queue length for unconfigured tasks
#define FMRB_DEFAULT_MSG_QUEUE_LEN (10)

// ============================================================
// Process IDs
// ============================================================
typedef enum FMRB_PROC_ID{
    PROC_ID_KERNEL = 0,
    PROC_ID_HOST,
    PROC_ID_SYSTEM_APP,
    PROC_ID_USER_APP0,
    PROC_ID_USER_APP1,
    PROC_ID_USER_APP2,
    PROC_ID_MAX
} fmrb_proc_id_t;
