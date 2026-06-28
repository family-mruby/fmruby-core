// Family mruby Modern (ESP32-P4 / M5Stack Tab5) display task.
//
// Acts as the local transport receiver for the kernel's host_task:
// reads msgpack-encoded GFX/CONTROL commands from the hal_link_local
// TX buffer, dispatches them, and sends ACK responses via the RX buffer.
//
// Phase 1: LGFX panel bring-up + CONTROL/INIT_DISPLAY ACK (allows kernel boot).
//          GFX commands receive ACK but are not yet rendered.
// Phase 2 (future): full GFX command rendering via g_lcd (LGFX_Tab5).

#include "display_p4_task.h"
#include "lgfx_tab5.hpp"

#include "fmrb_log.h"
#include "fmrb_hal_link.h"
#include "fmrb_link_protocol.h"
#include "fmrb_link_types.h"
#include "fmrb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include <msgpack.h>

static const char *TAG = "display_p4";

// Tab5 board pins / I2C (see fmrb_pin_assign.h; kept local to the driver).
#define TAB5_I2C_PORT   I2C_NUM_1
#define TAB5_I2C_SDA    GPIO_NUM_31
#define TAB5_I2C_SCL    GPIO_NUM_32
#define TAB5_TP_INT     GPIO_NUM_23
#define PI4IO1_ADDR     0x43   // PI4IO GPIO expander #1 (LCD/touch reset)
#define PI4IO2_ADDR     0x44   // PI4IO GPIO expander #2 (power rails)

static LGFX_Tab5 g_lcd;

// Receive buffer for local link commands
#define DISPLAY_P4_RECV_BUF_SIZE 4096
static uint8_t g_recv_buf[DISPLAY_P4_RECV_BUF_SIZE];

// GfxBlock program ID allocator (Phase 1: no actual VM, just track IDs)
#define DISPLAY_P4_MAX_PROGS 16
static uint8_t g_next_prog_id = 0;

static inline void pi4io_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

// Power/reset sequence for the Tab5 panel + touch, mirroring M5GFX's Tab5
// bring-up. Uses a temporary IDF i2c_master bus on port 1 that is deleted
// before LovyanGFX initializes its own I2C (port 1) for the ST7123 touch, so
// the two drivers never own the peripheral at the same time.
static void tab5_power_on(void)
{
    // TP INT high during reset selects ST7123 touch I2C address (0x55).
    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = 1ULL << TAB5_TP_INT;
    int_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&int_cfg);
    gpio_set_level(TAB5_TP_INT, 1);

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = TAB5_I2C_PORT;
    bus_cfg.sda_io_num = TAB5_I2C_SDA;
    bus_cfg.scl_io_num = TAB5_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        FMRB_LOGE(TAG, "PI4IO i2c bus init failed");
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t io1 = NULL, io2 = NULL;
    dev_cfg.device_address = PI4IO1_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io1);
    dev_cfg.device_address = PI4IO2_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io2);

    // PI4IO #1: directions/pulls, resets driven LOW (OUT_SET bit4=LCD, bit5=touch)
    pi4io_write(io1, 0x03, 0x7F);
    pi4io_write(io1, 0x05, 0x46);
    pi4io_write(io1, 0x07, 0x00);
    pi4io_write(io1, 0x0D, 0x7F);
    pi4io_write(io1, 0x0B, 0x7F);
    // PI4IO #2: power rails / IO config
    pi4io_write(io2, 0x03, 0xB9);
    pi4io_write(io2, 0x07, 0x06);
    pi4io_write(io2, 0x0D, 0xB9);
    pi4io_write(io2, 0x0B, 0xF9);
    pi4io_write(io2, 0x09, 0x40);
    pi4io_write(io2, 0x11, 0xBF);
    pi4io_write(io2, 0x05, 0x89);

    vTaskDelay(pdMS_TO_TICKS(10));
    // PI4IO #1: release LCD + touch resets (OUT_SET resets HIGH)
    pi4io_write(io1, 0x05, 0x76);

    // Hand the INT pin back as input for the touch controller.
    gpio_set_direction(TAB5_TP_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    i2c_master_bus_rm_device(io1);
    i2c_master_bus_rm_device(io2);
    i2c_del_master_bus(bus);
    // Backlight is driven by LEDC PWM via Light_PWM in LGFX_Tab5 (GPIO22, ch7, 44100Hz).
    // LovyanGFX calls Light_PWM::init() during g_lcd.init(), so no manual GPIO setup needed.
}

// ============================================================
// ACK response sender (display_p4 -> kernel via RX buffer)
// Mirrors m5gfx_task's send_ack().
// ============================================================

static void send_ack(uint8_t msg_type, uint8_t seq, const uint8_t *data, size_t data_len)
{
    // Build ACK as msgpack: [type, seq, RESPONSE_MSG_ACK, payload]
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, msg_type);
    msgpack_pack_uint8(&pk, seq);
    msgpack_pack_uint8(&pk, FMRB_LINK_RESPONSE_MSG_ACK);

    if (data && data_len > 0) {
        msgpack_pack_bin(&pk, data_len);
        msgpack_pack_bin_body(&pk, data, data_len);
    } else {
        msgpack_pack_nil(&pk);
    }

    fmrb_link_message_t resp = {
        .data = (uint8_t *)sbuf.data,
        .size = sbuf.size,
    };
    fmrb_hal_link_local_send_response(FMRB_LINK_CHANNEL_DEFAULT, &resp, 1000);

    msgpack_sbuffer_destroy(&sbuf);
}

// ============================================================
// Message dispatcher
// msgpack format: [type(u8), seq(u8), sub_cmd(u8), payload(bin|nil)]
// ============================================================

static void process_message(const uint8_t *msgpack_data, size_t msgpack_len)
{
    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);

    msgpack_unpack_return ret = msgpack_unpack_next(
        &msg, (const char *)msgpack_data, msgpack_len, NULL);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        FMRB_LOGE(TAG, "msgpack unpack failed");
        msgpack_unpacked_destroy(&msg);
        return;
    }

    msgpack_object root = msg.data;
    if (root.type != MSGPACK_OBJECT_ARRAY || root.via.array.size < 4) {
        FMRB_LOGE(TAG, "Invalid msgpack format");
        msgpack_unpacked_destroy(&msg);
        return;
    }

    uint8_t type    = (uint8_t)root.via.array.ptr[0].via.u64;
    uint8_t seq     = (uint8_t)root.via.array.ptr[1].via.u64;
    uint8_t sub_cmd = (uint8_t)root.via.array.ptr[2].via.u64;
    uint8_t base_type = type & FMRB_LINK_TYPE_MASK;
    bool ack_required = (type & FMRB_LINK_FLAG_ACK_REQUIRED) != 0;

    const uint8_t *payload = nullptr;
    size_t payload_len = 0;
    if (root.via.array.ptr[3].type == MSGPACK_OBJECT_BIN) {
        payload     = (const uint8_t *)root.via.array.ptr[3].via.bin.ptr;
        payload_len = root.via.array.ptr[3].via.bin.size;
    }

    switch (base_type) {
    case FMRB_LINK_TYPE_CONTROL:
        if (sub_cmd == FMRB_LINK_CONTROL_VERSION &&
            payload_len >= sizeof(fmrb_control_version_req_t)) {
            uint8_t local_ver = FMRB_LINK_VERSION;
            FMRB_LOGI(TAG, "VERSION check: remote=%u local=%u seq=%u",
                      payload[0], local_ver, seq);
            send_ack(type, seq, &local_ver, sizeof(local_ver));
        } else if (sub_cmd == FMRB_LINK_CONTROL_INIT_DISPLAY &&
                   payload_len >= sizeof(fmrb_control_init_display_t)) {
            const auto *init_cmd = (const fmrb_control_init_display_t *)payload;
            FMRB_LOGI(TAG, "INIT_DISPLAY: %dx%d %d-bit margin=%d,%d",
                      init_cmd->width, init_cmd->height, init_cmd->color_depth,
                      init_cmd->margin_x, init_cmd->margin_y);
            send_ack(type, seq, nullptr, 0);
        } else if (sub_cmd == FMRB_LINK_CONTROL_GA_VERSION) {
            fmrb_control_ga_version_resp_t resp = {};
            strncpy(resp.version, FMRB_GA_VERSION, sizeof(resp.version) - 1);
            FMRB_LOGI(TAG, "GA_VERSION: responding with \"%s\"", resp.version);
            send_ack(type, seq, (const uint8_t *)&resp, sizeof(resp));
        } else {
            if (ack_required) {
                send_ack(type, seq, nullptr, 0);
            }
        }
        break;

    case FMRB_LINK_TYPE_GRAPHICS:
        // Phase 1: rendering is not yet implemented (Phase 2).
        // Sync commands that return data require specific payloads in the ACK.
        if (sub_cmd == FMRB_LINK_GFX_DEFINE_PROG) {
            // DEFINE_PROG is sync; ACK must carry a uint8_t prog_id.
            // Return a sequential ID; INVALID (0xFF) would abort the caller.
            uint8_t prog_id = g_next_prog_id;
            g_next_prog_id = (g_next_prog_id + 1) % DISPLAY_P4_MAX_PROGS;
            FMRB_LOGD(TAG, "DEFINE_PROG: assigned prog_id=%u", prog_id);
            send_ack(type, seq, &prog_id, sizeof(prog_id));
        } else if (ack_required) {
            send_ack(type, seq, nullptr, 0);
        }
        break;

    default:
        if (ack_required) {
            send_ack(type, seq, nullptr, 0);
        }
        break;
    }

    msgpack_unpacked_destroy(&msg);
}

// ============================================================
// Main task
// ============================================================

static void display_p4_task(void *arg)
{
    (void)arg;
    FMRB_LOGI(TAG, "Tab5 display: power on");
    tab5_power_on();

    FMRB_LOGI(TAG, "Tab5 display: LGFX init");
    if (!g_lcd.init()) {
        FMRB_LOGE(TAG, "LGFX init failed");
    } else {
        FMRB_LOGI(TAG, "LGFX init OK (%dx%d)", g_lcd.width(), g_lcd.height());
        // Draw a boot indicator: dark blue background with white text.
        g_lcd.fillScreen(g_lcd.color888(0, 0, 64));
        g_lcd.setTextColor(g_lcd.color888(255, 255, 255));
        g_lcd.setTextSize(4);
        g_lcd.setCursor(20, 20);
        g_lcd.print("Family mruby");
        g_lcd.setCursor(20, 80);
        g_lcd.print("Modern (P4)");
        g_lcd.setTextSize(2);
        g_lcd.setCursor(20, 160);
        g_lcd.print("Initializing...");
    }

    FMRB_LOGI(TAG, "Tab5 display: entering command receive loop");

    while (1) {
        fmrb_link_message_t msg = {
            .data = g_recv_buf,
            .size = sizeof(g_recv_buf),
        };
        fmrb_err_t err = fmrb_hal_link_local_receive_cmd(
            FMRB_LINK_CHANNEL_DEFAULT, &msg, 100);
        if (err == FMRB_OK && msg.size > 0) {
            process_message(g_recv_buf, msg.size);
        }
    }
}

extern "C" fmrb_err_t display_p4_task_init(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(display_p4_task, "display_p4",
                                            8192, NULL, 5, NULL, 1);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create display_p4 task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

extern "C" fmrb_err_t display_p4_task_deinit(void)
{
    return FMRB_OK;
}
