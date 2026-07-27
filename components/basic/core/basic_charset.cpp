// Family BASIC character code <-> UTF-8 conversion. Table follows
// core_spec sec 12 (table B).

#include "basic_charset.hpp"

namespace fmrb_basic {
namespace {

struct charset_entry {
    uint8_t code;
    uint16_t ucs;
};

// Non-ASCII part of table B. ASCII (32-90) maps 1:1 and is handled in code.
// Codes 184-255 are BG pattern characters with no Unicode equivalent.
constexpr charset_entry charset_table[] = {
    // 91-95: [ (yen) ] ^ _   (91,93,94,95 are ASCII, only the yen sign differs)
    {92, 0x00A5},
    // 96-127: katakana, plain, A - MI
    {96, 0x30A2},  {97, 0x30A4},  {98, 0x30A6},  {99, 0x30A8},  {100, 0x30AA},
    {101, 0x30AB}, {102, 0x30AD}, {103, 0x30AF}, {104, 0x30B1}, {105, 0x30B3},
    {106, 0x30B5}, {107, 0x30B7}, {108, 0x30B9}, {109, 0x30BB}, {110, 0x30BD},
    {111, 0x30BF}, {112, 0x30C1}, {113, 0x30C4}, {114, 0x30C6}, {115, 0x30C8},
    {116, 0x30CA}, {117, 0x30CB}, {118, 0x30CC}, {119, 0x30CD}, {120, 0x30CE},
    {121, 0x30CF}, {122, 0x30D2}, {123, 0x30D5}, {124, 0x30D8}, {125, 0x30DB},
    {126, 0x30DE}, {127, 0x30DF},
    // 128-141: MU - WO
    {128, 0x30E0}, {129, 0x30E1}, {130, 0x30E2}, {131, 0x30E4}, {132, 0x30E6},
    {133, 0x30E8}, {134, 0x30E9}, {135, 0x30EA}, {136, 0x30EB}, {137, 0x30EC},
    {138, 0x30ED}, {139, 0x30EF}, {140, 0x30F3}, {141, 0x30F2},
    // 142-150: small katakana
    {142, 0x30A1}, {143, 0x30A3}, {144, 0x30A5}, {145, 0x30A7}, {146, 0x30A9},
    {147, 0x30E3}, {148, 0x30E5}, {149, 0x30E7}, {150, 0x30C3},
    // 151-159: GA - ZE
    {151, 0x30AC}, {152, 0x30AE}, {153, 0x30B0}, {154, 0x30B2}, {155, 0x30B4},
    {156, 0x30B6}, {157, 0x30B8}, {158, 0x30BA}, {159, 0x30BC},
    // 160-175: ZO - PO
    {160, 0x30BE}, {161, 0x30C0}, {162, 0x30C2}, {163, 0x30C5}, {164, 0x30C7},
    {165, 0x30C9}, {166, 0x30D0}, {167, 0x30D3}, {168, 0x30D6}, {169, 0x30D9},
    {170, 0x30DC}, {171, 0x30D1}, {172, 0x30D4}, {173, 0x30D7}, {174, 0x30DA},
    {175, 0x30DD},
    // 176-183: symbols (176 and 183 are undetermined in the spec, left unmapped)
    {177, 0x00B0},  // degree
    {178, 0x300C},  // corner bracket, opening
    {179, 0x300D},  // corner bracket, closing
    {180, 0x00A9},  // copyright
    {181, 0x00D7},  // multiplication
    {182, 0x00F7},  // division
};

constexpr size_t charset_table_size = sizeof(charset_table) / sizeof(charset_table[0]);

}  // namespace

uint32_t utf8_decode(const char* src, size_t len, size_t* used) noexcept {
    *used = 1;
    if (len == 0) {
        return 0;
    }
    const uint8_t b0 = static_cast<uint8_t>(src[0]);
    if (b0 < 0x80) {
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && len >= 2) {
        *used = 2;
        return static_cast<uint32_t>((b0 & 0x1F) << 6) |
               static_cast<uint32_t>(static_cast<uint8_t>(src[1]) & 0x3F);
    }
    if ((b0 & 0xF0) == 0xE0 && len >= 3) {
        *used = 3;
        return static_cast<uint32_t>((b0 & 0x0F) << 12) |
               static_cast<uint32_t>((static_cast<uint8_t>(src[1]) & 0x3F) << 6) |
               static_cast<uint32_t>(static_cast<uint8_t>(src[2]) & 0x3F);
    }
    if ((b0 & 0xF8) == 0xF0 && len >= 4) {
        *used = 4;
        return static_cast<uint32_t>((b0 & 0x07) << 18) |
               static_cast<uint32_t>((static_cast<uint8_t>(src[1]) & 0x3F) << 12) |
               static_cast<uint32_t>((static_cast<uint8_t>(src[2]) & 0x3F) << 6) |
               static_cast<uint32_t>(static_cast<uint8_t>(src[3]) & 0x3F);
    }
    return 0xFFFD;
}

uint8_t unicode_to_fbcode(uint32_t ucs) noexcept {
    if (ucs < 0x20) {
        // Control characters pass through: the loader wants TAB as whitespace
        // and key input uses TAB as the kana toggle. Table B's 0-31 group is
        // never produced from text.
        return static_cast<uint8_t>(ucs);
    }
    if (ucs >= 0x20 && ucs <= 0x5A) {
        return static_cast<uint8_t>(ucs);  // space - Z, ASCII identical
    }
    if (ucs == 0x5B || (ucs >= 0x5D && ucs <= 0x5F)) {
        return static_cast<uint8_t>(ucs);  // [ ] ^ _
    }
    if (ucs >= 0x61 && ucs <= 0x7A) {
        // Family BASIC has no lower case: fold to upper case.
        return static_cast<uint8_t>(ucs - 0x20);
    }
    if (ucs == 0x5C) {
        return 92;  // backslash reads as the yen sign in this code table
    }
    for (size_t i = 0; i < charset_table_size; ++i) {
        if (charset_table[i].ucs == ucs) {
            return charset_table[i].code;
        }
    }
    return charset_substitute;
}

size_t fbcode_to_utf8(uint8_t code, char* out) noexcept {
    uint32_t ucs = 0xFFFD;
    if (code >= 0x20 && code <= 0x5A) {
        ucs = code;
    } else if (code == 0x5B || (code >= 0x5D && code <= 0x5F)) {
        ucs = code;
    } else {
        for (size_t i = 0; i < charset_table_size; ++i) {
            if (charset_table[i].code == code) {
                ucs = charset_table[i].ucs;
                break;
            }
        }
    }

    if (ucs < 0x80) {
        out[0] = static_cast<char>(ucs);
        return 1;
    }
    if (ucs < 0x800) {
        out[0] = static_cast<char>(0xC0 | (ucs >> 6));
        out[1] = static_cast<char>(0x80 | (ucs & 0x3F));
        return 2;
    }
    out[0] = static_cast<char>(0xE0 | (ucs >> 12));
    out[1] = static_cast<char>(0x80 | ((ucs >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (ucs & 0x3F));
    return 3;
}

}  // namespace fmrb_basic
