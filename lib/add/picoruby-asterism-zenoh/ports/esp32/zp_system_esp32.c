/*
 * zenoh-pico system layer for ESP-IDF, as used by picoruby-asterism-zenoh.
 *
 * This is zenoh-pico's own ESP-IDF port (src/system/espidf/system.c: random,
 * clock, time, sleep) compiled as it is, with one change: its allocator.
 * Upstream z_malloc takes MALLOC_CAP_8BIT memory, which ESP-IDF serves from
 * internal RAM first. Here every zenoh-pico allocation (session state, the
 * rx/tx batches, fragment reassembly, received values) comes from external
 * RAM (PSRAM): the session is not latency-critical, and internal RAM is the
 * scarce one on PSRAM boards. There is no fallback to internal RAM; zenoh-pico
 * reports a failed allocation as out of memory.
 *
 * The upstream file is included with its three allocator functions renamed
 * (they are left unused), so the rest of it follows zenoh-pico updates.
 * Compiled by the ESP-IDF component (it needs the ESP-IDF headers); the
 * zenoh-pico core is compiled by the mruby build.
 */
#include "zenoh-pico/config.h"

#if defined(ZENOH_ESPIDF)

#include <esp_heap_caps.h>
/* The upstream file gets these through zenoh-pico's espidf.h, which this gem
 * replaces with include/zenoh_espidf_platform.h (no ESP-IDF headers). */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define z_malloc zp_espidf_upstream_malloc
#define z_realloc zp_espidf_upstream_realloc
#define z_free zp_espidf_upstream_free
#include "system/espidf/system.c"
#undef z_malloc
#undef z_realloc
#undef z_free

#define ZP_HEAP_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void *z_malloc(size_t size) { return heap_caps_malloc(size, ZP_HEAP_CAPS); }

void *z_realloc(void *ptr, size_t size) { return heap_caps_realloc(ptr, size, ZP_HEAP_CAPS); }

void z_free(void *ptr) { heap_caps_free(ptr); }

#endif /* defined(ZENOH_ESPIDF) */
