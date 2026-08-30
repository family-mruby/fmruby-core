#include "hw_proxy_internal.h"
#include "fmrb_hal_pin_manager.h"
#include "fmrb_log.h"
#include "driver/i2c_master.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4
// On Modern the I2C1 pins are the board's internal bus, which the display
// driver brings up and owns: creating a second i2c_master bus on it fails
// ("already acquired"), and on Tab5 -- where LovyanGFX drives that controller
// at register level for the touch panel -- raw transactions would corrupt its
// state outright. Mediate all unit-1 traffic through the display driver's
// serialized I2C service instead. The pins come from fmrb_pin_assign.h so this
// follows the board (Tab5 GPIO31/32, NARYA v4 GPIO7/8).
#include "display_p4_task.h"
#include "fmrb_pin_assign.h"
#define HW_PROXY_I2C_MEDIATED_UNIT  1
#define HW_PROXY_I2C_MEDIATED_SDA   FMRB_PIN_I2C1_SDA
#define HW_PROXY_I2C_MEDIATED_SCL   FMRB_PIN_I2C1_SCL
#endif

static const char *TAG = "hw_proxy_i2c";

typedef struct {
    i2c_master_bus_handle_t bus_handle;
    bool initialized;
    bool mediated;   // routed via display_p4 I2C service (Modern unit 1)
    uint32_t frequency;
    int8_t sda;
    int8_t scl;
    hw_proxy_task_handle_t owner;
} hw_proxy_i2c_context_t;

static hw_proxy_i2c_context_t s_i2c[2] = {0};

static void handle_init(hw_proxy_request_t *req)
{
    hw_proxy_i2c_init_params_t *p = (hw_proxy_i2c_init_params_t *)req->params;

    if (p->unit < 0 || p->unit > 1) {
        req->result = FMRB_ERR_INVALID_PARAM;
        return;
    }

    if (s_i2c[p->unit].initialized) {
        // Bus stays alive across release/init cycles to avoid an ESP-IDF
        // i2c_master heap-corruption crash on re-creating the same port (see
        // doc/known_issue). Reuse the existing bus regardless of ownership;
        // claim ownership and re-acquire pins for the new caller.
        if (s_i2c[p->unit].owner == NULL) {
            // Bus was released by previous owner: take over.
            s_i2c[p->unit].owner = req->caller;
            s_i2c[p->unit].sda = p->sda;
            s_i2c[p->unit].scl = p->scl;
            fmrb_pin_manager_acquire(p->sda, FMRB_PIN_USER_I2C, req->caller);
            fmrb_pin_manager_acquire(p->scl, FMRB_PIN_USER_I2C, req->caller);
        } else if (s_i2c[p->unit].owner != req->caller) {
            FMRB_LOGE(TAG, "I2C%d already owned by another task", p->unit);
            req->result = FMRB_ERR_BUSY;
            return;
        }
        s_i2c[p->unit].frequency = p->freq;
        req->result = FMRB_OK;
        return;
    }

#ifdef CONFIG_IDF_TARGET_ESP32P4
    if (p->unit == HW_PROXY_I2C_MEDIATED_UNIT) {
        if (p->sda != HW_PROXY_I2C_MEDIATED_SDA ||
            p->scl != HW_PROXY_I2C_MEDIATED_SCL) {
            FMRB_LOGE(TAG, "I2C%d on Modern is fixed to SDA=%d SCL=%d",
                      p->unit, HW_PROXY_I2C_MEDIATED_SDA,
                      HW_PROXY_I2C_MEDIATED_SCL);
            req->result = FMRB_ERR_INVALID_PARAM;
            return;
        }
        if (!display_p4_is_ready()) {
            FMRB_LOGE(TAG, "I2C%d mediation unavailable (display not ready)",
                      p->unit);
            req->result = FMRB_ERR_BUSY;
            return;
        }
        s_i2c[p->unit].bus_handle = NULL;
        s_i2c[p->unit].mediated = true;
        s_i2c[p->unit].initialized = true;
        s_i2c[p->unit].frequency = p->freq;
        s_i2c[p->unit].sda = p->sda;
        s_i2c[p->unit].scl = p->scl;
        s_i2c[p->unit].owner = req->caller;
        fmrb_pin_manager_acquire(p->sda, FMRB_PIN_USER_I2C, req->caller);
        fmrb_pin_manager_acquire(p->scl, FMRB_PIN_USER_I2C, req->caller);
        FMRB_LOGI(TAG, "I2C%d mediated via display I2C service", p->unit);
        req->result = FMRB_OK;
        return;
    }
#endif

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = p->unit,
        .scl_io_num = p->scl,
        .sda_io_num = p->sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c[p->unit].bus_handle);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "I2C%d bus init failed: %d", p->unit, err);
        req->result = FMRB_ERR_FAILED;
        return;
    }

    s_i2c[p->unit].initialized = true;
    s_i2c[p->unit].frequency = p->freq;
    s_i2c[p->unit].sda = p->sda;
    s_i2c[p->unit].scl = p->scl;
    s_i2c[p->unit].owner = req->caller;

    // Register pins in pin manager
    fmrb_pin_manager_acquire(p->sda, FMRB_PIN_USER_I2C, req->caller);
    fmrb_pin_manager_acquire(p->scl, FMRB_PIN_USER_I2C, req->caller);

    req->result = FMRB_OK;
}

void hw_proxy_i2c_release(hw_proxy_task_handle_t owner)
{
    // Drop ownership and pin reservations only; keep the underlying ESP-IDF
    // bus handle alive (see handle_init for rationale).
    for (int i = 0; i < 2; i++) {
        if (s_i2c[i].initialized && s_i2c[i].owner == owner) {
            FMRB_LOGI(TAG, "Releasing I2C%d ownership (bus kept)", i);
            fmrb_pin_manager_release(s_i2c[i].sda);
            fmrb_pin_manager_release(s_i2c[i].scl);
            s_i2c[i].owner = NULL;
        }
    }
}

static void handle_read(hw_proxy_request_t *req)
{
    hw_proxy_i2c_rw_params_t *p = (hw_proxy_i2c_rw_params_t *)req->params;

    if (p->unit < 0 || p->unit > 1 || !s_i2c[p->unit].initialized) {
        req->result = FMRB_ERR_INVALID_PARAM;
        return;
    }

#ifdef CONFIG_IDF_TARGET_ESP32P4
    if (s_i2c[p->unit].mediated) {
        req->result = display_p4_i2c_read(p->addr, p->buf, p->len,
                                          s_i2c[p->unit].frequency);
        return;
    }
#endif

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = p->addr,
        .scl_speed_hz = s_i2c[p->unit].frequency,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t err = i2c_master_bus_add_device(s_i2c[p->unit].bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        req->result = FMRB_ERR_FAILED;
        return;
    }

    uint32_t timeout_ms = (p->timeout_us + 999) / 1000;
    if (timeout_ms < 10) timeout_ms = 10;

    err = i2c_master_receive(dev_handle, p->buf, p->len, timeout_ms);
    i2c_master_bus_rm_device(dev_handle);

    req->result = (err == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
}

static void handle_release_unit(hw_proxy_request_t *req)
{
    hw_proxy_i2c_release_params_t *p = (hw_proxy_i2c_release_params_t *)req->params;

    if (p->unit < 0 || p->unit > 1 || !s_i2c[p->unit].initialized) {
        req->result = FMRB_ERR_INVALID_PARAM;
        return;
    }

    if (s_i2c[p->unit].owner != req->caller) {
        FMRB_LOGE(TAG, "I2C%d release denied: not owner", p->unit);
        req->result = FMRB_ERR_BUSY;
        return;
    }

    // Keep ESP-IDF bus alive across release/init cycles; only drop ownership
    // and pin reservations (see handle_init for rationale).
    FMRB_LOGI(TAG, "Releasing I2C%d ownership (explicit, bus kept)", p->unit);
    fmrb_pin_manager_release(s_i2c[p->unit].sda);
    fmrb_pin_manager_release(s_i2c[p->unit].scl);
    s_i2c[p->unit].owner = NULL;

    req->result = FMRB_OK;
}

static void handle_write(hw_proxy_request_t *req)
{
    hw_proxy_i2c_rw_params_t *p = (hw_proxy_i2c_rw_params_t *)req->params;

    if (p->unit < 0 || p->unit > 1 || !s_i2c[p->unit].initialized) {
        req->result = FMRB_ERR_INVALID_PARAM;
        return;
    }

#ifdef CONFIG_IDF_TARGET_ESP32P4
    if (s_i2c[p->unit].mediated) {
        req->result = display_p4_i2c_write(p->addr, p->buf, p->len,
                                           s_i2c[p->unit].frequency);
        return;
    }
#endif

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = p->addr,
        .scl_speed_hz = s_i2c[p->unit].frequency,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t err = i2c_master_bus_add_device(s_i2c[p->unit].bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        req->result = FMRB_ERR_FAILED;
        return;
    }

    uint32_t timeout_ms = (p->timeout_us + 999) / 1000;
    if (timeout_ms < 10) timeout_ms = 10;

    err = i2c_master_transmit(dev_handle, p->buf, p->len, timeout_ms);
    i2c_master_bus_rm_device(dev_handle);

    req->result = (err == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
}

void hw_proxy_i2c_execute(hw_proxy_request_t *req)
{
    switch (req->op) {
    case HW_PROXY_OP_I2C_INIT:
        handle_init(req);
        break;
    case HW_PROXY_OP_I2C_READ:
        handle_read(req);
        break;
    case HW_PROXY_OP_I2C_WRITE:
        handle_write(req);
        break;
    case HW_PROXY_OP_I2C_RELEASE:
        handle_release_unit(req);
        break;
    default:
        FMRB_LOGE(TAG, "Unknown I2C op: %d", req->op);
        req->result = FMRB_ERR_INVALID_PARAM;
        break;
    }
}
