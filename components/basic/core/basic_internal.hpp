/**
 * @file basic_internal.hpp
 * @brief Helpers shared between the interpreter core translation units
 *
 * Implementation detail: not part of the interface the embedder uses.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "basic_core.hpp"

namespace fmrb_basic {

/// Statements executed between two basic_host_t::on_tick calls.
inline constexpr uint32_t tick_interval = 32;

/// Highest nesting the expression evaluator accepts before raising FT.
inline constexpr uint8_t max_expr_nesting = 16;

constexpr bool is_digit(uint8_t c) noexcept {
    return c >= '0' && c <= '9';
}

constexpr bool is_alpha(uint8_t c) noexcept {
    return c >= 'A' && c <= 'Z';
}

constexpr bool is_alnum(uint8_t c) noexcept {
    return is_alpha(c) || is_digit(c);
}

constexpr bool is_hex_digit(uint8_t c) noexcept {
    return is_digit(c) || (c >= 'A' && c <= 'F');
}

/// Operator precedence, higher binds tighter (core_spec sec 3).
enum class precedence : uint8_t {
    none = 0,
    xor_op = 1,
    or_op = 2,
    and_op = 3,
    not_op = 4,
    relational = 5,
    additive = 6,
    multiplicative = 7,
    unary = 8,
};

/// Precedence of a binary operator token, precedence::none when not one.
constexpr precedence binary_precedence(token tk) noexcept {
    switch (tk) {
        case token::xor_:
            return precedence::xor_op;
        case token::or_:
            return precedence::or_op;
        case token::and_:
            return precedence::and_op;
        case token::equal:
        case token::less:
        case token::greater:
        case token::le:
        case token::ge:
        case token::ne:
            return precedence::relational;
        case token::plus:
        case token::minus:
            return precedence::additive;
        case token::star:
        case token::slash:
        case token::mod:
            return precedence::multiplicative;
        default:
            return precedence::none;
    }
}

/// Copy a Family BASIC string into a value, raising ST when too long.
inline bool set_string(basic_value* v, const uint8_t* src, size_t len) noexcept {
    v->is_string = true;
    v->num = 0;
    if (len > max_string_len) {
        return false;
    }
    v->len = static_cast<uint8_t>(len);
    for (size_t i = 0; i < len; ++i) {
        v->str[i] = src[i];
    }
    return true;
}

inline void set_number(basic_value* v, int16_t n) noexcept {
    v->is_string = false;
    v->num = n;
    v->len = 0;
}

}  // namespace fmrb_basic
