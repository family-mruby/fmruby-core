#pragma once

// The kanji dictionary's own task (doc/kanji_input/, K2b).
//
// One low-priority task opens the dictionary (components/fmrb_kanji) and does
// every lookup, so the host task -- the single pump for input, drawing and
// sound -- never waits on the SD card. The host task asks with
// fmrb_kanji_lookup_request; the task looks the reading up, writes the
// candidates out as UTF-8 into a result buffer of its own (PSRAM) and tells
// the host task with fmrb_host_kanji_result. One request is outstanding at a
// time: the host task does not ask again before it has the answer.
//
// The task exists only when conversion is enabled and the dictionary file is
// there; without one nothing is created and kana input stays as it was. The
// browser build has no task at all (it converts with the browser's IME).
//
// The file can be replaced while the machine runs: a change hook on the file
// HAL (and the dev remote's /fs/put, /fs/del) asks the task to close the file
// before it is written, removed or renamed, and to open it again after. While
// it is closed, lookups answer "none" and kana input goes on as kana. A
// dictionary placed later at the configured path starts the task.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest reading a request carries, in bytes (UTF-8 kana, 3 bytes each). */
#define FMRB_KANJI_LOOKUP_READING_MAX 384

/** whole_index when every candidate replaces matched_bytes. */
#define FMRB_KANJI_LOOKUP_NO_WHOLE 0xFFFF

/** What a lookup found. */
typedef struct {
    uint32_t seq;            /**< the request it answers */
    bool found;              /**< false: no candidate (the lookup failed) */
    uint16_t matched_bytes;  /**< bytes of the reading the candidates replace (longest match) */
    uint16_t count;          /**< candidates kept (all of them unless the buffer filled) */
    /**
     * The candidate that replaces the whole reading instead (the katakana of
     * all of it when only a leading part matched, doc/kanji_input K6), or
     * FMRB_KANJI_LOOKUP_NO_WHOLE.
     */
    uint16_t whole_index;
} fmrb_kanji_lookup_result_t;

/**
 * @brief The settings, from system_conf (kanji_dict, kanji_convert).
 *
 * Called by the kernel while it reads the configuration, before
 * fmrb_kanji_lookup_start. A NULL or empty path keeps the default
 * (/sd/dict/fmrb_kanji.dic).
 */
void fmrb_kanji_lookup_configure(const char *path, bool enabled);

/**
 * @brief Start the task when conversion is enabled and the file exists.
 *
 * Called once at boot after the host task is up. Installs the file change
 * hook whenever conversion is enabled. The task opens the dictionary; if that
 * fails conversion stays off until the file is written again.
 */
void fmrb_kanji_lookup_start(void);

/** @brief True once the dictionary is open and lookups can be asked for. */
bool fmrb_kanji_lookup_ready(void);

/**
 * @brief Ask for the candidates of the longest leading part of a reading.
 *
 * Host task only. Katakana in the reading is looked up as hiragana. The
 * answer arrives as fmrb_host_kanji_result(seq).
 *
 * @return The request's sequence number, 0 when it could not be asked
 *         (no dictionary, reading too long or empty).
 */
uint32_t fmrb_kanji_lookup_request(const char *reading, size_t len);

/**
 * @brief The last answer. Valid from fmrb_host_kanji_result until the next
 *        request.
 */
const fmrb_kanji_lookup_result_t *fmrb_kanji_lookup_result(void);

/**
 * @brief One candidate of the last answer as UTF-8 (not NUL-terminated).
 * @return NULL when index is out of range.
 */
const char *fmrb_kanji_lookup_candidate(uint16_t index, size_t *len);

#ifdef __cplusplus
}
#endif
