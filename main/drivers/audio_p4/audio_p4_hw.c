// Tab5 audio hardware: I2S master + ES8388 codec via esp_codec_dev.
//
// Configuration mirrors the M5Stack Tab5 BSP (M5Tab5-UserDemo
// components/m5stack_tab5): I2S pins MCLK=30 BCLK=27 WS=29 DOUT=26,
// ES8388 as I2S slave in DAC mode, speaker amp enabled via PI4IO #1 P1
// (already driven high by tab5_power_on in display_p4_task).
//
// The codec control interface attaches to the existing I2C bus on
// GPIO31/32 (port 1) owned by LovyanGFX; the bus handle is obtained
// with i2c_master_get_bus_handle(). esp_codec_dev (i2c_master driver)
// is used only inside the init window, before GT911 polling starts;
// after that the i2c_master path is unusable on this controller, so
// runtime volume changes write the ES8388 DAC volume registers
// directly through the display driver's I2C service (lgfx path,
// mutex-serialized). See doc/tab5_i2c_bus_notes.md.
//
// Output format: 47160 Hz (= 3 x 15720 Hz NTSC APU rate) 16-bit stereo.
// The APU produces mono 15720 Hz; audio_p4_hw_write() expands each
// sample 3x to both channels, so no fractional resampling is needed.

#include "audio_p4_internal.h"

#include "fmrb_log.h"
#include "display_p4_task.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "audio_p4";

#define AUDIO_P4_I2C_PORT      1        // shared bus (GPIO31/32)
#define AUDIO_P4_I2S_PORT      I2S_NUM_1
#define AUDIO_P4_PIN_MCLK      GPIO_NUM_30
#define AUDIO_P4_PIN_BCLK      GPIO_NUM_27
#define AUDIO_P4_PIN_WS        GPIO_NUM_29
#define AUDIO_P4_PIN_DOUT      GPIO_NUM_26

#define AUDIO_P4_UPSAMPLE      3
#define AUDIO_P4_SAMPLE_RATE   (15720 * AUDIO_P4_UPSAMPLE)
#define AUDIO_P4_DEFAULT_VOL   70       // esp_codec_dev volume 0-100

// Max APU frame is ~263 mono samples; x3 upsample, x2 channels
#define AUDIO_P4_STEREO_BUF_SAMPLES (264 * AUDIO_P4_UPSAMPLE * 2)

static i2s_chan_handle_t s_tx_chan = NULL;
static esp_codec_dev_handle_t s_codec = NULL;
static volatile bool s_hw_ready = false;
static int16_t s_stereo_buf[AUDIO_P4_STEREO_BUF_SAMPLES];

bool audio_p4_hw_ready(void) {
    return s_hw_ready;
}

fmrb_err_t audio_p4_hw_init(void) {
    if (s_hw_ready) return FMRB_OK;

    // I2S TX channel (master), standard Philips mode
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_P4_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "i2s_new_channel failed: %d", err);
        return FMRB_ERR_FAILED;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_P4_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_P4_PIN_MCLK,
            .bclk = AUDIO_P4_PIN_BCLK,
            .ws   = AUDIO_P4_PIN_WS,
            .dout = AUDIO_P4_PIN_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "i2s std init failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "i2s enable failed: %d", err);
        return FMRB_ERR_FAILED;
    }

    // Codec control on the shared I2C bus. LovyanGFX normally creates
    // the port-1 bus during LGFX init (GT911); reuse its handle. If it
    // does not exist yet, create it with the same configuration -
    // LovyanGFX tolerates the existing bus since it drives the
    // controller at register level.
    i2c_master_bus_handle_t bus = NULL;
    err = i2c_master_get_bus_handle(AUDIO_P4_I2C_PORT, &bus);
    if (err != ESP_OK || !bus) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = AUDIO_P4_I2C_PORT,
            .sda_io_num = GPIO_NUM_31,
            .scl_io_num = GPIO_NUM_32,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = { .enable_internal_pullup = true },
        };
        err = i2c_new_master_bus(&bus_cfg, &bus);
        if (err != ESP_OK || !bus) {
            FMRB_LOGE(TAG, "I2C bus %d unavailable: %d", AUDIO_P4_I2C_PORT, err);
            return FMRB_ERR_FAILED;
        }
        FMRB_LOGI(TAG, "I2C bus %d created for codec", AUDIO_P4_I2C_PORT);
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = AUDIO_P4_I2C_PORT,
        .addr = ES8388_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        FMRB_LOGE(TAG, "codec i2c ctrl create failed");
        return FMRB_ERR_FAILED;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = AUDIO_P4_I2S_PORT,
        .rx_handle = NULL,
        .tx_handle = s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        FMRB_LOGE(TAG, "codec i2s data create failed");
        return FMRB_ERR_FAILED;
    }

    es8388_codec_cfg_t es_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,
        .ctrl_if = ctrl_if,
        .pa_pin = -1,  // speaker amp is driven by PI4IO #1 P1 (tab5_power_on)
    };
    const audio_codec_if_t *codec_if = es8388_codec_new(&es_cfg);
    if (!codec_if) {
        FMRB_LOGE(TAG, "es8388 codec create failed");
        return FMRB_ERR_FAILED;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) {
        FMRB_LOGE(TAG, "esp_codec_dev_new failed");
        return FMRB_ERR_FAILED;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_P4_SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_codec, &fs) != ESP_CODEC_DEV_OK) {
        FMRB_LOGE(TAG, "codec open failed");
        return FMRB_ERR_FAILED;
    }
    esp_codec_dev_set_out_vol(s_codec, AUDIO_P4_DEFAULT_VOL);

    // Headphone detect / speaker amp gating lives in the display driver
    // (display_p4_poll_headphone): the PI4IO shares the I2C controller
    // with the GT911, which LovyanGFX drives at register level, so all
    // runtime accesses must go through lgfx's I2C path from the touch
    // task context. Using i2c_master here breaks the controller state.

    FMRB_LOGI(TAG, "Tab5 audio ready: ES8388 %d Hz 16-bit stereo (APU %d Hz x%d)",
              AUDIO_P4_SAMPLE_RATE, 15720, AUDIO_P4_UPSAMPLE);
    s_hw_ready = true;
    return FMRB_OK;
}

// APU output writer: mono 15720 Hz -> stereo 47160 Hz (repeat x3).
// Called from the audio task at 60 Hz; esp_codec_dev_write blocks on
// I2S DMA and paces the caller.
void audio_p4_hw_write(const int16_t *samples, int len, int channels) {
    if (!s_hw_ready || !samples || len <= 0) return;
    if (channels != 1) return;  // APU path is always mono

    if (len * AUDIO_P4_UPSAMPLE * 2 > AUDIO_P4_STEREO_BUF_SAMPLES) {
        len = AUDIO_P4_STEREO_BUF_SAMPLES / (AUDIO_P4_UPSAMPLE * 2);
    }

    int16_t *out = s_stereo_buf;
    for (int i = 0; i < len; i++) {
        int16_t s = samples[i];
        for (int r = 0; r < AUDIO_P4_UPSAMPLE; r++) {
            *out++ = s;  // L
            *out++ = s;  // R
        }
    }

    size_t bytes = (size_t)len * AUDIO_P4_UPSAMPLE * 2 * sizeof(int16_t);
    int ret = esp_codec_dev_write(s_codec, s_stereo_buf, bytes);
    if (ret != ESP_CODEC_DEV_OK) {
        FMRB_LOGW(TAG, "codec write failed: %d", ret);
    }
}

// ES8388 DAC digital volume (LDACVOL/RDACVOL): 0.5 dB attenuation steps,
// 0x00 = 0 dB, 0xC0 = -96 dB. esp_codec_dev's default curve maps volume
// 0-100 linearly to -50..0 dB, i.e. reg = 100 - vol; keep that mapping so
// runtime changes sound identical to the boot default, but treat 0 as a
// full mute (-96 dB).
#define ES8388_I2C_ADDR_7BIT  (ES8388_CODEC_DEFAULT_ADDR >> 1)
#define ES8388_REG_LDACVOL    0x1A  // ES8388_DACCONTROL4
#define ES8388_REG_RDACVOL    0x1B  // ES8388_DACCONTROL5
#define ES8388_I2C_FREQ       400000

void audio_p4_hw_set_volume(uint8_t volume_0_255) {
    if (!s_hw_ready) return;
    int vol = (volume_0_255 * 100) / 255;
    // esp_codec_dev talks through i2c_master, which is broken on this
    // controller once touch polling runs; write the codec registers
    // through the display driver's serialized lgfx I2C service instead.
    uint8_t reg = (vol == 0) ? 0xC0 : (uint8_t)(100 - vol);
    fmrb_err_t err = display_p4_i2c_write_reg8(ES8388_I2C_ADDR_7BIT,
                                               ES8388_REG_RDACVOL, reg,
                                               ES8388_I2C_FREQ);
    if (err == FMRB_OK) {
        err = display_p4_i2c_write_reg8(ES8388_I2C_ADDR_7BIT,
                                        ES8388_REG_LDACVOL, reg,
                                        ES8388_I2C_FREQ);
    }
    if (err != FMRB_OK) {
        FMRB_LOGW(TAG, "volume set failed: %d", err);
        return;
    }
    FMRB_LOGI(TAG, "volume set to %d/100 (dacvol=0x%02X)", vol, reg);
}
