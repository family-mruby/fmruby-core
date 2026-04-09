#include "fmrb_log_buffer.h"
#include "fmrb_rtos.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/*
 * Ring buffer layout:
 *   [header (16 bytes)] [line data ...]
 *
 * Each line is stored as: length(2 bytes) + text(N bytes)
 * Lines are written sequentially; when the buffer wraps, old lines are overwritten.
 * write_pos is a monotonically increasing counter; actual offset = write_pos % data_size.
 */

#define LOG_HEADER_SIZE 16
#define LOG_LINE_MAX    256

typedef struct {
    uint32_t write_pos;     // Monotonic write position (wraps via modulo)
    uint32_t oldest_pos;    // Position of oldest valid line
    uint32_t data_size;     // Usable data area size
    uint32_t reserved;
} log_buffer_header_t;

static log_buffer_header_t *g_header = NULL;
static uint8_t *g_data = NULL;
static fmrb_semaphore_t g_mutex = NULL;
static volatile char g_buffer_level = 'I';  // Default: collect E/W/I, skip D

static int level_rank(char level)
{
    switch (level) {
    case 'E': return 0;
    case 'W': return 1;
    case 'I': return 2;
    case 'D': return 3;
    default:  return 99;
    }
}

void fmrb_log_buffer_set_level(char level)
{
    g_buffer_level = level;
}

char fmrb_log_buffer_get_level(void)
{
    return g_buffer_level;
}

int fmrb_log_buffer_level_enabled(char level)
{
    return level_rank(level) <= level_rank(g_buffer_level);
}

void fmrb_log_buffer_init(void *buf, size_t size)
{
    if (!buf || size < LOG_HEADER_SIZE + 256) return;

    g_header = (log_buffer_header_t *)buf;
    g_data = (uint8_t *)buf + LOG_HEADER_SIZE;

    g_header->write_pos = 0;
    g_header->oldest_pos = 0;
    g_header->data_size = (uint32_t)(size - LOG_HEADER_SIZE);
    g_header->reserved = 0;

    memset(g_data, 0, g_header->data_size);

    g_mutex = fmrb_semaphore_create_mutex();
}

/* Write raw bytes to ring buffer at position, wrapping as needed */
static void ring_write(uint32_t pos, const void *src, size_t len)
{
    uint32_t ds = g_header->data_size;
    uint32_t offset = pos % ds;
    const uint8_t *s = (const uint8_t *)src;

    if (offset + len <= ds) {
        memcpy(g_data + offset, s, len);
    } else {
        size_t first = ds - offset;
        memcpy(g_data + offset, s, first);
        memcpy(g_data, s + first, len - first);
    }
}

/* Read raw bytes from ring buffer at position */
static void ring_read(uint32_t pos, void *dst, size_t len)
{
    uint32_t ds = g_header->data_size;
    uint32_t offset = pos % ds;
    uint8_t *d = (uint8_t *)dst;

    if (offset + len <= ds) {
        memcpy(d, g_data + offset, len);
    } else {
        size_t first = ds - offset;
        memcpy(d, g_data + offset, first);
        memcpy(d + first, g_data, len - first);
    }
}

void fmrb_log_buffer_printf(const char *tag, char level, const char *format, ...)
{
    if (!g_header || !g_data) return;

    char line[LOG_LINE_MAX];
    int prefix_len = snprintf(line, sizeof(line), "[%c][%s] ", level, tag ? tag : "?");
    if (prefix_len < 0) return;

    va_list args;
    va_start(args, format);
    int msg_len = vsnprintf(line + prefix_len, sizeof(line) - prefix_len, format, args);
    va_end(args);

    if (msg_len < 0) return;

    int total_len = prefix_len + msg_len;
    if (total_len >= (int)sizeof(line)) total_len = sizeof(line) - 1;
    line[total_len] = '\0';

    /* Entry format: uint16_t length + text bytes */
    uint16_t entry_len = (uint16_t)total_len;
    size_t entry_size = sizeof(uint16_t) + entry_len;

    if (entry_size > g_header->data_size / 2) return;  // Sanity check

    if (!g_mutex) return;
    fmrb_semaphore_take(g_mutex, FMRB_MAX_DELAY);

    uint32_t wp = g_header->write_pos;

    /* Advance oldest_pos if we would overwrite old data */
    while (g_header->oldest_pos < wp &&
           (wp + entry_size - g_header->oldest_pos) > g_header->data_size) {
        /* Skip over the oldest entry */
        uint16_t old_len;
        ring_read(g_header->oldest_pos, &old_len, sizeof(uint16_t));
        g_header->oldest_pos += sizeof(uint16_t) + old_len;
    }

    /* Write entry */
    ring_write(wp, &entry_len, sizeof(uint16_t));
    ring_write(wp + sizeof(uint16_t), line, entry_len);
    g_header->write_pos = wp + entry_size;

    fmrb_semaphore_give(g_mutex);
}

int fmrb_log_buffer_read_lines(char *out_buf, size_t out_size, int max_lines, uint32_t *read_pos)
{
    if (!g_header || !g_data || !out_buf || out_size == 0) return 0;
    if (!g_mutex) return 0;

    fmrb_semaphore_take(g_mutex, FMRB_MAX_DELAY);

    uint32_t rp = read_pos ? *read_pos : g_header->oldest_pos;
    uint32_t wp = g_header->write_pos;

    /* Clamp read position to valid range */
    if (rp < g_header->oldest_pos) rp = g_header->oldest_pos;
    if (rp > wp) rp = wp;

    int lines = 0;
    size_t out_pos = 0;

    while (rp < wp && lines < max_lines) {
        uint16_t entry_len;
        ring_read(rp, &entry_len, sizeof(uint16_t));

        if (entry_len == 0 || entry_len >= LOG_LINE_MAX) {
            /* Corrupted entry, skip to end */
            rp = wp;
            break;
        }

        /* Check if line fits in output buffer (+ newline + null) */
        if (out_pos + entry_len + 2 > out_size) break;

        ring_read(rp + sizeof(uint16_t), out_buf + out_pos, entry_len);
        out_pos += entry_len;
        out_buf[out_pos++] = '\n';

        rp += sizeof(uint16_t) + entry_len;
        lines++;
    }

    out_buf[out_pos] = '\0';

    if (read_pos) *read_pos = rp;

    fmrb_semaphore_give(g_mutex);

    return lines;
}

uint32_t fmrb_log_buffer_get_write_pos(void)
{
    if (!g_header) return 0;
    return g_header->write_pos;
}
