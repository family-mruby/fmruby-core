#include "status_led.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_pin_assign.h"
#include "fmrb_log.h"

#define STATUS_LED_STACK_SIZE 1024
#define STATUS_LED_PRIORITY   2
#define BLINK_INTERVAL_MS   250

static const char *TAG = "status_led";

static volatile int s_error_flag = 0;

static void status_led_task(void *pvParameters)
{
    int led_state = 1;
    fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 1);

    while (1) {
        if (s_error_flag) {
            // Error mode: blink at BLINK_INTERVAL_MS interval
            led_state = !led_state;
            fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, led_state);
            fmrb_task_delay_ms(BLINK_INTERVAL_MS);
        } else {
            fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 1);
            for (int i = 0; i < 19 && !s_error_flag; i++) {
                fmrb_task_delay_ms(100);
            }
            if (!s_error_flag) {
                fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 0);
                fmrb_task_delay_ms(100);
            }
        }
    }
}

void status_led_start(void)
{
    fmrb_task_handle_t handle;
    fmrb_task_create(status_led_task, "status_led",
                     STATUS_LED_STACK_SIZE, NULL,
                     STATUS_LED_PRIORITY, &handle);
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
