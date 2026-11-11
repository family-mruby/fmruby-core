// The kanji dictionary's own task (doc/kanji_input/, K2b). See kanji_lookup.h.

#include "kanji_lookup.h"

#include <string.h>

#include "fmrb_attr.h"
#include "fmrb_log.h"
#include "host_task.h"

static const char *TAG = "kanji";

#if defined(FMRB_PLATFORM_WASM)

// The browser converts with its own IME (doc/kanji_input K4): no dictionary,
// no task, and kana input stays as it is.

void fmrb_kanji_lookup_configure(const char *path, bool enabled)
{
    (void)path;
    (void)enabled;
}

void fmrb_kanji_lookup_start(void) {}

bool fmrb_kanji_lookup_ready(void)
{
    return false;
}

uint32_t fmrb_kanji_lookup_request(const char *reading, size_t len)
{
    (void)reading;
    (void)len;
    return 0;
}

const fmrb_kanji_lookup_result_t *fmrb_kanji_lookup_result(void)
{
    return NULL;
}

const char *fmrb_kanji_lookup_candidate(uint16_t index, size_t *len)
{
    (void)index;
    (void)len;
    return NULL;
}

#else

#include "fmrb_hal_file.h"
#include "fmrb_hal_time.h"
#include "fmrb_kanji.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"

#define KANJI_DEFAULT_PATH "/sd/dict/fmrb_kanji.dic"
#define KANJI_PATH_MAX 128

// The answer's room. The most candidates one reading has is 320 (こう in the
// 1 MB dictionary) at a few bytes each; a reading that does not fit keeps the
// candidates that did, in order.
#define KANJI_RESULT_MAX_CANDS 512
#define KANJI_RESULT_TEXT_BYTES (12 * 1024)

typedef struct {
    // Written by the kernel task before the start, read by the task.
    char path[KANJI_PATH_MAX];
    bool enabled;
    // Request: written by the host task, read by the lookup task after the
    // notification.
    char reading[FMRB_KANJI_LOOKUP_READING_MAX];
    uint16_t reading_len;
    uint32_t req_seq;
    // Answer: written by the lookup task, read by the host task after
    // fmrb_host_kanji_result.
    fmrb_kanji_lookup_result_t result;
    volatile bool ready;
    fmrb_task_handle_t task;
} kanji_state_t;

// PSRAM: the internal RAM budget has no room for new statics.
FMRB_EXT_RAM_BSS_ATTR static kanji_state_t s_kj;

// Allocated by the task from the SYSTEM pool (PSRAM on the boards) while the
// dictionary is open.
typedef struct {
    uint8_t block[FMRB_KANJI_BLOCK_SIZE];
    uint16_t offset[KANJI_RESULT_MAX_CANDS + 1];
    char text[KANJI_RESULT_TEXT_BYTES];
} kanji_buffers_t;

FMRB_EXT_RAM_BSS_ATTR static kanji_buffers_t *s_buf;

void fmrb_kanji_lookup_configure(const char *path, bool enabled)
{
    const char *p = (path && path[0]) ? path : KANJI_DEFAULT_PATH;
    strncpy(s_kj.path, p, sizeof(s_kj.path) - 1);
    s_kj.path[sizeof(s_kj.path) - 1] = '\0';
    s_kj.enabled = enabled;
}

bool fmrb_kanji_lookup_ready(void)
{
    return s_kj.ready;
}

const fmrb_kanji_lookup_result_t *fmrb_kanji_lookup_result(void)
{
    return &s_kj.result;
}

const char *fmrb_kanji_lookup_candidate(uint16_t index, size_t *len)
{
    if (!s_buf || index >= s_kj.result.count) return NULL;
    uint16_t a = s_buf->offset[index];
    uint16_t b = s_buf->offset[index + 1];
    if (len) *len = (size_t)(b - a);
    return &s_buf->text[a];
}

uint32_t fmrb_kanji_lookup_request(const char *reading, size_t len)
{
    if (!s_kj.ready || !s_kj.task || len == 0 || len > sizeof(s_kj.reading)) {
        return 0;
    }
    memcpy(s_kj.reading, reading, len);
    s_kj.reading_len = (uint16_t)len;
    s_kj.req_seq++;
    if (s_kj.req_seq == 0) s_kj.req_seq = 1;
    fmrb_task_notify_give(s_kj.task);
    return s_kj.req_seq;
}

// Katakana to hiragana, in place: the dictionary's readings are hiragana, and
// a reading typed in katakana mode means the same word. Both are three-byte
// UTF-8 (U+30A1-U+30F6 -> U+3041-U+3096), so the byte length is kept and the
// matched length still counts the reading as it was typed.
static void fold_katakana(char *s, size_t len)
{
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i + 2 < len; ) {
        if (p[i] < 0x80) { i++; continue; }
        if (p[i] == 0xE3 && (p[i + 1] == 0x82 || p[i + 1] == 0x83)) {
            uint32_t cp = ((uint32_t)(p[i] & 0x0F) << 12) |
                          ((uint32_t)(p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            if (cp >= 0x30A1 && cp <= 0x30F6) {
                cp -= 0x60;
                p[i + 1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                p[i + 2] = (uint8_t)(0x80 | (cp & 0x3F));
            }
            i += 3;
            continue;
        }
        // Skip the rest of any other character.
        i++;
        while (i < len && (p[i] & 0xC0) == 0x80) i++;
    }
}

// Hiragana to katakana, from src into dst (same byte length: both are
// three-byte UTF-8). Anything else (the long vowel mark, punctuation) is
// copied as it is.
static void to_katakana(const char *src, size_t len, char *dst)
{
    const uint8_t *p = (const uint8_t *)src;
    uint8_t *q = (uint8_t *)dst;
    memcpy(q, p, len);
    for (size_t i = 0; i + 2 < len; ) {
        if (p[i] == 0xE3 && (p[i + 1] == 0x81 || p[i + 1] == 0x82)) {
            uint32_t cp = ((uint32_t)(p[i] & 0x0F) << 12) |
                          ((uint32_t)(p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            if (cp >= 0x3041 && cp <= 0x3096) {
                cp += 0x60;
                q[i + 1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                q[i + 2] = (uint8_t)(0x80 | (cp & 0x3F));
            }
            i += 3;
            continue;
        }
        i++;
        while (i < len && (p[i] & 0xC0) == 0x80) i++;
    }
}

static void reverse(char *s, size_t n)
{
    for (size_t i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
}

// The katakana form of the matched part always among the candidates
// (doc/kanji_input K6): second, after the dictionary's first, or first when
// the dictionary has none; not again when the dictionary already has it.
// The dictionary carries no katakana spelled exactly as its reading, so any
// reading can still be made katakana.
static uint16_t add_katakana(const char *reading, uint16_t matched, uint16_t n, uint16_t pos)
{
    char *text = s_buf->text;
    if (n >= KANJI_RESULT_MAX_CANDS || (size_t)pos + matched > sizeof(s_buf->text)) {
        return n;
    }
    to_katakana(reading, matched, &text[pos]);
    for (uint16_t i = 0; i < n; i++) {
        if (s_buf->offset[i + 1] - s_buf->offset[i] == matched &&
            memcmp(&text[s_buf->offset[i]], &text[pos], matched) == 0) {
            return n;
        }
    }
    uint16_t at = (n > 0) ? 1 : 0;
    // Rotate it from the end into place: [offset[at], pos) then itself.
    uint16_t start = s_buf->offset[at];
    reverse(&text[start], (size_t)(pos - start));
    reverse(&text[pos], matched);
    reverse(&text[start], (size_t)(pos + matched - start));
    for (uint16_t i = n + 1; i > at; i--) {
        s_buf->offset[i] = (uint16_t)(s_buf->offset[i - 1] + matched);
    }
    s_buf->offset[at] = start;
    return (uint16_t)(n + 1);
}

static void lookup_one(fmrb_kanji_dict_t *dict)
{
    char reading[FMRB_KANJI_LOOKUP_READING_MAX];
    uint32_t seq = s_kj.req_seq;
    size_t len = s_kj.reading_len;
    memcpy(reading, s_kj.reading, len);
    fold_katakana(reading, len);

    fmrb_kanji_lookup_result_t res = { .seq = seq };
    fmrb_time_t t0 = fmrb_hal_time_get_us();
    uint32_t reads0 = fmrb_kanji_block_reads(dict);
    fmrb_kanji_result_t kr;
    fmrb_err_t err = fmrb_kanji_longest_prefix(dict, reading, len, s_buf->block, &kr);
    uint16_t pos = 0;
    uint16_t n = 0;
    uint16_t matched = (uint16_t)len;   // the whole reading when nothing matched
    if (err == FMRB_OK) {
        matched = kr.matched_bytes;
        // Each candidate straight into the text area; FMRB_KANJI_CAND_UTF8_MAX
        // of room left guarantees the next one fits, with room kept for the
        // katakana form.
        while (n < kr.count && n + 1 < KANJI_RESULT_MAX_CANDS &&
               (size_t)pos + FMRB_KANJI_CAND_UTF8_MAX + matched <= sizeof(s_buf->text)) {
            size_t clen = 0;
            if (fmrb_kanji_candidate(&kr, n, &s_buf->text[pos],
                                     sizeof(s_buf->text) - pos, &clen) != FMRB_OK) {
                break;
            }
            s_buf->offset[n] = pos;
            pos = (uint16_t)(pos + clen);
            n++;
        }
    } else if (err != FMRB_ERR_NOT_FOUND) {
        FMRB_LOGW(TAG, "lookup failed: %d", err);
    }
    s_buf->offset[n] = pos;
    if (err == FMRB_OK || err == FMRB_ERR_NOT_FOUND) {
        n = add_katakana(reading, matched, n, pos);
    }
    res.found = n > 0;
    res.matched_bytes = res.found ? matched : 0;
    res.count = n;
    uint32_t us = (uint32_t)(fmrb_hal_time_get_us() - t0);
    FMRB_LOGI(TAG, "lookup %u bytes: %s %u cands, matched %u, %u block reads, %u us",
              (unsigned)len, res.found ? "found" : "none", (unsigned)res.count,
              (unsigned)res.matched_bytes,
              (unsigned)(fmrb_kanji_block_reads(dict) - reads0), (unsigned)us);
    s_kj.result = res;
    fmrb_host_kanji_result(seq);
}

static void kanji_task(void *arg)
{
    (void)arg;
    s_kj.task = fmrb_task_get_current();
    fmrb_time_t t0 = fmrb_hal_time_get_us();
    fmrb_kanji_dict_t *dict = NULL;
    fmrb_err_t err = fmrb_kanji_open(s_kj.path, &dict);
    if (err == FMRB_OK) {
        s_buf = (kanji_buffers_t *)fmrb_sys_malloc(sizeof(kanji_buffers_t));
        if (!s_buf) {
            err = FMRB_ERR_NO_MEMORY;
            fmrb_kanji_close(dict);
            dict = NULL;
        }
    }
    if (err != FMRB_OK) {
        // Any failure means "no dictionary": kana input stays as it is.
        FMRB_LOGW(TAG, "%s not opened (%d): kanji conversion off", s_kj.path, err);
        s_kj.task = 0;
        fmrb_task_delete(NULL);
        return;
    }
    fmrb_kanji_info_t info;
    fmrb_kanji_get_info(dict, &info);
    FMRB_LOGI(TAG, "%s open in %u us: %u readings, %u blocks, %u bytes held (+%u buffers)",
              s_kj.path, (unsigned)(fmrb_hal_time_get_us() - t0),
              (unsigned)info.reading_count, (unsigned)info.block_count,
              (unsigned)info.table_bytes, (unsigned)sizeof(kanji_buffers_t));
    s_kj.ready = true;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, FMRB_MAX_DELAY);
        lookup_one(dict);
    }
}

void fmrb_kanji_lookup_start(void)
{
    if (!s_kj.path[0]) {
        fmrb_kanji_lookup_configure(NULL, true);
    }
    if (!s_kj.enabled) {
        FMRB_LOGI(TAG, "kanji conversion disabled (kanji_convert = false)");
        return;
    }
    fmrb_file_info_t st;
    if (fmrb_hal_file_stat(s_kj.path, &st) != FMRB_OK) {
        // The usual case: no dictionary on this machine. Nothing is created.
        FMRB_LOGI(TAG, "no dictionary at %s: kanji conversion off", s_kj.path);
        return;
    }
    fmrb_task_handle_t handle = 0;
    if (fmrb_task_create_ex(kanji_task, "fmrb_kanji", FMRB_KANJI_TASK_STACK_SIZE, NULL,
                            FMRB_KANJI_TASK_PRIORITY, &handle,
                            FMRB_KANJI_TASK_FLAGS) != FMRB_PASS) {
        FMRB_LOGW(TAG, "kanji task not created: conversion off");
        return;
    }
    fmrb_mem_log_boot_snapshot("kanji");
}

#endif
