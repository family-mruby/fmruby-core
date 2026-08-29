/*
 * fmrb_log_port_wasm.c - the wasm logging backend behind fmrb_log_port.h.
 *
 * The ESP_LOG-compatible surface over printf, with one global runtime level
 * (per-tag levels are not kept: esp_log_level_set("*", ...) sets the global
 * level, any other tag is ignored). Moved verbatim from the interim
 * wasm/stub/esp_stub.c (doc/idf_seam/).
 */

#include "fmrb_log_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

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
