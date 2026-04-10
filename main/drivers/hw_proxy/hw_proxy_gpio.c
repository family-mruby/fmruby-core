#include "hw_proxy_internal.h"
#include "fmrb_hal_gpio.h"
#include "fmrb_hal_pin_manager.h"
#include "fmrb_log.h"
#include "driver/gpio.h"

static const char *TAG = "hw_proxy_gpio";

void hw_proxy_gpio_execute(hw_proxy_request_t *req)
{
    switch (req->op) {
    case HW_PROXY_OP_GPIO_RESET: {
        hw_proxy_gpio_reset_params_t *p = (hw_proxy_gpio_reset_params_t *)req->params;
        fmrb_err_t acq = fmrb_pin_manager_acquire(p->pin, FMRB_PIN_USER_GPIO, req->caller);
        if (acq != FMRB_OK) {
            req->result = acq;
            break;
        }
        esp_err_t ret = gpio_reset_pin(p->pin);
        req->result = (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
        break;
    }
    case HW_PROXY_OP_GPIO_CONFIG: {
        hw_proxy_gpio_config_params_t *p = (hw_proxy_gpio_config_params_t *)req->params;
        gpio_config_t config = {0};
        config.pin_bit_mask = (1ULL << p->pin);

        switch (p->mode) {
        case FMRB_GPIO_MODE_INPUT:
            config.mode = GPIO_MODE_INPUT;
            break;
        case FMRB_GPIO_MODE_OUTPUT:
            config.mode = GPIO_MODE_OUTPUT;
            break;
        case FMRB_GPIO_MODE_OUTPUT_OD:
            config.mode = GPIO_MODE_OUTPUT_OD;
            break;
        default:
            req->result = FMRB_ERR_INVALID_PARAM;
            return;
        }

        switch (p->pull) {
        case FMRB_GPIO_PULL_NONE:
            config.pull_up_en = GPIO_PULLUP_DISABLE;
            config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case FMRB_GPIO_PULL_UP:
            config.pull_up_en = GPIO_PULLUP_ENABLE;
            config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case FMRB_GPIO_PULL_DOWN:
            config.pull_up_en = GPIO_PULLUP_DISABLE;
            config.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        default:
            req->result = FMRB_ERR_INVALID_PARAM;
            return;
        }

        esp_err_t ret = gpio_config(&config);
        req->result = (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
        break;
    }
    case HW_PROXY_OP_GPIO_READ: {
        hw_proxy_gpio_read_params_t *p = (hw_proxy_gpio_read_params_t *)req->params;
        *p->out_level = gpio_get_level(p->pin);
        req->result = FMRB_OK;
        break;
    }
    case HW_PROXY_OP_GPIO_WRITE: {
        hw_proxy_gpio_write_params_t *p = (hw_proxy_gpio_write_params_t *)req->params;
        esp_err_t ret = gpio_set_level(p->pin, p->level);
        req->result = (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
        break;
    }
    case HW_PROXY_OP_GPIO_PULL: {
        hw_proxy_gpio_pull_params_t *p = (hw_proxy_gpio_pull_params_t *)req->params;
        gpio_pull_mode_t esp_pull;
        switch (p->pull) {
        case FMRB_GPIO_PULL_NONE:
            esp_pull = GPIO_FLOATING;
            break;
        case FMRB_GPIO_PULL_UP:
            esp_pull = GPIO_PULLUP_ONLY;
            break;
        case FMRB_GPIO_PULL_DOWN:
            esp_pull = GPIO_PULLDOWN_ONLY;
            break;
        default:
            req->result = FMRB_ERR_INVALID_PARAM;
            return;
        }
        esp_err_t ret = gpio_set_pull_mode(p->pin, esp_pull);
        req->result = (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
        break;
    }
    default:
        FMRB_LOGE(TAG, "Unknown GPIO op: %d", req->op);
        req->result = FMRB_ERR_INVALID_PARAM;
        break;
    }
}
