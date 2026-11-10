#pragma once

// Kanji dictionary lookup (doc/kanji_input/, file format in
// doc/kanji_input/dict_format.md, version 1). The same code runs on the
// ESP32 boards, the native build and the simulation; it reads the file
// through the file HAL and allocates from the SYSTEM pool (PSRAM on the
// boards), never from internal RAM.
//
// Opening reads the header, the character table and the block index (about
// 16 KB for a 1 MB dictionary) and keeps the file open. A lookup reads one
// 4 KB block into a buffer the caller owns. A dictionary handle is not
// thread-safe: one task opens it and does all of its lookups.

#include <stdint.h>
#include <stddef.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the block buffer a lookup needs. */
#define FMRB_KANJI_BLOCK_SIZE 4096
/** Longest reading in the file, in characters (kana). */
#define FMRB_KANJI_READING_MAX 127
/** Buffer size that holds any candidate as UTF-8, with its NUL. */
#define FMRB_KANJI_CAND_UTF8_MAX (255 * 3 + 1)

typedef struct fmrb_kanji_dict fmrb_kanji_dict_t;

/** Numbers from the header. */
typedef struct {
    uint32_t version;
    uint32_t file_size;
    uint32_t char_count;
    uint32_t block_count;
    uint32_t reading_count;
    uint32_t word_count;
    uint32_t single_count;
    uint32_t crc32;
    uint32_t table_bytes;   /**< memory held while open (tables + handle) */
} fmrb_kanji_info_t;

/**
 * The candidates of one reading. Points into the caller's block buffer and
 * the dictionary's tables: valid until that buffer is reused or the
 * dictionary is closed. Read the candidates with fmrb_kanji_candidate.
 */
typedef struct {
    const fmrb_kanji_dict_t *dict;
    const uint8_t *block;
    uint16_t count;          /**< number of candidates */
    uint16_t matched_bytes;  /**< UTF-8 bytes of the reading that matched */
    uint16_t first_pos;      /**< private: first candidate in the block */
    uint16_t cursor_index;   /**< private: next candidate's index */
    uint16_t cursor_pos;     /**< private: next candidate's position */
} fmrb_kanji_result_t;

/**
 * @brief Open a dictionary file.
 *
 * Checks the marks dict_format.md lists (magic, version, file size, the
 * header's offsets) and the block index, then loads the tables. The CRC is
 * not checked here (fmrb_kanji_verify does that).
 *
 * @param path Virtual path (e.g. "/sd/dict/fmrb_kanji.dic").
 * @param out  Receives the handle on success.
 * @return FMRB_OK; FMRB_ERR_NOT_FOUND when the file cannot be opened;
 *         FMRB_ERR_NOT_SUPPORTED for another format version;
 *         FMRB_ERR_INVALID_STATE when the file is not a dictionary or is
 *         broken (short copy, inconsistent header); FMRB_ERR_NO_MEMORY;
 *         FMRB_ERR_FAILED on a read error. Any error means "no dictionary".
 */
fmrb_err_t fmrb_kanji_open(const char *path, fmrb_kanji_dict_t **out);

/** @brief Close the file and free the tables. NULL is ignored. */
void fmrb_kanji_close(fmrb_kanji_dict_t *dict);

/** @brief Numbers from the header of an open dictionary. */
void fmrb_kanji_get_info(const fmrb_kanji_dict_t *dict, fmrb_kanji_info_t *out);

/** @brief Blocks read from the file since opening (for measurement). */
uint32_t fmrb_kanji_block_reads(const fmrb_kanji_dict_t *dict);

/**
 * @brief Candidates of exactly this reading.
 *
 * @param reading UTF-8 hiragana (and the long vowel mark), not necessarily
 *                NUL-terminated.
 * @param len     Bytes of @p reading.
 * @param block   The caller's buffer of FMRB_KANJI_BLOCK_SIZE bytes; one
 *                block of the file is read into it.
 * @param out     Receives the candidates; out->matched_bytes == len.
 * @return FMRB_OK; FMRB_ERR_NOT_FOUND when the dictionary has no such
 *         reading (including readings with characters other than kana);
 *         FMRB_ERR_INVALID_STATE when the block is broken;
 *         FMRB_ERR_FAILED on a read error; FMRB_ERR_INVALID_PARAM.
 */
fmrb_err_t fmrb_kanji_lookup(fmrb_kanji_dict_t *dict, const char *reading, size_t len,
                             uint8_t *block, fmrb_kanji_result_t *out);

/**
 * @brief Candidates of the longest leading part of the reading that has any.
 *
 * Tries the whole reading first, then one character shorter each time
 * (dict_format.md "longest match"). Blocks already in the buffer are not read
 * again. out->matched_bytes tells how much of @p reading matched; the rest is
 * left for the next conversion. Only the leading kana count: the search
 * starts before the first character that is not kana, and readings longer
 * than FMRB_KANJI_READING_MAX characters are cut there.
 *
 * @return As fmrb_kanji_lookup; FMRB_ERR_NOT_FOUND when no leading part has
 *         candidates.
 */
fmrb_err_t fmrb_kanji_longest_prefix(fmrb_kanji_dict_t *dict, const char *reading, size_t len,
                                     uint8_t *block, fmrb_kanji_result_t *out);

/**
 * @brief One candidate as NUL-terminated UTF-8.
 *
 * Reading the candidates in order costs nothing extra; going back starts the
 * walk again from the first.
 *
 * @param result   From a lookup.
 * @param index    0 .. result->count - 1.
 * @param utf8     Output buffer (FMRB_KANJI_CAND_UTF8_MAX always suffices).
 * @param cap      Size of @p utf8.
 * @param out_len  Optional: bytes written, without the NUL.
 * @return FMRB_OK; FMRB_ERR_INVALID_PARAM (index out of range, buffer too
 *         small); FMRB_ERR_INVALID_STATE when the entry is broken.
 */
fmrb_err_t fmrb_kanji_candidate(fmrb_kanji_result_t *result, uint16_t index,
                                char *utf8, size_t cap, size_t *out_len);

/**
 * @brief Check the file's CRC-32 (everything after the header).
 *
 * Reads the whole file through @p block. For checking a copied or downloaded
 * dictionary, not for every open.
 *
 * @return FMRB_OK; FMRB_ERR_INVALID_STATE when the CRC differs;
 *         FMRB_ERR_FAILED on a read error.
 */
fmrb_err_t fmrb_kanji_verify(fmrb_kanji_dict_t *dict, uint8_t *block);

#ifdef __cplusplus
}
#endif
