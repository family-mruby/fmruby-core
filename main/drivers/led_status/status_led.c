#include "status_led.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_pin_assign.h"
#include "fmrb_log.h"
#include "fmrb_debug.h"
#include "fmrb_app.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_freertos_hooks.h"
#endif

// Stack size, priority, and flags are defined in fmrb_task_config.h
#define HEARTBEAT_TICK_MS       100
#define BOOT_BLINK_HALF_MS      150
#define ERROR_LED_SELFTEST_MS   1000
#define TASK_DUMP_INTERVAL_MS   10000

// VERSION_MISMATCH pattern: [on 100, off 100] x 3 + off 1000 = 1600ms cycle (16 ticks)
#define VERSION_MISMATCH_CYCLE_TICKS  16
#define VERSION_MISMATCH_PULSE_TICKS  6  // 3 on/off pairs

// Heartbeat cycle: 20 ticks of HEARTBEAT_TICK_MS = 2s, in both patterns. The
// cycle length is fixed so the red error patterns keep their cadence whichever
// green pattern is running.
#define HEARTBEAT_CYCLE_TICKS   20
#define HEARTBEAT_ON_TICKS      19  // idle: 1.9s on, 0.1s off
#define SATURATED_HALF_TICKS    5   // busy: 0.5s on, 0.5s off, twice

static const char *TAG = "status_led";

static volatile int s_error_pattern = FMRB_LED_STATUS_NONE;
static volatile int s_boot_complete = 0;

// Liveness of the idle task on this core, sampled once per heartbeat cycle.
// The counter only advances when the idle task gets to run, so a guest that
// never yields freezes it -- which is what turns the heartbeat into the
// saturated pattern. Together the three states are: normal heartbeat = alive
// with CPU to spare, saturated = alive but no idle time, no blink at all =
// the system itself is wedged.
static volatile uint32_t s_idle_ticks = 0;
static bool s_idle_hook_ready = false;

#ifndef CONFIG_IDF_TARGET_LINUX
// Returning true asks to be called once per tick rather than spun on, which is
// all the resolution a 2s sample needs. Must not block (IDF idle hook rule).
static bool idle_tick_hook(void)
{
    s_idle_ticks++;
    return true;
}
#endif

// Register on the core the LED task runs on: that is the core the guests are
// pinned to, so it is the one whose saturation the LED should report.
static void idle_hook_register(void)
{
#ifndef CONFIG_IDF_TARGET_LINUX
    if (esp_register_freertos_idle_hook(idle_tick_hook) == ESP_OK) {
        s_idle_hook_ready = true;
    } else {
        FMRB_LOGW(TAG, "Idle hook full; CPU saturation will not be shown");
    }
#else
    // The Linux simulation has no esp_system idle hook, so the heartbeat stays
    // on its normal pattern there.
    s_idle_hook_ready = false;
#endif
}

// Green level for one tick of the heartbeat cycle.
static int compute_green_level(bool saturated, uint32_t tick)
{
    if (saturated) {
        return ((tick / SATURATED_HALF_TICKS) % 2 == 0) ? 1 : 0;
    }
    return (tick < HEARTBEAT_ON_TICKS) ? 1 : 0;
}

static int compute_red_level(int pattern, uint32_t step)
{
    switch (pattern) {
        case FMRB_LED_STATUS_FATAL:
            return 1;
        case FMRB_LED_STATUS_VERSION_MISMATCH: {
            uint32_t s = step % VERSION_MISMATCH_CYCLE_TICKS;
            if (s < VERSION_MISMATCH_PULSE_TICKS) {
                return (s % 2 == 0) ? 1 : 0;
            }
            return 0;
        }
        case FMRB_LED_STATUS_NONE:
        default:
            return 0;
    }
}

static void status_led_task(void *pvParameters)
{
    uint32_t dump_counter = 0;
    uint32_t red_step = 0;
    uint32_t last_idle_ticks = 0;

    idle_hook_register();

    // Red LED self-test: ON for 1s then OFF
    fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED, 1);
    fmrb_task_delay_ms(ERROR_LED_SELFTEST_MS);
    fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED, 0);

    while (1) {
        if (!s_boot_complete) {
            // Boot phase: symmetric fast blink so the user sees the system is
            // alive while INIT_DISPLAY and the desktop boot animation run.
            fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 1);
            fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED,
                                    compute_red_level(s_error_pattern, red_step++));
            fmrb_task_delay_ms(BOOT_BLINK_HALF_MS);
            dump_counter += BOOT_BLINK_HALF_MS;

            fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 0);
            fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED,
                                    compute_red_level(s_error_pattern, red_step++));
            fmrb_task_delay_ms(BOOT_BLINK_HALF_MS);
            dump_counter += BOOT_BLINK_HALF_MS;
        } else {
            // One 2s cycle. The idle counter is sampled once here, so the
            // pattern for the whole cycle reflects the CPU state at its start.
            const uint32_t idle_now = s_idle_ticks;
            const bool saturated = s_idle_hook_ready && (idle_now == last_idle_ticks);
            last_idle_ticks = idle_now;

            for (uint32_t t = 0; t < HEARTBEAT_CYCLE_TICKS; t++) {
                fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED,
                                        compute_green_level(saturated, t));
                fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED,
                                        compute_red_level(s_error_pattern, red_step++));
                fmrb_task_delay_ms(HEARTBEAT_TICK_MS);
                dump_counter += HEARTBEAT_TICK_MS;
            }
        }

        if (dump_counter >= TASK_DUMP_INTERVAL_MS) {
            // Skip the dump entirely when debug mode is off: the dump walks
            // each task's stack via uxTaskGetStackHighWaterMark and the heap
            // free list twice, so the measurement itself is the dominant cost.
            if (fmrb_debug_mode_enabled()) {
                fmrb_task_dump_status();
                /* Task stacks and the system heap say nothing about the
                   per-VM mempools, which is where a Spinel instance actually
                   runs out (sp_oom_die aborts the firmware). */
                fmrb_app_dump_vm_pools();
            }
            dump_counter = 0;
        }
    }
}

void status_led_start(void)
{
    fmrb_task_handle_t handle;
    fmrb_task_create_ex(status_led_task, "status_led",
                     FMRB_STATUS_LED_TASK_STACK_SIZE, NULL,
                     FMRB_STATUS_LED_TASK_PRIORITY, &handle,
                     FMRB_STATUS_LED_TASK_FLAGS);
}

void status_led_set_error(int level)
{
    FMRB_LOGE(TAG, "Set Error pattern=%d", level);
    s_error_pattern = level;
}

void status_led_clear_error(void)
{
    FMRB_LOGI(TAG, "Clear Error pattern");
    s_error_pattern = FMRB_LED_STATUS_NONE;
}

void status_led_set_boot_complete(void)
{
    if (!s_boot_complete) {
        FMRB_LOGI(TAG, "Boot complete: switching to heartbeat");
    }
    s_boot_complete = 1;
}
