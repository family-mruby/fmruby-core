/**
 * @file basic_tokens.hpp
 * @brief Crunched token encoding and the keyword table
 *
 * A loaded program is stored as crunched token bytes, one line at a time:
 *
 *   byte < 0x20   structural token (see ::fmrb_basic::token)
 *   0x20 - 0x7E   the character itself (operators, punctuation)
 *   0x80 - 0xFE   keyword token (::fmrb_basic::token, keyword_table below)
 *
 * Operands follow their token inline:
 *   token::number    2 bytes, little endian, raw 16 bit pattern
 *   token::string    1 length byte + that many Family BASIC character codes
 *   token::var_num   2 name bytes (significant characters, 0 padded)
 *   token::var_str   2 name bytes
 *   token::raw       1 length byte + that many bytes (REM and DATA payloads,
 *                    kept verbatim because their contents are not expressions)
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace fmrb_basic {

// X macro: name, spec abbreviation length (0 = none), kind.
// The abbreviation length is the number of characters before the '.' in the
// short form documented by core_spec / v3_spec ("P." -> PRINT is 1).
#define FMRB_BASIC_KEYWORDS(X)                        \
    /* commands */                                    \
    X(clear,     "CLEAR",    3, kw_kind::statement)   \
    X(new_,      "NEW",      0, kw_kind::statement)   \
    X(list,      "LIST",     1, kw_kind::statement)   \
    X(run,       "RUN",      1, kw_kind::statement)   \
    X(cont,      "CONT",     1, kw_kind::statement)   \
    X(load,      "LOAD",     2, kw_kind::statement)   \
    X(loads,     "LOADS",    0, kw_kind::statement)   \
    X(save,      "SAVE",     2, kw_kind::statement)   \
    X(saves,     "SAVES",    0, kw_kind::statement)   \
    X(system,    "SYSTEM",   1, kw_kind::statement)   \
    X(view,      "VIEW",     1, kw_kind::statement)   \
    X(keylist,   "KEYLIST",  0, kw_kind::statement)   \
    X(key,       "KEY",      1, kw_kind::statement)   \
    X(pause,     "PAUSE",    2, kw_kind::statement)   \
    X(auto_,     "AUTO",     0, kw_kind::statement)   \
    X(delete_,   "DELETE",   3, kw_kind::statement)   \
    X(renum,     "RENUM",    3, kw_kind::statement)   \
    X(find,      "FIND",     2, kw_kind::statement)   \
    X(tron,      "TRON",     0, kw_kind::statement)   \
    X(troff,     "TROFF",    0, kw_kind::statement)   \
    X(backup,    "BACKUP",   0, kw_kind::statement)   \
    X(game,      "GAME",     2, kw_kind::statement)   \
    /* general statements */                          \
    X(print,     "PRINT",    1, kw_kind::statement)   \
    X(input,     "INPUT",    1, kw_kind::statement)   \
    X(linput,    "LINPUT",   0, kw_kind::statement)   \
    X(if_,       "IF",       0, kw_kind::statement)   \
    X(then,      "THEN",     1, kw_kind::modifier)    \
    X(for_,      "FOR",      1, kw_kind::statement)   \
    X(to,        "TO",       0, kw_kind::modifier)    \
    X(step,      "STEP",     2, kw_kind::modifier)    \
    X(next,      "NEXT",     1, kw_kind::statement)   \
    X(goto_,     "GOTO",     1, kw_kind::statement)   \
    X(gosub,     "GOSUB",    3, kw_kind::statement)   \
    X(return_,   "RETURN",   2, kw_kind::statement)   \
    X(on,        "ON",       0, kw_kind::statement)   \
    X(off,       "OFF",      0, kw_kind::modifier)    \
    X(error,     "ERROR",    3, kw_kind::statement)   \
    X(resume,    "RESUME",   4, kw_kind::statement)   \
    X(stop,      "STOP",     3, kw_kind::statement)   \
    X(end,       "END",      1, kw_kind::statement)   \
    X(swap,      "SWAP",     2, kw_kind::statement)   \
    X(rem,       "REM",      0, kw_kind::statement)   \
    X(data,      "DATA",     1, kw_kind::statement)   \
    X(dim,       "DIM",      0, kw_kind::statement)   \
    X(read,      "READ",     3, kw_kind::statement)   \
    X(restore,   "RESTORE",  3, kw_kind::statement)   \
    X(call,      "CALL",     2, kw_kind::statement)   \
    X(poke,      "POKE",     0, kw_kind::statement)   \
    /* screen (B2) */                                 \
    X(locate,    "LOCATE",   3, kw_kind::statement)   \
    X(color,     "COLOR",    3, kw_kind::statement)   \
    X(cgen,      "CGEN",     3, kw_kind::statement)   \
    X(cls,       "CLS",      2, kw_kind::statement)   \
    X(cgset,     "CGSET",    2, kw_kind::statement)   \
    X(palet,     "PALET",    3, kw_kind::statement)   \
    X(screen,    "SCREEN",   2, kw_kind::statement)   \
    X(filter,    "FILTER",   3, kw_kind::statement)   \
    X(bgget,     "BGGET",    3, kw_kind::statement)   \
    X(bgput,     "BGPUT",    3, kw_kind::statement)   \
    X(bgtool,    "BGTOOL",   0, kw_kind::statement)   \
    /* sprites and sound (B3) */                      \
    X(def,       "DEF",      0, kw_kind::statement)   \
    X(sprite,    "SPRITE",   2, kw_kind::statement)   \
    X(move,      "MOVE",     1, kw_kind::statement)   \
    X(cut,       "CUT",      2, kw_kind::statement)   \
    X(era,       "ERA",      2, kw_kind::statement)   \
    X(can,       "CAN",      0, kw_kind::statement)   \
    X(position,  "POSITION", 3, kw_kind::statement)   \
    X(play,      "PLAY",     2, kw_kind::statement)   \
    X(beep,      "BEEP",     1, kw_kind::statement)   \
    X(click,     "CLICK",    3, kw_kind::statement)   \
    /* fmruby specific extensions (not Family BASIC) */ \
    X(circle,    "CIRCLE",   0, kw_kind::statement)   \
    X(present,   "PRESENT",  0, kw_kind::statement)   \
    /* operators */                                   \
    X(mod,       "MOD",      0, kw_kind::operator_kw) \
    X(not_,      "NOT",      0, kw_kind::operator_kw) \
    X(and_,      "AND",      0, kw_kind::operator_kw) \
    X(or_,       "OR",       0, kw_kind::operator_kw) \
    X(xor_,      "XOR",      0, kw_kind::operator_kw) \
    /* numeric functions */                           \
    X(abs,       "ABS",      2, kw_kind::function)    \
    X(sgn,       "SGN",      2, kw_kind::function)    \
    X(rnd,       "RND",      2, kw_kind::function)    \
    X(fre,       "FRE",      2, kw_kind::function)    \
    X(peek,      "PEEK",     2, kw_kind::function)    \
    X(asc,       "ASC",      2, kw_kind::function)    \
    X(val,       "VAL",      2, kw_kind::function)    \
    X(len,       "LEN",      0, kw_kind::function)    \
    X(instr,     "INSTR",    3, kw_kind::function)    \
    X(pos,       "POS",      0, kw_kind::function)    \
    X(csrlin,    "CSRLIN",   3, kw_kind::function)    \
    X(stick,     "STICK",    3, kw_kind::function)    \
    X(strig,     "STRIG",    0, kw_kind::function)    \
    X(xpos,      "XPOS",     2, kw_kind::function)    \
    X(ypos,      "YPOS",     1, kw_kind::function)    \
    X(crash,     "CRASH",    2, kw_kind::function)    \
    X(vct,       "VCT",      2, kw_kind::function)    \
    X(erl,       "ERL",      0, kw_kind::function)    \
    X(err,       "ERR",      0, kw_kind::function)    \
    /* string functions (names end with $) */         \
    X(chr_s,     "CHR$",     2, kw_kind::function)    \
    X(str_s,     "STR$",     3, kw_kind::function)    \
    X(hex_s,     "HEX$",     1, kw_kind::function)    \
    X(left_s,    "LEFT$",    3, kw_kind::function)    \
    X(right_s,   "RIGHT$",   2, kw_kind::function)    \
    X(mid_s,     "MID$",     2, kw_kind::function)    \
    X(inkey_s,   "INKEY$",   3, kw_kind::function)    \
    X(scr_s,     "SCR$",     2, kw_kind::function)

/// What a keyword may appear as, used by the loader and the decruncher.
enum class kw_kind : uint8_t {
    statement,    ///< can start a statement
    modifier,     ///< only valid inside another statement (TO, STEP, THEN, OFF)
    operator_kw,  ///< word operator (MOD, AND, ...)
    function,     ///< only valid inside an expression
};

/// Token byte values. Values below 0x20 are structural, 0x80 and up keywords.
enum class token : uint8_t {
    eol = 0x00,      ///< end of line
    number = 0x01,   ///< + 2 bytes, little endian
    string = 0x02,   ///< + length byte + characters
    var_num = 0x03,  ///< + 2 name bytes
    var_str = 0x04,  ///< + 2 name bytes
    raw = 0x05,      ///< + length byte + bytes (REM / DATA payload)
    le = 0x10,       ///< <=
    ge = 0x11,       ///< >=
    ne = 0x12,       ///< <>

    // Single character tokens keep their ASCII value (see the file comment).
    plus = '+',
    minus = '-',
    star = '*',
    slash = '/',
    lparen = '(',
    rparen = ')',
    comma = ',',
    semicolon = ';',
    colon = ':',
    equal = '=',
    less = '<',
    greater = '>',

    keyword_first = 0x80,
#define FMRB_BASIC_TOKEN_ENUM(id, name, abbrev, kind) id,
    FMRB_BASIC_KEYWORDS(FMRB_BASIC_TOKEN_ENUM)
#undef FMRB_BASIC_TOKEN_ENUM
        keyword_last,
};

/// One keyword table row.
struct keyword_entry {
    const char* name;
    uint8_t abbrev_len;  ///< characters before the '.' in the short form, 0 = none
    kw_kind kind;
    token tk;
};

inline constexpr keyword_entry keyword_table[] = {
#define FMRB_BASIC_TOKEN_ROW(id, name, abbrev, kind) {name, abbrev, kind, token::id},
    FMRB_BASIC_KEYWORDS(FMRB_BASIC_TOKEN_ROW)
#undef FMRB_BASIC_TOKEN_ROW
};

inline constexpr size_t keyword_count = sizeof(keyword_table) / sizeof(keyword_table[0]);

// The encoding gives keywords the byte range 0x80 - 0xFE.
static_assert(static_cast<unsigned>(token::keyword_last) <= 0xFF,
              "keyword tokens no longer fit in one byte");

/// True when @p tk is a keyword token.
constexpr bool is_keyword(token tk) noexcept {
    return static_cast<uint8_t>(tk) > static_cast<uint8_t>(token::keyword_first) &&
           static_cast<uint8_t>(tk) < static_cast<uint8_t>(token::keyword_last);
}

/// Keyword table row for @p tk, or nullptr when it is not a keyword.
const keyword_entry* keyword_info(token tk) noexcept;

}  // namespace fmrb_basic
