#include "hw_proxy_internal.h"
#include "fmrb_hal_pin_manager.h"
#include "fmrb_log.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "hw_proxy_rmt";

#define RMT_RESOLUTION_HZ       (10000000)
#define RMT_MEM_BLOCK_SYMBOLS   (64)
#define RMT_TRANS_QUEUE_DEPTH   (4)

static rmt_channel_handle_t s_rmt_channel = NULL;
static rmt_encoder_handle_t s_rmt_encoder = NULL;
static hw_proxy_task_handle_t s_rmt_owner = NULL;
static int s_rmt_gpio = -1;

typedef struct {
    uint32_t t0h_ns;
    uint32_t t0l_ns;
    uint32_t t1h_ns;
    uint32_t t1l_ns;
    uint32_t reset_ns;
} rmt_symbol_config_t;

static rmt_symbol_config_t s_symbol_config;

static size_t encoder_callback(const void *data, size_t data_size,
    size_t symbols_written, size_t symbols_free,
    rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    (void)arg;

    if (symbols_free < 8) {
        return 0;
    }

    const rmt_symbol_word_t sym_zero = {
        .level0 = 1,
        .duration0 = (uint16_t)(((uint64_t)s_symbol_config.t0h_ns * RMT_RESOLUTION_HZ) / 1000000000),
        .level1 = 0,
        .duration1 = (uint16_t)(((uint64_t)s_symbol_config.t0l_ns * RMT_RESOLUTION_HZ) / 1000000000),
    };
    const rmt_symbol_word_t sym_one = {
        .level0 = 1,
        .duration0 = (uint16_t)(((uint64_t)s_symbol_config.t1h_ns * RMT_RESOLUTION_HZ) / 1000000000),
        .level1 = 0,
        .duration1 = (uint16_t)(((uint64_t)s_symbol_config.t1l_ns * RMT_RESOLUTION_HZ) / 1000000000),
    };
    const rmt_symbol_word_t sym_reset = {
        .level0 = 0,
        .duration0 = (uint16_t)(((uint64_t)s_symbol_config.reset_ns * RMT_RESOLUTION_HZ) / 1000000000),
        .level1 = 0,
        .duration1 = (uint16_t)(((uint64_t)s_symbol_config.reset_ns * RMT_RESOLUTION_HZ) / 1000000000),
    };

    size_t data_pos = symbols_written / 8;
    uint8_t *data_bytes = (uint8_t *)data;
    if (data_pos < data_size) {
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            if (data_bytes[data_pos] & bitmask) {
                symbols[symbol_pos++] = sym_one;
            } else {
                symbols[symbol_pos++] = sym_zero;
            }
        }
        return symbol_pos;
    } else {
        symbols[0] = sym_reset;
        *done = 1;
        return 1;
    }
}

static void release_rmt_resources(void)
{
    if (s_rmt_gpio >= 0) {
        fmrb_pin_manager_release(s_rmt_gpio);
        s_rmt_gpio = -1;
    }
    if (s_rmt_channel) {
        rmt_disable(s_rmt_channel);
        rmt_del_channel(s_rmt_channel);
        s_rmt_channel = NULL;
    }
    if (s_rmt_encoder) {
        rmt_del_encoder(s_rmt_encoder);
        s_rmt_encoder = NULL;
    }
    s_rmt_owner = NULL;
}

static void handle_init(hw_proxy_request_t *req)
{
    hw_proxy_rmt_init_params_t *p = (hw_proxy_rmt_init_params_t *)req->params;

    if (s_rmt_channel != NULL) {
        if (s_rmt_owner != req->caller) {
            FMRB_LOGE(TAG, "RMT already owned by another task");
            req->result = FMRB_ERR_BUSY;
            return;
        }
        // Same owner re-init: release old resources first
        release_rmt_resources();
    }

    s_symbol_config.t0h_ns = p->t0h_ns;
    s_symbol_config.t0l_ns = p->t0l_ns;
    s_symbol_config.t1h_ns = p->t1h_ns;
    s_symbol_config.t1l_ns = p->t1l_ns;
    s_symbol_config.reset_ns = p->reset_ns;

    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = p->gpio,
        .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = RMT_TRANS_QUEUE_DEPTH,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_cfg, &s_rmt_channel);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "RMT channel create failed: %d", ret);
        req->result = FMRB_ERR_FAILED;
        return;
    }

    const rmt_simple_encoder_config_t enc_cfg = {
        .callback = encoder_callback
    };
    ret = rmt_new_simple_encoder(&enc_cfg, &s_rmt_encoder);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "RMT encoder create failed: %d", ret);
        req->result = FMRB_ERR_FAILED;
        return;
    }

    ret = rmt_enable(s_rmt_channel);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "RMT enable failed: %d", ret);
        req->result = FMRB_ERR_FAILED;
        return;
    }

    s_rmt_owner = req->caller;
    s_rmt_gpio = (int)p->gpio;
    fmrb_pin_manager_acquire(s_rmt_gpio, FMRB_PIN_USER_RMT, req->caller);
    req->result = FMRB_OK;
}

void hw_proxy_rmt_release(hw_proxy_task_handle_t owner)
{
    if (s_rmt_channel != NULL && s_rmt_owner == owner) {
        FMRB_LOGI(TAG, "Releasing RMT resources");
        release_rmt_resources();
    }
}

static void handle_write(hw_proxy_request_t *req)
{
    hw_proxy_rmt_write_params_t *p = (hw_proxy_rmt_write_params_t *)req->params;

    if (s_rmt_channel == NULL) {
        req->result = FMRB_ERR_INVALID_STATE;
        return;
    }

    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    esp_err_t ret = rmt_transmit(s_rmt_channel, s_rmt_encoder, p->buffer, p->nbytes, &tx_config);
    if (ret != ESP_OK) {
        req->result = FMRB_ERR_FAILED;
        return;
    }

    ret = rmt_tx_wait_all_done(s_rmt_channel, portMAX_DELAY);
    req->result = (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
}

void hw_proxy_rmt_execute(hw_proxy_request_t *req)
{
    switch (req->op) {
    case HW_PROXY_OP_RMT_INIT:
        handle_init(req);
        break;
    case HW_PROXY_OP_RMT_WRITE:
        handle_write(req);
        break;
    default:
        FMRB_LOGE(TAG, "Unknown RMT op: %d", req->op);
        req->result = FMRB_ERR_INVALID_PARAM;
        break;
    }
}
