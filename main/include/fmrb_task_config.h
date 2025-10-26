#pragma once

#include <stdint.h>

/*
 * FreeRTOS Task Priority Guidelines for ESP32
 *
 * Priority Range: 0-24 (configMAX_PRIORITIES = 25)
 * Higher number = Higher priority
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │ Priority │ Usage                        │ Examples                  │
 * ├──────────┼──────────────────────────────┼───────────────────────────┤
 * │ 20-24    │ Critical System Tasks        │ WiFi, Bluetooth drivers   │
 * │          │ (ESP-IDF internal, avoid)    │ Hardware interrupt tasks  │
 * ├──────────┼──────────────────────────────┼───────────────────────────┤
 * │ 15-19    │ Hard Real-time Tasks         │ I2S DMA, PWM control      │
 * │          │ (<1ms latency required)      │ Protocol stacks           │
 * ├──────────┼──────────────────────────────┼───────────────────────────┤
 * │ 10-14    │ High Priority Tasks          │ Audio decode, Network     │
 * │          │ (10-100ms latency OK)        │ HTTP server, MQTT client  │
 * ├──────────┼──────────────────────────────┼───────────────────────────┤
 * │  5-9     │ Application Tasks            │ UI processing, Main logic │
 * │          │ (100-500ms latency OK)       │ Sensor reading, Ruby VM   │
 * │          │ ** Most user tasks here **   │                           │
 * ├──────────┼──────────────────────────────┼───────────────────────────┤
 * │  3-4     │ Background Tasks             │ Logging, Statistics       │
 * │          │ (>1s latency OK)             │ Non-critical services     │
 * ├──────────┼──────────────────────────────┼───────────────────────────┤
 * │  1-2     │ Low Priority Tasks           │ Debug output, Power mgmt  │
 * ├──────────┼──────────────────────────────┼───────────────────────────┤
 * │  0       │ Idle Task (FreeRTOS only)    │ CPU idle, GC, Power save  │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * ESP-IDF Internal Task Priorities (Reference):
 * - ESP_TASK_WIFI_PRIO         = 23 (WiFi driver)
 * - ESP_TASK_BT_CONTROLLER_PRIO = 23 (Bluetooth controller)
 * - ESP_TASK_TIMER_PRIO        = 22 (Software timers)
 * - ESP_TASK_EVENT_PRIO        = 20 (Event loop)
 * - ESP_TASK_TCPIP_PRIO        = 18 (TCP/IP stack)
 * - ESP_TASK_MAIN_PRIO         = 1  (main() task)
 *
 * Design Considerations:
 * - Same priority tasks share CPU time (round-robin scheduling)
 * - Higher priority tasks preempt lower priority tasks immediately
 * - Blocked tasks (waiting for events/queues) don't consume CPU time
 * - Balance responsiveness vs. CPU fairness when choosing priorities
 */

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


typedef struct {
    int32_t id;
    char name[16];
    int32_t type; //0: kernel, 1: system app, 2: user app
    void* mrb;
} mrb_task_ctx_t;

mrb_task_ctx_t* get_mrb_context(void);
