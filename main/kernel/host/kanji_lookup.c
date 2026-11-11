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
#include "fmrb_kanji_cands.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"

_Static_assert(FMRB_KANJI_NO_INDEX == FMRB_KANJI_LOOKUP_NO_WHOLE,
               "the two no-whole markers are the same value");

#define KANJI_DEFAULT_PATH "/sd/dict/fmrb_kanji.dic"
#define KANJI_PATH_MAX 128
// The resolved path is longer on the hosted builds (the flash directory's
// own path in front).
#define KANJI_REAL_PATH_MAX 256

// The answer's room. The most candidates one reading has is 320 (こう in the
// 1 MB dictionary) at a few bytes each; a reading that does not fit keeps the
// candidates that did, in order.
#define KANJI_RESULT_MAX_CANDS 512
#define KANJI_RESULT_TEXT_BYTES (12 * 1024)

typedef struct {
    // Written by the kernel task before the start, read by the task.
    char path[KANJI_PATH_MAX];
    // The same path as the file HAL resolves it, for the change hook.
    char real_path[KANJI_REAL_PATH_MAX];
    bool enabled;
    // Request: written by the host task, read by the lookup task after the
    // notification.
    char reading[FMRB_KANJI_LOOKUP_READING_MAX];
    uint16_t reading_len;
    uint32_t req_seq;
    // The last request answered (the task's own).
    uint32_t done_seq;
    // Answer: written by the lookup task, read by the host task after
    // fmrb_host_kanji_result.
    fmrb_kanji_lookup_result_t result;
    volatile bool ready;
    // The file's changes (fmrb_hal_file_set_change_hook). `held` is set by the
    // task that changes the file, from the start of its change to the end;
    // `reopen` asks the task to look at the file again; `may_hold` is the
    // task's own answer to "is the file open", true from before the open
    // until after the close.
    volatile bool held;
    volatile bool reopen;
    volatile bool may_hold;
    volatile bool creating;
    fmrb_task_handle_t task;
} kanji_state_t;

// PSRAM: the internal RAM budget has no room for new statics.
FMRB_EXT_RAM_BSS_ATTR static kanji_state_t s_kj;

// Allocated by the task from the SYSTEM pool (PSRAM on the boards) the first
// time the dictionary opens, and kept: the host task may still be showing
// candidates from it when the file is closed for a change.
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

// Answer the pending request; without a dictionary (closed for a change in
// the meantime) the answer is "none", so the host task keeps the kana.
static void lookup_one(fmrb_kanji_dict_t *dict)
{
    char reading[FMRB_KANJI_LOOKUP_READING_MAX];
    uint32_t seq = s_kj.req_seq;
    size_t len = s_kj.reading_len;
    s_kj.done_seq = seq;
    if (dict == NULL || s_buf == NULL) {
        fmrb_kanji_lookup_result_t none = {
            .seq = seq, .found = false, .whole_index = FMRB_KANJI_LOOKUP_NO_WHOLE,
        };
        s_kj.result = none;
        FMRB_LOGI(TAG, "lookup %u bytes: no dictionary open", (unsigned)len);
        fmrb_host_kanji_result(seq);
        return;
    }
    memcpy(reading, s_kj.reading, len);
    fmrb_time_t t0 = fmrb_hal_time_get_us();
    uint32_t reads0 = fmrb_kanji_block_reads(dict);
    const fmrb_kanji_cand_buf_t buf = {
        .text = s_buf->text,
        .text_size = sizeof(s_buf->text),
        .offset = s_buf->offset,
        .max_cands = KANJI_RESULT_MAX_CANDS,
    };
    fmrb_kanji_cands_t made;
    fmrb_err_t err = fmrb_kanji_make_candidates(dict, reading, len, s_buf->block, &buf, &made);
    if (err != FMRB_OK && err != FMRB_ERR_NOT_FOUND) {
        FMRB_LOGW(TAG, "lookup failed: %d", err);
    }
    fmrb_kanji_lookup_result_t res = {
        .seq = seq,
        .found = made.found,
        .matched_bytes = made.matched_bytes,
        .count = made.count,
        .whole_index = made.whole_index,
    };
    uint32_t us = (uint32_t)(fmrb_hal_time_get_us() - t0);
    FMRB_LOGI(TAG, "lookup %u bytes: %s %u cands, matched %u, whole %d, %u block reads, %u us",
              (unsigned)len, res.found ? "found" : "none", (unsigned)res.count,
              (unsigned)res.matched_bytes,
              res.whole_index == FMRB_KANJI_LOOKUP_NO_WHOLE ? -1 : (int)res.whole_index,
              (unsigned)(fmrb_kanji_block_reads(dict) - reads0), (unsigned)us);
    s_kj.result = res;
    fmrb_host_kanji_result(seq);
}

// Open the dictionary (at the start, and after each change to the file).
static fmrb_kanji_dict_t *open_dict(void)
{
    fmrb_time_t t0 = fmrb_hal_time_get_us();
    fmrb_kanji_dict_t *dict = NULL;
    fmrb_err_t err = fmrb_kanji_open(s_kj.path, &dict);
    if (err == FMRB_OK && !s_buf) {
        s_buf = (kanji_buffers_t *)fmrb_sys_malloc(sizeof(kanji_buffers_t));
        if (!s_buf) {
            err = FMRB_ERR_NO_MEMORY;
            fmrb_kanji_close(dict);
            dict = NULL;
        }
    }
    if (err != FMRB_OK) {
        // Any failure means "no dictionary": kana input stays as it is until
        // the file is written again.
        s_kj.may_hold = false;
        FMRB_LOGW(TAG, "%s not opened (%d): kanji conversion off", s_kj.path, err);
        return NULL;
    }
    fmrb_kanji_info_t info;
    fmrb_kanji_get_info(dict, &info);
    FMRB_LOGI(TAG, "%s open in %u us: %u readings, %u blocks, %u bytes held (+%u buffers)",
              s_kj.path, (unsigned)(fmrb_hal_time_get_us() - t0),
              (unsigned)info.reading_count, (unsigned)info.block_count,
              (unsigned)info.table_bytes, (unsigned)sizeof(kanji_buffers_t));
    s_kj.ready = true;
    return dict;
}

static void kanji_task(void *arg)
{
    (void)arg;
    s_kj.task = fmrb_task_get_current();
    s_kj.creating = false;
    fmrb_kanji_dict_t *dict = NULL;
    // The task stays once it exists (a few KB of stack): the dictionary may
    // come back after a change, and the change hook only signals it.
    for (;;) {
        if (s_kj.held && dict) {
            s_kj.ready = false;
            fmrb_kanji_close(dict);
            dict = NULL;
            FMRB_LOGI(TAG, "%s closed: the file is being changed", s_kj.path);
        }
        if (!dict) {
            s_kj.may_hold = false;
        }
        if (s_kj.reopen) {
            // Say "may hold" before looking at `held`; the change hook sets
            // `held` before looking at `may_hold`. Either it waits for this
            // open to end, or this task sees the change and does not open.
            s_kj.may_hold = true;
            __sync_synchronize();
            if (s_kj.held) {
                s_kj.may_hold = (dict != NULL);
            } else {
                s_kj.reopen = false;
                if (dict) {
                    s_kj.ready = false;
                    fmrb_kanji_close(dict);
                }
                dict = open_dict();
            }
        }
        if (s_kj.req_seq != s_kj.done_seq) {
            lookup_one(dict);
        }
        ulTaskNotifyTake(pdTRUE, FMRB_MAX_DELAY);
    }
}

static void create_task(void)
{
    if (s_kj.task || s_kj.creating) return;
    s_kj.creating = true;
    fmrb_task_handle_t handle = 0;
    if (fmrb_task_create_ex(kanji_task, "fmrb_kanji", FMRB_KANJI_TASK_STACK_SIZE, NULL,
                            FMRB_KANJI_TASK_PRIORITY, &handle,
                            FMRB_KANJI_TASK_FLAGS) != FMRB_PASS) {
        s_kj.creating = false;
        FMRB_LOGW(TAG, "kanji task not created: conversion off");
        return;
    }
    s_kj.task = handle;
}

// The file HAL's change hook (fmrb_hal_file_set_change_hook). Runs on the
// task that changes the file; only BEGIN waits, and only for this task's
// close (the host task never writes the dictionary).
#define KANJI_CLOSE_WAIT_MS 2000
#define KANJI_CLOSE_POLL_MS 5

static bool change_hook(const char *real_path, fmrb_file_change_t change)
{
    // NULL: the end of a write reported by a close, on the path this hook
    // said true to (the only hook).
    if (real_path != NULL && strcmp(real_path, s_kj.real_path) != 0) {
        return false;
    }
    switch (change) {
    case FMRB_FILE_CHANGE_BEGIN: {
        s_kj.held = true;
        s_kj.ready = false;
        __sync_synchronize();
        fmrb_task_handle_t t = s_kj.task;
        if (!t) return true;
        fmrb_task_notify_give(t);
        uint32_t waited = 0;
        while (s_kj.may_hold && waited < KANJI_CLOSE_WAIT_MS) {
            fmrb_task_delay_ms(KANJI_CLOSE_POLL_MS);
            waited += KANJI_CLOSE_POLL_MS;
        }
        if (s_kj.may_hold) {
            FMRB_LOGW(TAG, "%s still open after %u ms", s_kj.path, (unsigned)waited);
        }
        return true;
    }
    case FMRB_FILE_CHANGE_WRITTEN:
        s_kj.held = false;
        s_kj.reopen = true;
        if (s_kj.task) {
            fmrb_task_notify_give(s_kj.task);
        } else {
            // No dictionary at boot: the first one placed starts the task.
            create_task();
        }
        return true;
    case FMRB_FILE_CHANGE_REMOVED:
        s_kj.held = false;
        s_kj.reopen = false;
        FMRB_LOGI(TAG, "%s removed: kanji conversion off", s_kj.path);
        if (s_kj.task) fmrb_task_notify_give(s_kj.task);
        return true;
    }
    return true;
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
    // Watch the file from now on, also when it is not there yet: replacing
    // or placing it needs no reboot.
    if (fmrb_hal_file_resolve_path(s_kj.path, s_kj.real_path, sizeof(s_kj.real_path)) == FMRB_OK) {
        fmrb_hal_file_set_change_hook(change_hook);
    } else {
        FMRB_LOGW(TAG, "%s: path too long to watch for changes", s_kj.path);
    }
    s_kj.reopen = true;
    fmrb_file_info_t st;
    if (fmrb_hal_file_stat(s_kj.path, &st) != FMRB_OK) {
        // The usual case: no dictionary on this machine. Nothing is created.
        FMRB_LOGI(TAG, "no dictionary at %s: kanji conversion off", s_kj.path);
        return;
    }
    create_task();
    if (s_kj.task) fmrb_mem_log_boot_snapshot("kanji");
}

#endif
