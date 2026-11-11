#pragma once

// The candidate list a conversion shows (doc/kanji_input K3, K6): the
// dictionary's candidates of the longest leading part of a reading, with the
// katakana forms added, each candidate knowing how much of the reading it
// replaces. Pure C over fmrb_kanji.h; the kanji task (main/kernel/host/
// kanji_lookup.c) calls it, and the host test (tools/kanji/test) drives it
// the same way.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"
#include "fmrb_kanji.h"

#ifdef __cplusplus
extern "C" {
#endif

/** whole_index when no candidate is the katakana of the whole reading. */
#define FMRB_KANJI_NO_INDEX 0xFFFF

/** Where the candidates are written: text, and offset[0..max_cands]. */
typedef struct {
    char *text;
    size_t text_size;
    uint16_t *offset;        /**< max_cands + 1 entries; candidate i is [offset[i], offset[i+1]) */
    uint16_t max_cands;
} fmrb_kanji_cand_buf_t;

/** What was made. */
typedef struct {
    bool found;              /**< false: no candidate at all (a read error) */
    uint16_t matched_bytes;  /**< bytes of the reading most candidates replace */
    uint16_t count;          /**< candidates written */
    /**
     * The candidate that replaces the whole reading instead of matched_bytes
     * (its katakana form, when only a leading part matched), or
     * FMRB_KANJI_NO_INDEX.
     */
    uint16_t whole_index;
} fmrb_kanji_cands_t;

/**
 * @brief Make the candidate list of a reading.
 *
 * Katakana in @p reading is folded to hiragana in place first (the
 * dictionary's readings are hiragana; the byte length is kept). The order:
 * the dictionary's first candidate; the katakana of the whole reading when
 * only a leading part matched (it replaces everything); the katakana of the
 * matched part (unless the dictionary has the same); the dictionary's other
 * candidates. A reading with no leading part in the dictionary gets its
 * katakana as the only candidate, replacing all of it.
 *
 * @return FMRB_OK or FMRB_ERR_NOT_FOUND (the list is made either way), or
 *         the dictionary's error (no candidates).
 */
fmrb_err_t fmrb_kanji_make_candidates(fmrb_kanji_dict_t *dict, char *reading, size_t len,
                                      uint8_t *block, const fmrb_kanji_cand_buf_t *buf,
                                      fmrb_kanji_cands_t *out);

#ifdef __cplusplus
}
#endif
