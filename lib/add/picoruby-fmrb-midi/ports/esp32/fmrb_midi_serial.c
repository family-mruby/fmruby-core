/*
 * Serial (UART) MIDI output, shared by the whole firmware.
 *
 * See include/fmrb_midi_serial.h for why this sits in C rather than being a
 * UART handed to Ruby. The MIDI protocol itself is in Ruby; this file opens
 * the port and writes bytes.
 */

#include "fmrb_midi_serial.h"
#include "fmrb_hal_uart.h"
#include "fmrb_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "fmrb_hal_pin_manager.h"
#include "fmrb_pin_assign.h"
#endif

static const char *TAG = "midi_serial";

/* The simulation writes into a FIFO under the project directory, which is
 * bind-mounted into the container, so a tool on the host can read the bytes
 * (tools/fmrb_midi_monitor.rb). */
#define FMRB_MIDI_SERIAL_SIM_PATH "/project/midi_out.fifo"

/* Retro (NARYAv3) brings GROVE port 2 out on the pins the pin assignment
 * header calls I2C2. The unit's yellow wire is its RX, so ours is TX. Which
 * GPIO carries which colour still has to be confirmed on the board; if it is
 * the other way round only these two lines change. */
#ifndef CONFIG_IDF_TARGET_LINUX
#define FMRB_MIDI_SERIAL_UART_NUM 2
#define FMRB_MIDI_SERIAL_TX_PIN   FMRB_PIN_I2C2_SDA
#define FMRB_MIDI_SERIAL_RX_PIN   (-1) /* output only for now */
#endif

static fmrb_uart_handle_t s_uart = NULL;
static SemaphoreHandle_t s_lock = NULL;
static fmrb_midi_serial_config_t s_config;

static bool ensure_lock(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    return s_lock != NULL;
}

void fmrb_midi_serial_default_config(fmrb_midi_serial_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->baud_rate = FMRB_MIDI_SERIAL_BAUD;
    config->rx_pin = -1;
#ifdef CONFIG_IDF_TARGET_LINUX
    config->device_path = FMRB_MIDI_SERIAL_SIM_PATH;
    config->uart_num = -1;
    config->tx_pin = -1;
#else
    config->device_path = NULL;
    config->uart_num = FMRB_MIDI_SERIAL_UART_NUM;
    config->tx_pin = FMRB_MIDI_SERIAL_TX_PIN;
#endif
}

#ifndef CONFIG_IDF_TARGET_LINUX
/* Claim the pins so a later I2C user on the same GROVE port fails loudly
 * instead of the two quietly fighting over the lines. Pin ownership is not
 * simulated on the host build, so this only runs on hardware. */
static fmrb_err_t claim_pins(const fmrb_midi_serial_config_t *config)
{
    if (config->tx_pin < 0) {
        return FMRB_OK;
    }

    fmrb_err_t err = fmrb_pin_manager_acquire(config->tx_pin, FMRB_PIN_USER_UART, NULL);
    if (err != FMRB_OK) {
        FMRB_LOGE(TAG, "TX pin %d is already in use (GROVE port 2 is either I2C or MIDI)",
                  config->tx_pin);
        return err;
    }

    if (config->rx_pin >= 0) {
        err = fmrb_pin_manager_acquire(config->rx_pin, FMRB_PIN_USER_UART, NULL);
        if (err != FMRB_OK) {
            FMRB_LOGE(TAG, "RX pin %d is already in use", config->rx_pin);
            fmrb_pin_manager_release(config->tx_pin);
            return err;
        }
    }
    return FMRB_OK;
}

static void release_pins(const fmrb_midi_serial_config_t *config)
{
    if (config->tx_pin >= 0) {
        fmrb_pin_manager_release(config->tx_pin);
    }
    if (config->rx_pin >= 0) {
        fmrb_pin_manager_release(config->rx_pin);
    }
}
#endif

fmrb_err_t fmrb_midi_serial_open(const fmrb_midi_serial_config_t *config)
{
    if (!ensure_lock()) {
        return FMRB_ERR_NO_MEMORY;
    }

    fmrb_midi_serial_config_t cfg;
    if (config == NULL) {
        fmrb_midi_serial_default_config(&cfg);
    } else {
        cfg = *config;
        if (cfg.baud_rate == 0) {
            cfg.baud_rate = FMRB_MIDI_SERIAL_BAUD;
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    /* Already open: share it. Apps come and go; the port belongs to the
     * system, not to whoever asked first. */
    if (s_uart != NULL) {
        xSemaphoreGive(s_lock);
        return FMRB_OK;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    fmrb_err_t pin_err = claim_pins(&cfg);
    if (pin_err != FMRB_OK) {
        xSemaphoreGive(s_lock);
        return pin_err;
    }
#endif

    fmrb_uart_config_t uart_cfg = {
        .device_path = cfg.device_path,
        .uart_num = cfg.uart_num,
        .tx_pin = cfg.tx_pin,
        .rx_pin = cfg.rx_pin,
        .baud_rate = cfg.baud_rate,
        .timeout_ms = 100,
    };

    fmrb_err_t err = fmrb_hal_uart_open(&uart_cfg, &s_uart);
    if (err != FMRB_OK) {
        s_uart = NULL;
#ifndef CONFIG_IDF_TARGET_LINUX
        release_pins(&cfg);
#endif
        FMRB_LOGW(TAG, "open failed (path=%s uart=%d tx=%d): %d",
                  cfg.device_path ? cfg.device_path : "-", cfg.uart_num, cfg.tx_pin, (int)err);
        xSemaphoreGive(s_lock);
        return err;
    }

    s_config = cfg;
    FMRB_LOGI(TAG, "MIDI serial out ready (path=%s uart=%d tx=%d baud=%lu)",
              cfg.device_path ? cfg.device_path : "-", cfg.uart_num, cfg.tx_pin,
              (unsigned long)cfg.baud_rate);
    xSemaphoreGive(s_lock);
    return FMRB_OK;
}

fmrb_err_t fmrb_midi_serial_close(void)
{
    if (!ensure_lock()) {
        return FMRB_ERR_NO_MEMORY;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_uart != NULL) {
        fmrb_hal_uart_close(s_uart);
        s_uart = NULL;
#ifndef CONFIG_IDF_TARGET_LINUX
        release_pins(&s_config);
#endif
        FMRB_LOGI(TAG, "MIDI serial out closed");
    }
    xSemaphoreGive(s_lock);
    return FMRB_OK;
}

bool fmrb_midi_serial_is_open(void)
{
    return s_uart != NULL;
}

fmrb_err_t fmrb_midi_serial_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (s_uart == NULL || !ensure_lock()) {
        return FMRB_ERR_INVALID_STATE;
    }

    /* One message must not be interleaved with another app's message, or the
     * receiver sees a status byte followed by the wrong data bytes. */
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    size_t written = 0;
    fmrb_err_t err = fmrb_hal_uart_write(s_uart, data, len, &written);
    xSemaphoreGive(s_lock);

    if (err == FMRB_OK && written != len) {
        FMRB_LOGW(TAG, "short write: %u of %u bytes", (unsigned)written, (unsigned)len);
    }
    return err;
}

/* --- Ruby binding -------------------------------------------------------
 *
 * Deliberately tiny: open, close, ask, write bytes. Everything about MIDI
 * itself is in mrblib/fmrb-serial-midi.rb, where it can be tested on the
 * host. The class is not meant to be used directly by apps; they get a
 * MIDI::Device transport that wraps it.
 */

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/class.h>

static mrb_value mrb_serial_open(mrb_state *mrb, mrb_value self)
{
    mrb_value path = mrb_nil_value();
    mrb_int uart_num = -1, tx_pin = -1, rx_pin = -1, baud = 0;
    mrb_get_args(mrb, "|oiiii", &path, &uart_num, &tx_pin, &rx_pin, &baud);

    fmrb_midi_serial_config_t config;
    fmrb_midi_serial_default_config(&config);
    if (mrb_string_p(path)) {
        config.device_path = mrb_str_to_cstr(mrb, path);
    }
    if (uart_num >= 0) {
        config.uart_num = (int)uart_num;
    }
    if (tx_pin >= 0) {
        config.tx_pin = (int)tx_pin;
    }
    if (rx_pin >= -1) {
        config.rx_pin = (int)rx_pin;
    }
    if (baud > 0) {
        config.baud_rate = (uint32_t)baud;
    }

    return mrb_bool_value(fmrb_midi_serial_open(&config) == FMRB_OK);
}

static mrb_value mrb_serial_close(mrb_state *mrb, mrb_value self)
{
    (void)mrb; (void)self;
    fmrb_midi_serial_close();
    return mrb_nil_value();
}

static mrb_value mrb_serial_is_open(mrb_state *mrb, mrb_value self)
{
    (void)mrb; (void)self;
    return mrb_bool_value(fmrb_midi_serial_is_open());
}

static mrb_value mrb_serial_write(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_value bytes;
    mrb_get_args(mrb, "S", &bytes);

    fmrb_err_t err = fmrb_midi_serial_write((const uint8_t *)RSTRING_PTR(bytes),
                                            (size_t)RSTRING_LEN(bytes));
    return mrb_fixnum_value(err == FMRB_OK ? (mrb_int)RSTRING_LEN(bytes) : -1);
}

void mrb_fmrb_midi_serial_init(mrb_state *mrb)
{
    struct RClass *module = mrb_define_module(mrb, "FmrbMidi");
    struct RClass *klass = mrb_define_class_under(mrb, module, "SerialPort", mrb->object_class);

    mrb_define_class_method(mrb, klass, "_open", mrb_serial_open, MRB_ARGS_OPT(5));
    mrb_define_class_method(mrb, klass, "_close", mrb_serial_close, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, klass, "_open?", mrb_serial_is_open, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, klass, "_write", mrb_serial_write, MRB_ARGS_REQ(1));
}
