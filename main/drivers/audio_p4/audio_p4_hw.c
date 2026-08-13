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
//
// Input (doc/mic_spectrum): the ES7210 microphone codec sits on the SAME
// I2S clocks and on the same I2C bus, with its data on GPIO28. So the
// microphone is the RX half of this port, not a port of its own, and it
// samples at the speaker's rate -- the board's wiring decides that, not
// this file. Its control registers are written through the display
// driver's I2C service for the same reason the volume is.

#include "audio_p4_internal.h"

#include <math.h>

#include "fmrb_fft_bench.h"
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
// Microphone data in. The ES7210 hangs off the same MCLK/BCLK/WS the ES8388
// does -- one I2S bus, two codecs -- so the RX channel is the other half of
// this port rather than a port of its own, and it samples at the speaker's
// rate. Anything else would need those clocks routed to a second controller.
#define AUDIO_P4_PIN_DIN       GPIO_NUM_28

#define AUDIO_P4_UPSAMPLE      3
#define AUDIO_P4_SAMPLE_RATE   (15720 * AUDIO_P4_UPSAMPLE)
#define AUDIO_P4_DEFAULT_VOL   70       // esp_codec_dev volume 0-100

// Max APU frame is ~263 mono samples; x3 upsample, x2 channels
#define AUDIO_P4_STEREO_BUF_SAMPLES (264 * AUDIO_P4_UPSAMPLE * 2)

static i2s_chan_handle_t s_tx_chan = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;
// The configuration both directions share, kept so the RX half can be set up
// later with exactly the same bytes the TX half was (see audio_p4_hw_init).
static i2s_std_config_t s_std_cfg;
static bool s_rx_ready = false;
static esp_codec_dev_handle_t s_codec = NULL;
static volatile bool s_hw_ready = false;
static volatile bool s_mic_on = false;
static int16_t s_stereo_buf[AUDIO_P4_STEREO_BUF_SAMPLES];

// Stereo scratch for the microphone: the codec sends two channels and the
// callers want one, so the de-interleave needs somewhere to land. 256 frames
// is one i2s_channel_read of about 5 ms at this rate.
#define AUDIO_P4_MIC_FRAMES 256
static int16_t s_mic_buf[AUDIO_P4_MIC_FRAMES * 2];

bool audio_p4_hw_ready(void) {
    return s_hw_ready;
}

fmrb_err_t audio_p4_hw_init(void) {
    if (s_hw_ready) return FMRB_OK;

    // I2S TX + RX channels (master), standard Philips mode. Full duplex is not
    // a preference here but the wiring: both codecs sit on the one set of
    // clocks, and the two directions of a port can only be allocated together.
    // If the RX half cannot be had, the speaker still gets its channel and the
    // microphone reports itself unavailable.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_P4_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "i2s full duplex unavailable (%d); speaker only", err);
        s_rx_chan = NULL;
        err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    }
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "i2s_new_channel failed: %d", err);
        return FMRB_ERR_FAILED;
    }

    // ONE config for both directions, data in included. The driver only routes
    // the pin that belongs to a channel's direction, so naming both here is
    // harmless -- and it is required: i2s_std.c decides the two channels are a
    // full-duplex pair by memcmp of their whole configuration, gpio_cfg
    // included. Give the RX half its own config (dout unused, din set) and the
    // compare fails, the pair is never constituted, and the driver then rebinds
    // MCLK to the RX clock -- which is stopped while the microphone is off. The
    // speaker loses its master clock and the boot sound goes with it. That is
    // not a hypothetical: it is what shipped for one flash.
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_P4_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_P4_PIN_MCLK,
            .bclk = AUDIO_P4_PIN_BCLK,
            .ws   = AUDIO_P4_PIN_WS,
            .dout = AUDIO_P4_PIN_DOUT,
            .din  = s_rx_chan ? AUDIO_P4_PIN_DIN : I2S_GPIO_UNUSED,
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

    // The RX half is NOT configured here. Allocating the channel is free --
    // i2s_new_channel only reserves it -- but initialising it writes clock and
    // GPIO registers on a port whose transmitter is already running, and the
    // codec is being woken a few lines further down. Booting is the one moment
    // this hardware makes a sound of its own, and the sound changed when the RX
    // half was set up here. So the setup waits for the first mic_enable
    // (mic_init_rx below), and a boot that never touches the microphone does
    // exactly what it did before there was one.
    s_std_cfg = std_cfg;

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

// ============================================================
// Microphone: ES7210 (two mics with echo cancellation; only the two analog
// channels are used here).
//
// Control goes through the display driver's serialized I2C path for the same
// reason the volume above does: once the touch task polls GT911, the
// i2c_master driver cannot share this controller. That rules out
// esp_codec_dev's es7210 driver, which is an i2c_master client -- so the
// registers are written directly. The sequence is the one the board's own
// reference software uses, which is what makes it trustworthy on hardware
// that has never been exercised here before.
//
// The sample rate is the speaker's (47160 Hz): one bus, one clock. At a
// 512-point transform that is 92 Hz per bin, which is coarse for speech and
// perfectly adequate for a bar display.
// ============================================================

#define ES7210_I2C_ADDR_7BIT  0x40
#define ES7210_I2C_FREQ       400000

// reg, value. Reset, clocks on, ADC format and oversampling, high-pass
// filters, mic bias and gain for MIC1/MIC2, MIC3/MIC4 powered down.
static const uint8_t s_es7210_on[][2] = {
    { 0x00, 0x41 },  // RESET_CTL
    { 0x01, 0x1F },  // CLK_ON_OFF
    { 0x06, 0x00 },  // DIGITAL_PDN
    { 0x07, 0x20 },  // ADC_OSR
    { 0x08, 0x10 },  // MODE_CFG (I2S slave)
    { 0x09, 0x30 },  // TCT0_CHPINI
    { 0x0A, 0x30 },  // TCT1_CHPINI
    { 0x20, 0x0A },  // ADC34_HPF2
    { 0x21, 0x2A },  // ADC34_HPF1
    { 0x22, 0x0A },  // ADC12_HPF2
    { 0x23, 0x2A },  // ADC12_HPF1
    { 0x02, 0xC1 },
    { 0x04, 0x01 },
    { 0x05, 0x00 },
    { 0x11, 0x60 },
    { 0x40, 0x42 },  // ANALOG_SYS
    { 0x41, 0x70 },  // MICBIAS12
    { 0x42, 0x70 },  // MICBIAS34
    { 0x43, 0x1B },  // MIC1_GAIN
    { 0x44, 0x1B },  // MIC2_GAIN
    { 0x45, 0x00 },  // MIC3_GAIN
    { 0x46, 0x00 },  // MIC4_GAIN
    { 0x47, 0x00 },  // MIC1_LP
    { 0x48, 0x00 },  // MIC2_LP
    { 0x49, 0x00 },  // MIC3_LP
    { 0x4A, 0x00 },  // MIC4_LP
    { 0x4B, 0x00 },  // MIC12_PDN (on)
    { 0x4C, 0xFF },  // MIC34_PDN (off)
    { 0x01, 0x14 },  // CLK_ON_OFF
};

static const uint8_t s_es7210_off[][2] = {
    { 0x4B, 0xFF },  // MIC12_PDN
    { 0x4C, 0xFF },  // MIC34_PDN
    { 0x06, 0x07 },  // DIGITAL_PDN
    { 0x01, 0x7F },  // CLK_ON_OFF: clocks off
};

static fmrb_err_t es7210_write_all(const uint8_t (*regs)[2], size_t count)
{
    for (size_t i = 0; i < count; i++) {
        fmrb_err_t err = display_p4_i2c_write_reg8(ES7210_I2C_ADDR_7BIT,
                                                   regs[i][0], regs[i][1],
                                                   ES7210_I2C_FREQ);
        if (err != FMRB_OK) {
            FMRB_LOGE(TAG, "ES7210 write reg 0x%02X failed", regs[i][0]);
            return err;
        }
    }
    return FMRB_OK;
}

bool audio_p4_mic_available(void) {
    return s_hw_ready && s_rx_chan != NULL;
}

// Set up the RX half on first use, from the config the TX half was given.
// Identical bytes is not a nicety: i2s_std.c decides the two channels are a
// full-duplex pair by memcmp of the whole configuration, and a pair that fails
// to constitute gets its MCLK rebound to the (stopped) RX clock -- which takes
// the speaker's master clock with it.
static bool mic_init_rx(void) {
    if (s_rx_ready) return true;
    if (!s_rx_chan) return false;

    esp_err_t err = i2s_channel_init_std_mode(s_rx_chan, &s_std_cfg);
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "i2s rx init failed (%d); microphone unavailable", err);
        return false;
    }
    // pair_chan is only set when the driver accepted the pair, so this is the
    // assertion for the paragraph above.
    i2s_chan_info_t info;
    if (i2s_channel_get_info(s_tx_chan, &info) == ESP_OK) {
        FMRB_LOGI(TAG, "i2s full duplex constituted: %s",
                  info.pair_chan ? "yes" : "NO (speaker clock at risk)");
    }
    s_rx_ready = true;
    return true;
}

int audio_p4_mic_sample_rate(void) {
    return AUDIO_P4_SAMPLE_RATE;
}

fmrb_err_t audio_p4_mic_enable(bool on) {
    if (!audio_p4_mic_available()) return FMRB_ERR_INVALID_STATE;
    if (on == s_mic_on) return FMRB_OK;

    if (on) {
        if (!mic_init_rx()) return FMRB_ERR_FAILED;
        // Reset first (0x00 = 0xFF), the way the codec expects to be woken.
        fmrb_err_t err = display_p4_i2c_write_reg8(ES7210_I2C_ADDR_7BIT, 0x00, 0xFF,
                                                   ES7210_I2C_FREQ);
        if (err != FMRB_OK) {
            FMRB_LOGE(TAG, "ES7210 not answering on the shared bus");
            return err;
        }
        err = es7210_write_all(s_es7210_on,
                               sizeof(s_es7210_on) / sizeof(s_es7210_on[0]));
        if (err != FMRB_OK) return err;

        esp_err_t rc = i2s_channel_enable(s_rx_chan);
        if (rc != ESP_OK) {
            FMRB_LOGE(TAG, "i2s rx enable failed: %d", rc);
            return FMRB_ERR_FAILED;
        }
        s_mic_on = true;
        FMRB_LOGI(TAG, "microphone on: ES7210 %d Hz 16-bit stereo", AUDIO_P4_SAMPLE_RATE);
    } else {
        i2s_channel_disable(s_rx_chan);
        s_mic_on = false;
        es7210_write_all(s_es7210_off,
                         sizeof(s_es7210_off) / sizeof(s_es7210_off[0]));
        FMRB_LOGI(TAG, "microphone off");
    }
    return FMRB_OK;
}

int audio_p4_mic_read(int16_t *dst, int max_samples, int timeout_ms) {
    if (!s_mic_on || !dst || max_samples <= 0) return -1;

    int frames = max_samples < AUDIO_P4_MIC_FRAMES ? max_samples : AUDIO_P4_MIC_FRAMES;
    size_t got = 0;
    esp_err_t rc = i2s_channel_read(s_rx_chan, s_mic_buf,
                                    (size_t)frames * 2 * sizeof(int16_t),
                                    &got, timeout_ms);
    if (rc == ESP_ERR_TIMEOUT) return 0;
    if (rc != ESP_OK) {
        FMRB_LOGW(TAG, "i2s read failed: %d", rc);
        return -1;
    }

    // Left channel only: the two microphones are a stereo pair on the board,
    // and one of them is what a spectrum wants.
    int n = (int)(got / (2 * sizeof(int16_t)));
    for (int i = 0; i < n; i++) {
        dst[i] = s_mic_buf[i * 2];
    }
    return n;
}

// The loudest frequency the microphone is hearing right now, in Hz, or -1 if
// nothing could be read. Captures one 512-point window and transforms it with
// the C FFT (the same one the four-engine comparison baselines on), so the
// answer to "are these samples real" can be a frequency rather than a level.
int audio_p4_mic_peak_hz(void) {
    if (!s_mic_on) return -1;

    static int16_t window[512];
    static int16_t mag[256];
    int have = 0;
    while (have < 512) {
        int n = audio_p4_mic_read(window + have, 512 - have, 200);
        if (n <= 0) return -1;
        have += n;
    }

    if (fmrb_fft_c(window, 512, 1, mag) == 0) return -1;

    // Skip bin 0: any DC offset in the analog path lands there and would
    // always win.
    int best = 1;
    for (int i = 2; i < 256; i++) {
        if (mag[i] > mag[best]) best = i;
    }
    return (int)((int64_t)best * AUDIO_P4_SAMPLE_RATE / 512);
}

void audio_p4_mic_selftest(void) {
    if (!audio_p4_mic_available()) {
        FMRB_LOGW(TAG, "mic selftest: no RX channel, skipping");
        return;
    }
    bool was_on = s_mic_on;
    if (audio_p4_mic_enable(true) != FMRB_OK) {
        FMRB_LOGE(TAG, "mic selftest: could not enable the microphone");
        return;
    }

    // The first blocks after power-up are the codec settling; read a few and
    // report each, so a flat zero (no data) reads differently from a quiet
    // room (small but moving numbers).
    static int16_t mono[AUDIO_P4_MIC_FRAMES];
    for (int block = 0; block < 8; block++) {
        int n = audio_p4_mic_read(mono, AUDIO_P4_MIC_FRAMES, 200);
        if (n <= 0) {
            FMRB_LOGW(TAG, "mic selftest block %d: no samples (%d)", block, n);
            continue;
        }
        int64_t sum = 0;
        int peak = 0;
        for (int i = 0; i < n; i++) {
            int v = mono[i];
            sum += (int64_t)v * v;
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
        int rms = (int)sqrtf((float)((double)sum / (double)n));
        FMRB_LOGI(TAG, "mic selftest block %d: n=%d rms=%d peak=%d", block, n, rms, peak);
    }

    if (!was_on) audio_p4_mic_enable(false);
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
