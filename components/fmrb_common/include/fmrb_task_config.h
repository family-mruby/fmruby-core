#pragma once

#include <stdint.h>

// ============================================================
// System Limits
// ============================================================

#define FMRB_MAX_SYSTEM_MRUBY_TASKS (4)  // Kernel, Host, System Desktop, System Overlay

#define FMRB_MAX_APPS (7)  // Max number of apps (including system, mruby, lua and basic apps)

// Maximum number of mruby VMs registered for tick delivery
#define FMRB_MRB_MAX_VMS (FMRB_MAX_SYSTEM_MRUBY_TASKS + FMRB_MAX_APPS)

// Maximum number of tasks tracked by fmrb_task monitor
#define FMRB_TASK_MONITOR_MAX (FMRB_MRB_MAX_VMS + 6) // +6 for infrastructure tasks (filesystem proxy, RTC, status LED, USB host, SPI conn check, BLE FS)

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

// --- Core Assignment Policy ---
// Core 0: HW-facing tasks (USB, SPI, GPIO, RTC, LED, FS proxy, host transport)
// Core 1: mruby VM / app tasks (kernel, system_desktop, system_overlay, shell, user apps)
//
// Note: PSRAM tasks use xTaskCreateWithCaps which does not support core pinning.
// PSRAM tasks run on any core (FreeRTOS scheduler decides).
// Internal RAM tasks can be pinned to a specific core.

// --- mruby App tasks (Core 1, PSRAM stack) ---

// Kernel task (mruby VM for OS kernel)
#define FMRB_KERNEL_TASK_STACK_SIZE     (12 * 1024)
#define FMRB_KERNEL_TASK_PRIORITY       (9)
#define FMRB_KERNEL_TASK_FLAGS          FMRB_TASK_FLAG_PINNED_1

// Host task (graphics/audio transport, SPI slave comm, heap alloc)
// Must be internal RAM (uses realloc via msgpack)
#define FMRB_HOST_TASK_STACK_SIZE       (16 * 1024)
#define FMRB_HOST_TASK_PRIORITY         (10)
#define FMRB_HOST_TASK_FLAGS            FMRB_TASK_FLAG_PINNED_0

// System App task (mruby VM for system GUI)
#define FMRB_SYSTEM_APP_TASK_STACK_SIZE (12 * 1024)
#define FMRB_SYSTEM_APP_TASK_PRIORITY   (8)
#define FMRB_SYSTEM_APP_TASK_FLAGS      FMRB_TASK_FLAG_PINNED_1

// Shell App task (mruby VM for shell)
#define FMRB_SHELL_APP_TASK_STACK_SIZE  (12 * 1024)
#define FMRB_SHELL_APP_PRIORITY         (5)
#define FMRB_SHELL_APP_TASK_FLAGS       FMRB_TASK_FLAG_PINNED_1

// User App task (mruby/lua/basic VM for user apps)
#define FMRB_USER_APP_TASK_STACK_SIZE   (16 * 1024)
#define FMRB_USER_APP_PRIORITY          (5)
#define FMRB_USER_APP_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_1

// --- Infrastructure tasks (Core 0, internal RAM) ---

// Filesystem proxy task (UART-based external FS access, uses flash I/O)
#define FMRB_FSPROXY_TASK_STACK_SIZE    (20 * 1024)
#define FMRB_FSPROXY_TASK_PRIORITY      (4)
#define FMRB_FSPROXY_TASK_FLAGS         FMRB_TASK_FLAG_PINNED_0

// RTC task (I2C, low priority)
#define FMRB_RTC_TASK_STACK_SIZE        (4096)
#define FMRB_RTC_TASK_PRIORITY          (3)
#define FMRB_RTC_TASK_FLAGS             FMRB_TASK_FLAG_PINNED_1

// Status LED task (GPIO, low priority)
#define FMRB_STATUS_LED_TASK_STACK_SIZE (4096)
#define FMRB_STATUS_LED_TASK_PRIORITY   (2)
#define FMRB_STATUS_LED_TASK_FLAGS      FMRB_TASK_FLAG_PINNED_1

// USB host library task (USB DMA needs internal RAM)
#define FMRB_USB_HOST_TASK_STACK_SIZE   (4096)
#define FMRB_USB_HOST_TASK_PRIORITY     (5)
#define FMRB_USB_HOST_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_0

// USB HID host task
#define FMRB_USB_HID_TASK_STACK_SIZE    (4096)
#define FMRB_USB_HID_TASK_PRIORITY      (5)
#define FMRB_USB_HID_TASK_FLAGS         FMRB_TASK_FLAG_PINNED_0

// SPI connection check task (SPI slave)
#define FMRB_SPI_CONN_TASK_STACK_SIZE   (4096)
#define FMRB_SPI_CONN_TASK_PRIORITY     (5)
#define FMRB_SPI_CONN_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_1

// HW proxy task (internal RAM, handles file I/O for PSRAM tasks)
#define FMRB_HW_PROXY_TASK_STACK_SIZE   (8 * 1024)
#define FMRB_HW_PROXY_TASK_PRIORITY     (6)
#define FMRB_HW_PROXY_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_0

// BLE task (managed by NimBLE, config referenced in sdkconfig)
#define FMRB_BLE_TASK_STACK_SIZE        (4096)
#define FMRB_BLE_TASK_PRIORITY          (4)

// BLE file service processing task (file I/O needs internal RAM for flash DMA)
#define FMRB_BLE_FS_TASK_STACK_SIZE     (8 * 1024)
#define FMRB_BLE_FS_TASK_PRIORITY       (4)
#define FMRB_BLE_FS_TASK_FLAGS          FMRB_TASK_FLAG_PINNED_0

// M5GFX receiver task (GFX commands via local Message Buffer)
#define FMRB_M5GFX_TASK_STACK_SIZE      (8 * 1024)
#define FMRB_M5GFX_TASK_PRIORITY        (6)
#define FMRB_M5GFX_TASK_FLAGS           FMRB_TASK_FLAG_PINNED_0

// I2C keyboard polling task (ATOM_DISPLAY mode)
#define FMRB_I2C_KBD_TASK_STACK_SIZE    (4096)
#define FMRB_I2C_KBD_TASK_PRIORITY      (5)
#define FMRB_I2C_KBD_TASK_FLAGS         FMRB_TASK_FLAG_PINNED_0

// ============================================================
// Maximum number of concurrent apps
// (FMRB_MAX_APPS is defined in System Limits section above)
// ============================================================
#define FMRB_MAX_USER_APPS (FMRB_MAX_APPS - PROC_ID_USER_APP0)

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
    PROC_ID_SYSTEM_OVERLAY,
    PROC_ID_USER_APP0,
    PROC_ID_USER_APP1,
    PROC_ID_USER_APP2,
    PROC_ID_USER_APP3,
    PROC_ID_USER_APP4,
    PROC_ID_USER_APP5,
    PROC_ID_MAX
} fmrb_proc_id_t;
