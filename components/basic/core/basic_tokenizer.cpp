// Crunching (source line -> token bytes) and decrunching (token bytes ->
// source text). Input is already in Family BASIC character codes.

#include "basic_core.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

/**
 * Longest keyword match at @p src + @p pos.
 *
 * Two forms are accepted: the full keyword, and the documented abbreviation
 * (the first `abbrev_len` characters followed by '.'). Keywords are matched
 * without requiring a word boundary, which is what lets "FORI=1TO9" work and
 * is why core_spec asks for a space between a variable and a word operator.
 */
bool match_keyword(const uint8_t* src, size_t len, size_t pos, token* out_tk,
                   size_t* out_consumed) noexcept {
    size_t best = 0;
    token best_tk = token::eol;

    for (size_t i = 0; i < keyword_count; ++i) {
        const keyword_entry& kw = keyword_table[i];

        size_t n = 0;
        while (kw.name[n] != '\0' && pos + n < len &&
               src[pos + n] == static_cast<uint8_t>(kw.name[n])) {
            ++n;
        }
        if (kw.name[n] == '\0' && n > best) {
            best = n;
            best_tk = kw.tk;
            continue;
        }
        // Abbreviation: "PA." for PAUSE. The first keyword in table order wins
        // when two keywords share an abbreviation (see the B1 report).
        if (kw.abbrev_len != 0 && n >= kw.abbrev_len && pos + kw.abbrev_len < len &&
            src[pos + kw.abbrev_len] == '.') {
            const size_t consumed = static_cast<size_t>(kw.abbrev_len) + 1;
            if (consumed > best) {
                best = consumed;
                best_tk = kw.tk;
            }
        }
    }

    if (best == 0) {
        return false;
    }
    *out_tk = best_tk;
    *out_consumed = best;
    return true;
}

}  // namespace

int32_t interpreter::crunch_line(const uint8_t* src, size_t len, uint8_t* out,
                                 size_t out_size) noexcept {
    size_t pos = 0;
    size_t outp = 0;

    auto emit = [&](uint8_t b) noexcept -> bool {
        if (outp >= out_size) {
            return false;
        }
        out[outp++] = b;
        return true;
    };

    while (pos < len) {
        const uint8_t c = src[pos];

        if (c == ' ' || c == '\t') {
            ++pos;
            continue;
        }

        // String literal. A missing closing quote at end of line is allowed
        // (core_spec sec 6).
        if (c == '"') {
            ++pos;
            const size_t start = pos;
            while (pos < len && src[pos] != '"') {
                ++pos;
            }
            const size_t slen = pos - start;
            if (pos < len) {
                ++pos;  // closing quote
            }
            if (slen > 255 || !emit(static_cast<uint8_t>(token::string)) ||
                !emit(static_cast<uint8_t>(slen))) {
                raise(error_code::out_of_memory, -1);
                return -1;
            }
            for (size_t i = 0; i < slen; ++i) {
                if (!emit(src[start + i])) {
                    raise(error_code::out_of_memory, -1);
                    return -1;
                }
            }
            continue;
        }

        // Numeric literal, decimal or &H hex (core_spec sec 2).
        if (is_digit(c) || (c == '&' && pos + 1 < len && src[pos + 1] == 'H')) {
            uint32_t value = 0;
            const bool hex = (c == '&');
            if (c == '&') {
                pos += 2;
                if (pos >= len || !is_hex_digit(src[pos])) {
                    raise(error_code::syntax, -1);
                    return -1;
                }
                while (pos < len && is_hex_digit(src[pos])) {
                    const uint8_t d = src[pos];
                    value = value * 16 + static_cast<uint32_t>(is_digit(d) ? d - '0'
                                                                          : d - 'A' + 10);
                    if (value > 0xFFFF) {
                        raise(error_code::overflow, -1);
                        return -1;
                    }
                    ++pos;
                }
            } else {
                while (pos < len && is_digit(src[pos])) {
                    value = value * 10 + static_cast<uint32_t>(src[pos] - '0');
                    if (value > 0xFFFF) {
                        // Line numbers reach 65535, expressions -32768..32767.
                        raise(error_code::overflow, -1);
                        return -1;
                    }
                    ++pos;
                }
            }
            // The base is kept in the token so LIST and SAVE can write the
            // literal the way it was typed; the value bytes are the same.
            if (!emit(static_cast<uint8_t>(hex ? token::number_hex : token::number)) ||
                !emit(static_cast<uint8_t>(value & 0xFF)) ||
                !emit(static_cast<uint8_t>((value >> 8) & 0xFF))) {
                raise(error_code::out_of_memory, -1);
                return -1;
            }
            continue;
        }

        // "?" is PRINT, "'" is REM (core_spec sec 4).
        token tk = token::eol;
        size_t consumed = 0;
        if (c == '?') {
            tk = token::print;
            consumed = 1;
        } else if (c == '\'') {
            tk = token::rem;
            consumed = 1;
        } else if (c == '_' && match_keyword(src, len, pos, &tk, &consumed)) {
            // Debug statements start with '_', which no BASIC name can.
        } else if (is_alpha(c) && !match_keyword(src, len, pos, &tk, &consumed)) {
            // Variable: first two characters are significant (core_spec sec 2).
            const size_t start = pos;
            ++pos;
            while (pos < len && is_alnum(src[pos])) {
                token dummy_tk = token::eol;
                size_t dummy_len = 0;
                if (match_keyword(src, len, pos, &dummy_tk, &dummy_len)) {
                    break;
                }
                ++pos;
            }
            const bool is_string_var = (pos < len && src[pos] == '$');
            if (is_string_var) {
                ++pos;
            }
            const uint8_t n0 = src[start];
            const uint8_t n1 = (pos - start > 1 && is_alnum(src[start + 1])) ? src[start + 1] : 0;
            if (!emit(static_cast<uint8_t>(is_string_var ? token::var_str : token::var_num)) ||
                !emit(n0) || !emit(n1)) {
                raise(error_code::out_of_memory, -1);
                return -1;
            }
            continue;
        } else if (!is_alpha(c)) {
            // Operators and punctuation keep their character code, except the
            // two character relational operators.
            if (c == '<' && pos + 1 < len && src[pos + 1] == '=') {
                tk = token::le;
                consumed = 2;
            } else if (c == '<' && pos + 1 < len && src[pos + 1] == '>') {
                tk = token::ne;
                consumed = 2;
            } else if (c == '>' && pos + 1 < len && src[pos + 1] == '=') {
                tk = token::ge;
                consumed = 2;
            } else if (c >= 0x20 && c < 0x7F) {
                if (!emit(c)) {
                    raise(error_code::out_of_memory, -1);
                    return -1;
                }
                ++pos;
                continue;
            } else {
                raise(error_code::syntax, -1);
                return -1;
            }
        }

        pos += consumed;
        if (!emit(static_cast<uint8_t>(tk))) {
            raise(error_code::out_of_memory, -1);
            return -1;
        }

        // REM and DATA payloads are not expressions: keep them verbatim.
        // REM runs to end of line, DATA to the next ':' outside quotes
        // (core_spec sec 6 requires quotes around data containing ':').
        if (tk == token::rem || tk == token::data) {
            const size_t start = pos;
            bool in_quotes = false;
            while (pos < len) {
                if (src[pos] == '"') {
                    in_quotes = !in_quotes;
                } else if (src[pos] == ':' && !in_quotes && tk == token::data) {
                    break;
                }
                ++pos;
            }
            const size_t rlen = pos - start;
            if (rlen > 255 || !emit(static_cast<uint8_t>(token::raw)) ||
                !emit(static_cast<uint8_t>(rlen))) {
                raise(error_code::out_of_memory, -1);
                return -1;
            }
            for (size_t i = 0; i < rlen; ++i) {
                if (!emit(src[start + i])) {
                    raise(error_code::out_of_memory, -1);
                    return -1;
                }
            }
        }
    }

    if (!emit(static_cast<uint8_t>(token::eol))) {
        raise(error_code::out_of_memory, -1);
        return -1;
    }
    return static_cast<int32_t>(outp);
}

size_t interpreter::decrunch_line(uint16_t index, char* out, size_t out_size) const noexcept {
    if (index >= line_count_ || out_size == 0) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    size_t outp = 0;
    auto put = [&](char ch) noexcept {
        if (outp + 1 < out_size) {
            out[outp++] = ch;
        }
    };
    auto put_num = [&](uint32_t value) noexcept {
        char digits[6];
        size_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value != 0 && n < sizeof(digits));
        while (n > 0) {
            put(digits[--n]);
        }
    };

    auto put_hex = [&](uint16_t value) noexcept {
        // Upper case, no leading zeros, which is how the spec writes them.
        char digits[4];
        size_t n = 0;
        do {
            const uint8_t d = static_cast<uint8_t>(value & 0xF);
            digits[n++] = static_cast<char>(d < 10 ? '0' + d : 'A' + (d - 10));
            value = static_cast<uint16_t>(value >> 4);
        } while (value != 0 && n < sizeof(digits));
        while (n > 0) {
            put(digits[--n]);
        }
    };

    put_num(lines_[index].number);
    put(' ');

    const uint8_t* code = code_ + lines_[index].offset;
    const uint16_t length = lines_[index].length;
    uint16_t at = 0;
    while (at < length) {
        const token tk = static_cast<token>(code[at]);
        if (tk == token::eol) {
            break;
        }
        switch (tk) {
            case token::number_hex: {
                const uint16_t raw =
                    static_cast<uint16_t>(code[at + 1] | (code[at + 2] << 8));
                put('&');
                put('H');
                put_hex(raw);
                at += 3;
                break;
            }
            case token::number: {
                const uint16_t raw =
                    static_cast<uint16_t>(code[at + 1] | (code[at + 2] << 8));
                const int16_t value = static_cast<int16_t>(raw);
                if (value < 0) {
                    put('-');
                    put_num(static_cast<uint32_t>(-static_cast<int32_t>(value)));
                } else {
                    put_num(static_cast<uint32_t>(value));
                }
                at += 3;
                break;
            }
            case token::string: {
                const uint8_t slen = code[at + 1];
                put('"');
                for (uint8_t i = 0; i < slen; ++i) {
                    put(static_cast<char>(code[at + 2 + i]));
                }
                put('"');
                at += 2 + slen;
                break;
            }
            case token::raw: {
                const uint8_t rlen = code[at + 1];
                for (uint8_t i = 0; i < rlen; ++i) {
                    put(static_cast<char>(code[at + 2 + i]));
                }
                at += 2 + rlen;
                break;
            }
            case token::var_num:
            case token::var_str: {
                put(static_cast<char>(code[at + 1]));
                if (code[at + 2] != 0) {
                    put(static_cast<char>(code[at + 2]));
                }
                if (tk == token::var_str) {
                    put('$');
                }
                at += 3;
                break;
            }
            case token::le:
                put('<');
                put('=');
                ++at;
                break;
            case token::ge:
                put('>');
                put('=');
                ++at;
                break;
            case token::ne:
                put('<');
                put('>');
                ++at;
                break;
            default: {
                if (is_keyword(tk)) {
                    // LOAD? is LOAD followed by PRINT: '?' is PRINT's shorthand
                    // and crunches to that token. Printing "LOAD PRINT" back
                    // would not reload, so put the '?' where it belongs.
                    if (tk == token::print && at > 0 &&
                        static_cast<token>(code[at - 1]) == token::load) {
                        // The trailing space after LOAD is already out; step on
                        // it so the '?' sits against the keyword.
                        if (outp > 0 && out[outp - 1] == ' ') {
                            --outp;
                        }
                        put('?');
                        ++at;
                        break;
                    }
                    const keyword_entry* kw = keyword_info(tk);
                    if (kw) {
                        for (const char* p = kw->name; *p != '\0'; ++p) {
                            put(*p);
                        }
                        put(' ');
                    }
                } else {
                    put(static_cast<char>(code[at]));
                }
                ++at;
                break;
            }
        }
    }

    out[outp] = '\0';
    return outp;
}

}  // namespace fmrb_basic
