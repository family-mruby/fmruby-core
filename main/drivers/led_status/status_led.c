#include "status_led.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_pin_assign.h"
#include "fmrb_log.h"

// Stack size, priority, and flags are defined in fmrb_task_config.h
#define HEARTBEAT_TICK_MS       100
#define ERROR_LED_SELFTEST_MS   1000
#define TASK_DUMP_INTERVAL_MS   10000

static const char *TAG = "status_led";

static volatile int s_error_flag = 0;

static void status_led_task(void *pvParameters)
{
    uint32_t dump_counter = 0;

    // Red LED self-test: ON for 1s then OFF
    fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED, 1);
    fmrb_task_delay_ms(ERROR_LED_SELFTEST_MS);
    fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED, 0);

    while (1) {
        // Green LED heartbeat: 1.9s ON, 0.1s OFF
        fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 1);
        for (int i = 0; i < 19; i++) {
            fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED, s_error_flag ? 1 : 0);
            fmrb_task_delay_ms(HEARTBEAT_TICK_MS);
            dump_counter += HEARTBEAT_TICK_MS;
        }
        fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 0);
        fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED, s_error_flag ? 1 : 0);
        fmrb_task_delay_ms(HEARTBEAT_TICK_MS);
        dump_counter += HEARTBEAT_TICK_MS;

        if (dump_counter >= TASK_DUMP_INTERVAL_MS) {
            fmrb_task_dump_status();
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
    FMRB_LOGE(TAG, "Set Error flag");
    s_error_flag = 1;
}

void status_led_clear_error(void)
{
    FMRB_LOGI(TAG, "Clear Error flag");
    s_error_flag = 0;
}
