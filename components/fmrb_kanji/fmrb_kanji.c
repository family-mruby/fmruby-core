// Kanji dictionary lookup (doc/kanji_input/dict_format.md, version 1).
// The reference answers are tools/kanji/lookup.rb's; the host test
// (tools/kanji/test/c_lookup.rb) compares the two.

#include "fmrb_kanji.h"

#include <string.h>

#include "fmrb_hal_file.h"
#include "fmrb_mem.h"

#define KD_MAGIC "FMKD"
#define KD_VERSION 1
#define KD_HEADER_SIZE 64
#define KD_MAX_CHARS (64 * 256)
// The tables (character table + block index) of a 1 MB dictionary are about
// 16 KB; a header asking for more than this is treated as broken rather than
// allocated.
#define KD_MAX_TABLE_BYTES (128 * 1024)

// One-byte codes (dict_format.md "packed").
#define KD_HIRA_FIRST 0x3041
#define KD_HIRA_LAST 0x3096
#define KD_KATA_FIRST 0x30A1
#define KD_KATA_LAST 0x30FA
#define KD_CHOON 0x30FC
#define KD_HIRA_BASE 0x01
#define KD_KATA_BASE 0x57
#define KD_CHOON_CODE 0xB1
#define KD_LEAD_BASE 0xC0

struct fmrb_kanji_dict {
    fmrb_file_t file;
    uint32_t version;
    uint32_t file_size;
    uint32_t char_count;
    uint32_t index_size;
    uint32_t block_count;
    uint32_t body_offset;
    uint32_t reading_count;
    uint32_t word_count;
    uint32_t single_count;
    uint32_t crc32;
    uint32_t table_bytes;
    uint32_t reads;
    const uint8_t *chars;   // char_count x u16 LE
    const uint8_t *index;   // index_size bytes: u32 LE offsets, then keys
    uint8_t tables[];       // chars followed by index
};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static fmrb_err_t read_at(fmrb_file_t f, uint32_t offset, void *buf, size_t size)
{
    if (fmrb_hal_file_seek(f, (int32_t)offset, FMRB_SEEK_SET) != FMRB_OK) {
        return FMRB_ERR_FAILED;
    }
    uint8_t *p = (uint8_t *)buf;
    size_t done = 0;
    while (done < size) {
        size_t n = 0;
        if (fmrb_hal_file_read(f, p + done, size - done, &n) != FMRB_OK || n == 0) {
            return FMRB_ERR_FAILED;
        }
        done += n;
    }
    return FMRB_OK;
}

// Compare packed strings: bytes first, then the shorter is smaller.
static int key_cmp(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0) {
        return c;
    }
    return (alen > blen) - (alen < blen);
}

static void block_key(const fmrb_kanji_dict_t *d, uint32_t i, const uint8_t **key, size_t *len)
{
    uint32_t off = rd32(d->index + i * 4);
    *len = d->index[off];
    *key = d->index + off + 1;
}

// The block index: every offset inside the index, every key 1..127 bytes,
// keys strictly increasing.
static fmrb_err_t check_index(const fmrb_kanji_dict_t *d)
{
    if ((uint64_t)d->block_count * 4 > d->index_size) {
        return FMRB_ERR_INVALID_STATE;
    }
    const uint8_t *prev = NULL;
    size_t prev_len = 0;
    for (uint32_t i = 0; i < d->block_count; i++) {
        uint32_t off = rd32(d->index + i * 4);
        if (off >= d->index_size) {
            return FMRB_ERR_INVALID_STATE;
        }
        size_t len = d->index[off];
        if (len == 0 || len > FMRB_KANJI_READING_MAX || off + 1 + len > d->index_size) {
            return FMRB_ERR_INVALID_STATE;
        }
        const uint8_t *key = d->index + off + 1;
        if (prev && key_cmp(prev, prev_len, key, len) >= 0) {
            return FMRB_ERR_INVALID_STATE;
        }
        prev = key;
        prev_len = len;
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_kanji_open(const char *path, fmrb_kanji_dict_t **out)
{
    if (path == NULL || out == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }
    *out = NULL;

    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &f) != FMRB_OK || f == NULL) {
        return FMRB_ERR_NOT_FOUND;
    }

    uint8_t h[KD_HEADER_SIZE];
    uint32_t actual_size = 0;
    fmrb_err_t err = FMRB_ERR_INVALID_STATE;
    if (fmrb_hal_file_size(f, &actual_size) != FMRB_OK) {
        err = FMRB_ERR_FAILED;
        goto fail;
    }
    if (actual_size < KD_HEADER_SIZE) {
        goto fail;
    }
    if (read_at(f, 0, h, sizeof(h)) != FMRB_OK) {
        err = FMRB_ERR_FAILED;
        goto fail;
    }
    if (memcmp(h, KD_MAGIC, 4) != 0) {
        goto fail;
    }
    if (rd16(h + 4) != KD_VERSION) {
        err = FMRB_ERR_NOT_SUPPORTED;
        goto fail;
    }

    uint32_t header_size = rd16(h + 6);
    uint32_t file_size = rd32(h + 8);
    uint32_t chars_offset = rd32(h + 12);
    uint32_t char_count = rd16(h + 16);
    uint32_t index_offset = rd32(h + 20);
    uint32_t index_size = rd32(h + 24);
    uint32_t block_count = rd32(h + 28);
    uint32_t body_offset = rd32(h + 32);
    uint32_t block_size = rd32(h + 36);

    // The marks of dict_format.md, plus bounds that keep the reads and the
    // allocation sane on a damaged header.
    uint64_t index_end = (uint64_t)index_offset + index_size;
    if (header_size != KD_HEADER_SIZE || file_size != actual_size ||
        block_size != FMRB_KANJI_BLOCK_SIZE || chars_offset < KD_HEADER_SIZE ||
        char_count > KD_MAX_CHARS || block_count == 0 || index_size == 0 ||
        index_offset != chars_offset + char_count * 2 || index_end > body_offset ||
        (uint64_t)body_offset + (uint64_t)block_count * block_size != file_size) {
        goto fail;
    }
    uint32_t table_len = char_count * 2 + index_size;
    if (table_len > KD_MAX_TABLE_BYTES) {
        goto fail;
    }

    fmrb_kanji_dict_t *d = (fmrb_kanji_dict_t *)fmrb_sys_malloc(sizeof(*d) + table_len);
    if (d == NULL) {
        err = FMRB_ERR_NO_MEMORY;
        goto fail;
    }
    memset(d, 0, sizeof(*d));
    d->file = f;
    d->version = KD_VERSION;
    d->file_size = file_size;
    d->char_count = char_count;
    d->index_size = index_size;
    d->block_count = block_count;
    d->body_offset = body_offset;
    d->reading_count = rd32(h + 40);
    d->word_count = rd32(h + 44);
    d->single_count = rd32(h + 48);
    d->crc32 = rd32(h + 52);
    d->table_bytes = (uint32_t)(sizeof(*d) + table_len);
    d->chars = d->tables;
    d->index = d->tables + char_count * 2;

    // The character table and the block index are adjacent: one read.
    err = read_at(f, chars_offset, d->tables, table_len);
    if (err == FMRB_OK) {
        err = check_index(d);
    }
    if (err != FMRB_OK) {
        fmrb_sys_free(d);
        goto fail;
    }
    *out = d;
    return FMRB_OK;

fail:
    fmrb_hal_file_close(f);
    return err;
}

void fmrb_kanji_close(fmrb_kanji_dict_t *dict)
{
    if (dict == NULL) {
        return;
    }
    fmrb_hal_file_close(dict->file);
    fmrb_sys_free(dict);
}

void fmrb_kanji_get_info(const fmrb_kanji_dict_t *dict, fmrb_kanji_info_t *out)
{
    if (dict == NULL || out == NULL) {
        return;
    }
    out->version = dict->version;
    out->file_size = dict->file_size;
    out->char_count = dict->char_count;
    out->block_count = dict->block_count;
    out->reading_count = dict->reading_count;
    out->word_count = dict->word_count;
    out->single_count = dict->single_count;
    out->crc32 = dict->crc32;
    out->table_bytes = dict->table_bytes;
}

uint32_t fmrb_kanji_block_reads(const fmrb_kanji_dict_t *dict)
{
    return dict ? dict->reads : 0;
}

// Pack the leading kana of a UTF-8 string into one-byte codes. Returns the
// number of characters packed (at most `cap`); *all is set when the whole
// string was kana (and fit).
static size_t pack_reading(const char *s, size_t len, uint8_t *out, size_t cap, int *all)
{
    const uint8_t *p = (const uint8_t *)s;
    size_t i = 0;
    size_t n = 0;
    *all = 0;
    while (i < len) {
        if (n == cap || i + 3 > len || (p[i] & 0xF0) != 0xE0 || (p[i + 1] & 0xC0) != 0x80 ||
            (p[i + 2] & 0xC0) != 0x80) {
            return n;
        }
        uint32_t cp = ((uint32_t)(p[i] & 0x0F) << 12) | ((uint32_t)(p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
        uint8_t code;
        if (cp >= KD_HIRA_FIRST && cp <= KD_HIRA_LAST) {
            code = (uint8_t)(KD_HIRA_BASE + (cp - KD_HIRA_FIRST));
        } else if (cp >= KD_KATA_FIRST && cp <= KD_KATA_LAST) {
            code = (uint8_t)(KD_KATA_BASE + (cp - KD_KATA_FIRST));
        } else if (cp == KD_CHOON) {
            code = KD_CHOON_CODE;
        } else {
            return n;
        }
        out[n++] = code;
        i += 3;
    }
    *all = 1;
    return n;
}

// The block that would hold `key`: the last block whose first reading is
// <= key, or -1 when the key sorts before the first block.
static int32_t block_for(const fmrb_kanji_dict_t *d, const uint8_t *key, size_t len)
{
    const uint8_t *k0;
    size_t l0;
    block_key(d, 0, &k0, &l0);
    if (key_cmp(key, len, k0, l0) < 0) {
        return -1;
    }
    uint32_t lo = 0;
    uint32_t hi = d->block_count - 1;
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1) / 2;
        const uint8_t *km;
        size_t lm;
        block_key(d, mid, &km, &lm);
        if (key_cmp(km, lm, key, len) <= 0) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return (int32_t)lo;
}

// Skip `count` candidates from `pos`; returns the position after them or 0
// when they run past the block.
static uint32_t skip_candidates(const uint8_t *b, uint32_t pos, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (pos >= FMRB_KANJI_BLOCK_SIZE) {
            return 0;
        }
        uint32_t clen = b[pos];
        if (clen == 0 || pos + 1 + clen > FMRB_KANJI_BLOCK_SIZE) {
            return 0;
        }
        pos += 1 + clen;
    }
    return pos;
}

// Look for `key` in a block already in the buffer.
static fmrb_err_t scan_block(const uint8_t *b, const uint8_t *key, size_t len,
                             uint16_t *first_pos, uint16_t *count)
{
    uint32_t pos = 0;
    while (pos < FMRB_KANJI_BLOCK_SIZE) {
        uint32_t rlen = b[pos];
        if (rlen == 0) {
            break;
        }
        if (rlen > FMRB_KANJI_READING_MAX || pos + 1 + rlen + 1 > FMRB_KANJI_BLOCK_SIZE) {
            return FMRB_ERR_INVALID_STATE;
        }
        int c = key_cmp(b + pos + 1, rlen, key, len);
        pos += 1 + rlen;
        uint32_t n = b[pos];
        if (n & 0x80) {
            if (pos + 2 > FMRB_KANJI_BLOCK_SIZE) {
                return FMRB_ERR_INVALID_STATE;
            }
            n = ((n & 0x7F) << 8) | b[pos + 1];
            pos += 2;
        } else {
            pos += 1;
        }
        if (c > 0) {
            return FMRB_ERR_NOT_FOUND;
        }
        uint32_t next = skip_candidates(b, pos, n);
        if (next == 0 && n > 0) {
            return FMRB_ERR_INVALID_STATE;
        }
        if (c == 0) {
            if (n == 0) {
                return FMRB_ERR_INVALID_STATE;
            }
            *first_pos = (uint16_t)pos;
            *count = (uint16_t)n;
            return FMRB_OK;
        }
        pos = next;
    }
    return FMRB_ERR_NOT_FOUND;
}

// Find a packed key; `loaded` is the block now in the buffer (-1: none) and
// is updated when another block is read.
static fmrb_err_t find_key(fmrb_kanji_dict_t *d, const uint8_t *key, size_t len, uint8_t *block,
                           int32_t *loaded, uint16_t *first_pos, uint16_t *count)
{
    int32_t b = block_for(d, key, len);
    if (b < 0) {
        return FMRB_ERR_NOT_FOUND;
    }
    if (b != *loaded) {
        *loaded = -1;
        fmrb_err_t err = read_at(d->file, d->body_offset + (uint32_t)b * FMRB_KANJI_BLOCK_SIZE, block,
                                 FMRB_KANJI_BLOCK_SIZE);
        d->reads++;
        if (err != FMRB_OK) {
            return err;
        }
        *loaded = b;
    }
    return scan_block(block, key, len, first_pos, count);
}

static void fill_result(const fmrb_kanji_dict_t *d, const uint8_t *block, uint16_t first_pos,
                        uint16_t count, size_t matched_chars, fmrb_kanji_result_t *out)
{
    out->dict = d;
    out->block = block;
    out->count = count;
    out->matched_bytes = (uint16_t)(matched_chars * 3);  // every one-byte kana is 3 bytes of UTF-8
    out->first_pos = first_pos;
    out->cursor_index = 0;
    out->cursor_pos = first_pos;
}

fmrb_err_t fmrb_kanji_lookup(fmrb_kanji_dict_t *dict, const char *reading, size_t len,
                             uint8_t *block, fmrb_kanji_result_t *out)
{
    if (dict == NULL || (reading == NULL && len > 0) || block == NULL || out == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }
    memset(out, 0, sizeof(*out));
    uint8_t key[FMRB_KANJI_READING_MAX];
    int all = 0;
    size_t n = pack_reading(reading, len, key, sizeof(key), &all);
    if (!all || n == 0) {
        return FMRB_ERR_NOT_FOUND;
    }
    int32_t loaded = -1;
    uint16_t first_pos = 0;
    uint16_t count = 0;
    fmrb_err_t err = find_key(dict, key, n, block, &loaded, &first_pos, &count);
    if (err == FMRB_OK) {
        fill_result(dict, block, first_pos, count, n, out);
    }
    return err;
}

fmrb_err_t fmrb_kanji_longest_prefix(fmrb_kanji_dict_t *dict, const char *reading, size_t len,
                                     uint8_t *block, fmrb_kanji_result_t *out)
{
    if (dict == NULL || (reading == NULL && len > 0) || block == NULL || out == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }
    memset(out, 0, sizeof(*out));
    uint8_t key[FMRB_KANJI_READING_MAX];
    int all = 0;
    size_t n = pack_reading(reading, len, key, sizeof(key), &all);
    int32_t loaded = -1;
    for (; n > 0; n--) {
        uint16_t first_pos = 0;
        uint16_t count = 0;
        fmrb_err_t err = find_key(dict, key, n, block, &loaded, &first_pos, &count);
        if (err == FMRB_OK) {
            fill_result(dict, block, first_pos, count, n, out);
            return FMRB_OK;
        }
        if (err != FMRB_ERR_NOT_FOUND) {
            return err;
        }
    }
    return FMRB_ERR_NOT_FOUND;
}

// Append one code point as UTF-8; false when it does not fit (room for the
// NUL is kept).
static int put_utf8(char *out, size_t cap, size_t *pos, uint32_t cp)
{
    uint8_t buf[3];
    size_t n;
    if (cp < 0x80) {
        buf[0] = (uint8_t)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (uint8_t)(0xC0 | (cp >> 6));
        buf[1] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 2;
    } else {
        buf[0] = (uint8_t)(0xE0 | (cp >> 12));
        buf[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 3;
    }
    if (*pos + n + 1 > cap) {
        return 0;
    }
    memcpy(out + *pos, buf, n);
    *pos += n;
    return 1;
}

fmrb_err_t fmrb_kanji_candidate(fmrb_kanji_result_t *result, uint16_t index,
                                char *utf8, size_t cap, size_t *out_len)
{
    if (result == NULL || result->dict == NULL || utf8 == NULL || cap == 0 || index >= result->count) {
        return FMRB_ERR_INVALID_PARAM;
    }
    const fmrb_kanji_dict_t *d = result->dict;
    const uint8_t *b = result->block;
    if (index < result->cursor_index) {
        result->cursor_index = 0;
        result->cursor_pos = result->first_pos;
    }
    uint32_t pos = skip_candidates(b, result->cursor_pos, (uint32_t)(index - result->cursor_index));
    if (pos == 0 || pos >= FMRB_KANJI_BLOCK_SIZE) {
        return FMRB_ERR_INVALID_STATE;
    }
    uint32_t clen = b[pos];
    if (clen == 0 || pos + 1 + clen > FMRB_KANJI_BLOCK_SIZE) {
        return FMRB_ERR_INVALID_STATE;
    }
    const uint8_t *p = b + pos + 1;
    size_t w = 0;
    for (uint32_t i = 0; i < clen;) {
        uint8_t c = p[i];
        uint32_t cp;
        if (c >= KD_LEAD_BASE) {
            if (i + 1 >= clen) {
                return FMRB_ERR_INVALID_STATE;
            }
            uint32_t ci = ((uint32_t)(c - KD_LEAD_BASE) << 8) | p[i + 1];
            if (ci >= d->char_count) {
                return FMRB_ERR_INVALID_STATE;
            }
            cp = rd16(d->chars + ci * 2);
            i += 2;
        } else if (c >= KD_HIRA_BASE && c < KD_KATA_BASE) {
            cp = KD_HIRA_FIRST + (c - KD_HIRA_BASE);
            i++;
        } else if (c >= KD_KATA_BASE && c < KD_CHOON_CODE) {
            cp = KD_KATA_FIRST + (c - KD_KATA_BASE);
            i++;
        } else if (c == KD_CHOON_CODE) {
            cp = KD_CHOON;
            i++;
        } else {
            return FMRB_ERR_INVALID_STATE;
        }
        if (cp == 0) {
            return FMRB_ERR_INVALID_STATE;
        }
        if (!put_utf8(utf8, cap, &w, cp)) {
            utf8[0] = '\0';
            return FMRB_ERR_INVALID_PARAM;
        }
    }
    utf8[w] = '\0';
    if (out_len) {
        *out_len = w;
    }
    result->cursor_index = (uint16_t)(index + 1);
    result->cursor_pos = (uint16_t)(pos + 1 + clen);
    return FMRB_OK;
}

// CRC-32 (zlib's polynomial), four bits at a time: a 64-byte table.
static const uint32_t s_crc_nibble[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ s_crc_nibble[crc & 0x0F];
        crc = (crc >> 4) ^ s_crc_nibble[crc & 0x0F];
    }
    return crc;
}

fmrb_err_t fmrb_kanji_verify(fmrb_kanji_dict_t *dict, uint8_t *block)
{
    if (dict == NULL || block == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t pos = KD_HEADER_SIZE;
    while (pos < dict->file_size) {
        uint32_t n = dict->file_size - pos;
        if (n > FMRB_KANJI_BLOCK_SIZE) {
            n = FMRB_KANJI_BLOCK_SIZE;
        }
        if (read_at(dict->file, pos, block, n) != FMRB_OK) {
            return FMRB_ERR_FAILED;
        }
        crc = crc_update(crc, block, n);
        pos += n;
    }
    return (crc ^ 0xFFFFFFFFu) == dict->crc32 ? FMRB_OK : FMRB_ERR_INVALID_STATE;
}
