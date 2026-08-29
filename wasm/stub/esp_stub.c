/*
 * esp_stub.c - the small IDF surface the wasm build keeps: logging to stdout,
 * the microsecond clock, restart-as-abort, randomness, a fixed MAC.
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ log */

static esp_log_level_t s_level = ESP_LOG_INFO;

void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    (void)tag; /* only the global "*" level is kept */
    s_level = level;
}

esp_log_level_t esp_log_level_get(const char *tag)
{
    (void)tag;
    return s_level;
}

uint32_t esp_log_timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void esp_log_write(esp_log_level_t level, const char *tag,
                   const char *format, ...)
{
    if (level > s_level || level == ESP_LOG_NONE) return;
    static const char marks[] = { '?', 'E', 'W', 'I', 'D', 'V' };
    /* One buffered line per call: tasks log concurrently, and stdio's
     * per-call lock is what keeps lines whole. */
    char line[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    printf("%c (%u) %s: %s\n", marks[level], (unsigned)esp_log_timestamp(),
           tag, line);
    fflush(stdout);
}

/* ---------------------------------------------------------------- misc */

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK:   return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    default:       return "ESP_ERR";
    }
}

void esp_restart(void)
{
    fprintf(stderr, "esp_restart: aborting the wasm module (reload to reboot)\n");
    abort();
}

esp_reset_reason_t esp_reset_reason(void)
{
    return ESP_RST_POWERON;
}

uint32_t esp_get_free_heap_size(void)
{
    return (uint32_t)heap_caps_get_free_size(0);
}

uint32_t esp_get_minimum_free_heap_size(void)
{
    return (uint32_t)heap_caps_get_free_size(0);
}

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

uint32_t esp_random(void)
{
    uint32_t v = 0;
    if (getentropy(&v, sizeof(v)) != 0) v = (uint32_t)rand();
    return v;
}

void esp_fill_random(void *buf, size_t len)
{
    if (getentropy(buf, len) != 0) {
        uint8_t *p = (uint8_t *)buf;
        for (size_t i = 0; i < len; i++) p[i] = (uint8_t)rand();
    }
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    (void)type;
    /* Locally administered, stable: "FM" + wasm */
    static const uint8_t fixed[6] = { 0x02, 0x46, 0x4D, 0x57, 0x41, 0x53 };
    memcpy(mac, fixed, 6);
    return ESP_OK;
}

/* Emscripten's libc ships the <sys/sysinfo.h> header but no implementation;
 * FmrbApp.system_info's linux branch calls it. Answer with the heap budget. */
#include <sys/sysinfo.h>

int sysinfo(struct sysinfo *info)
{
    memset(info, 0, sizeof(*info));
    info->mem_unit = 1;
    info->totalram = 256u * 1024u * 1024u;
    info->freeram = heap_caps_get_free_size(0);
    return 0;
}
