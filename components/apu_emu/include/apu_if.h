#ifndef _APU_C_H_
#define _APU_C_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

/*
 * fmruby-core port of the apu_emu interface (ESP32-P4 / Tab5).
 * Unlike the WROVER original, this component performs NO hardware output
 * itself: the platform audio driver registers an output writer callback
 * (apuif_set_output_writer) that receives mono int16 samples at the APU
 * rate (15720 Hz NTSC) and forwards them to the codec / I2S.
 */
typedef void (*apuif_output_writer_t)(const int16_t *samples, int len, int channels);
void apuif_set_output_writer(apuif_output_writer_t writer);

void apuif_init();
int apuif_frame_sample_count();
int apuif_process(int16_t* buff, int len);
void apuif_write_reg(uint32_t address, uint8_t value);
uint8_t apuif_read_reg(uint32_t address);

void apuif_audio_write(const int16_t* s, int len, int channels);
int apuif_use_external_process();
void apuif_set_external_process(int flag);

#ifdef __linux__
/* Read samples from ring buffer (Linux/SDL2 only) */
int apuif_ring_read(int16_t* out, int count);
/* Flush ring buffer to minimize latency */
void apuif_ring_flush(void);
#endif

/*
 * Dual APU instance support for simultaneous playback.
 * Instance 0: default (NSF/main music)
 * Instance 1: secondary (FMSQ/effects)
 *
 * Before writing to an APU instance, call apuif_select(n).
 * apuif_process_mix() processes both instances and mixes the output.
 */
#define APUIF_INSTANCE_MAIN   0
#define APUIF_INSTANCE_SUB    1
#define APUIF_INSTANCE_MAX    2

/* Initialize secondary APU instance */
void apuif_init_sub(void);

/* Select which APU instance receives write_reg calls */
void apuif_select(int instance);

/* Process both APU instances and mix into output buffer.
 * Returns number of samples written. */
int apuif_process_mix(int16_t* buff, int len);

/*
 * Memory allocation proxy for apu_emu component.
 * On ESP32, these map to heap_caps_malloc (PSRAM).
 * On Linux, these map to standard malloc/free.
 * Implemented in apu_if_linux.c / apu_if.cpp respectively.
 */
void *apuemu_malloc(uint32_t size);
void  apuemu_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif //_APU_C_H_

