/*
 * apu_if implementation for fmruby-core on ESP32-P4 (M5Stack Tab5).
 *
 * Ported from fmruby-graphics-audio components/apu_emu/src/apu_if.cpp.
 * Differences from the WROVER original:
 *  - No on-chip audio output (no I2S / LEDC-PWM / DAC): the platform audio
 *    driver registers an output writer via apuif_set_output_writer() and
 *    receives mono int16 samples at the APU rate (15720 Hz NTSC).
 *  - The APULOG replay utilities were dropped (legacy demo feature).
 */
#include "apu_if.h"
#include "noftypes.h"
#include "nes_apu.h"
#include "esp_heap_caps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static apu_t* _apu = 0;
static int _audio_frequency = 0;
static int _audio_frame_samples = 0;
static int _audio_fraction = 0;
static int _initialized = 0;
static volatile int _use_external_process = 0;

/* Dual APU instance support */
static apu_t* _apu_sub = NULL;
static int _sub_initialized = 0;
static int _current_instance = APUIF_INSTANCE_MAIN;
static int _sub_audio_fraction = 0;

static apuif_output_writer_t _output_writer = NULL;

void apuif_set_output_writer(apuif_output_writer_t writer)
{
    _output_writer = writer;
}

void apuif_init(void)
{
    if (_initialized) return;

    _audio_frequency = 15720; /* NTSC audio rate */
    _audio_frame_samples = (_audio_frequency << 16) / 60; /* 16.16 fixed point */
    _audio_fraction = 0;

    _apu = apu_create(0, _audio_frequency, 60, 8);

    _initialized = 1;
}

int apuif_frame_sample_count(void)
{
    int n = _audio_frame_samples + _audio_fraction;
    _audio_fraction = n & 0xFFFF;
    return n >> 16;
}

int apuif_process(int16_t* buff, int len)
{
    int n = apuif_frame_sample_count();
    if (n > len) {
        printf("bad buffer size %d > %d\n", n, len);
        return -1;
    }

    apu_process(buff, n);
    uint8_t* b8 = (uint8_t*)buff;
    for (int i = n - 1; i >= 0; i--) {
        buff[i] = (b8[i] ^ 0x80) << 8; /* turn it back into signed 16 */
    }
    return n;
}

void apuif_write_reg(uint32_t address, uint8_t value)
{
    if (_current_instance == APUIF_INSTANCE_SUB && _sub_initialized) {
        apu_setcontext(_apu_sub);
        apu_write(address, value);
        apu_getcontext(_apu_sub);
        apu_setcontext(_apu);
    } else {
        apu_setcontext(_apu);
        apu_write(address, value);
        apu_getcontext(_apu);
    }
}

uint8_t apuif_read_reg(uint32_t address)
{
    if (_current_instance == APUIF_INSTANCE_SUB && _sub_initialized) {
        apu_setcontext(_apu_sub);
        uint8_t val = apu_read(address);
        apu_setcontext(_apu);
        return val;
    }
    return apu_read(address);
}

void apuif_audio_write(const int16_t* s, int len, int channels)
{
    if (_output_writer) {
        _output_writer(s, len, channels);
    }
}

int apuif_use_external_process(void)
{
    return _use_external_process;
}

void apuif_set_external_process(int flag)
{
    _use_external_process = flag;
}

/* --- Dual APU instance support --- */

void apuif_init_sub(void)
{
    if (_sub_initialized) return;
    if (!_initialized) {
        printf("Error: main APU must be initialized before sub\n");
        return;
    }

    _apu_sub = apu_create(0, _audio_frequency, 60, 8);
    apu_getcontext(_apu_sub);

    /* Restore main context */
    apu_setcontext(_apu);

    _sub_audio_fraction = 0;
    _sub_initialized = 1;
}

void apuif_select(int instance)
{
    if (instance >= 0 && instance < APUIF_INSTANCE_MAX) {
        _current_instance = instance;
    }
}

int apuif_process_mix(int16_t* buff, int len)
{
    /* Process main APU */
    apu_setcontext(_apu);
    int n = apuif_frame_sample_count();
    if (n > len) {
        printf("bad buffer size %d > %d\n", n, len);
        return -1;
    }

    int16_t main_buf[528];
    apu_process(main_buf, n);
    uint8_t* b8 = (uint8_t*)main_buf;
    for (int i = n - 1; i >= 0; i--) {
        main_buf[i] = (b8[i] ^ 0x80) << 8;
    }
    apu_getcontext(_apu);

    if (_sub_initialized) {
        /* Process sub APU */
        apu_setcontext(_apu_sub);

        int16_t sub_buf[528];
        apu_process(sub_buf, n);
        uint8_t* b8s = (uint8_t*)sub_buf;
        for (int i = n - 1; i >= 0; i--) {
            sub_buf[i] = (b8s[i] ^ 0x80) << 8;
        }
        apu_getcontext(_apu_sub);

        /* Restore main context */
        apu_setcontext(_apu);

        /* Mix: sum of both channels, clamp to int16 range */
        for (int i = 0; i < n; i++) {
            int32_t mixed = (int32_t)main_buf[i] + (int32_t)sub_buf[i];
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            buff[i] = (int16_t)mixed;
        }
    } else {
        /* No sub APU, just copy main */
        memcpy(buff, main_buf, n * sizeof(int16_t));
    }

    return n;
}

/* Memory allocation proxy - use PSRAM for the large player states */
void *apuemu_malloc(uint32_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void apuemu_free(void *ptr)
{
    free(ptr);
}
