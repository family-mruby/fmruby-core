#pragma once

#include <stdint.h>

 // Filesystem proxy task
#define FMRB_FSPROXY_TASK_STACK_SIZE (60 * 1024)
#define FMRB_FSPROXY_TASK_PRIORITY (4)


// Kernel task
#define FMRB_KERNEL_TASK_STACK_SIZE (60 * 1024)
#define FMRB_KERNEL_TASK_PRIORITY (9)

// Host task for Kernel
#define FMRB_HOST_TASK_STACK_SIZE (32 * 1024)
#define FMRB_HOST_TASK_PRIORITY (10)

// -- Base App task
// System App task
#define FMRB_SYSTEM_APP_TASK_STACK_SIZE (60 * 1024)
#define FMRB_SYSTEM_APP_TASK_PRIORITY (8)

// Shell
#define FMRB_SHELL_APP_TASK_STACK_SIZE (60 * 1024)
#define FMRB_SHELL_APP_PRIORITY (5)

// User App task
#define FMRB_USER_APP_TASK_STACK_SIZE (60 * 1024)
#define FMRB_USER_APP_PRIORITY (5)

// Maximum number of concurrent apps
#define FMRB_MAX_APPS (PROC_ID_MAX)
#define FMRB_MAX_USER_APPS (PROC_ID_MAX - PROC_ID_USER_APP0)

// Message Queue Lengths
// ----------------------
// Queue sizes are designed to handle burst traffic and prevent message loss
// during heavy usage (e.g., rapid mouse movements, fast typing, many graphics commands)

// HOST task: High-frequency graphics commands, HID events from USB
// Large queue needed as this is the main communication hub
#define FMRB_HOST_MSG_QUEUE_LEN (128)

// KERNEL task: HID events (mouse/keyboard), window management, app lifecycle
// Medium queue for handling mouse/keyboard event bursts
#define FMRB_KERNEL_MSG_QUEUE_LEN (32)

// SYSTEM_APP task: System-level application messages
#define FMRB_SYSTEM_APP_MSG_QUEUE_LEN (32)

// USER_APP tasks: User application messages (keyboard input, custom messages)
#define FMRB_USER_APP_MSG_QUEUE_LEN (32)

// Default queue length for unconfigured tasks
#define FMRB_DEFAULT_MSG_QUEUE_LEN (10)

typedef enum FMRB_PROC_ID{
    PROC_ID_KERNEL = 0,
    PROC_ID_HOST,
    PROC_ID_SYSTEM_APP,
    PROC_ID_USER_APP0,
    PROC_ID_USER_APP1,
    PROC_ID_USER_APP2,
    PROC_ID_MAX
} fmrb_proc_id_t;