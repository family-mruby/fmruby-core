// Expression evaluation and the built in functions.
//
// The evaluator is iterative on purpose (00_common memory design rule 1): an
// operand stack and an operator stack held by the interpreter replace the
// usual recursive descent, so BASIC expression nesting never turns into C
// call depth. Nesting beyond max_expr_nesting raises FT.

#include "basic_core.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

/// Operator stack entry. Markers keep track of what a ')' has to close.
struct op_entry {
    token op;       ///< operator token, or the token that opened the marker
    uint8_t kind;   ///< see marker kinds below
    uint8_t argc;   ///< arguments seen so far (function / array markers)
    uint8_t name0;  ///< array name, first significant character
    uint8_t name1;  ///< array name, second significant character
};

constexpr uint8_t op_binary = 0;
constexpr uint8_t op_unary = 1;
constexpr uint8_t mark_paren = 2;
constexpr uint8_t mark_func = 3;
constexpr uint8_t mark_array = 4;

constexpr bool is_marker(uint8_t kind) noexcept {
    return kind >= mark_paren;
}

/// Binding strength of a stacked operator. Unary minus binds tighter than any
/// binary operator; NOT sits between the comparisons and AND (core_spec sec 3).
constexpr precedence entry_precedence(const op_entry& e) noexcept {
    if (e.kind == op_unary) {
        return (e.op == token::not_) ? precedence::not_op : precedence::unary;
    }
    return binary_precedence(e.op);
}

/// Family BASIC truth values: true is -1, false is 0 (core_spec sec 2).
constexpr int16_t bool_value(bool b) noexcept {
    return b ? static_cast<int16_t>(-1) : static_cast<int16_t>(0);
}

int compare_strings(const basic_value& a, const basic_value& b) noexcept {
    const uint8_t n = (a.len < b.len) ? a.len : b.len;
    for (uint8_t i = 0; i < n; ++i) {
        if (a.str[i] != b.str[i]) {
            return (a.str[i] < b.str[i]) ? -1 : 1;
        }
    }
    if (a.len == b.len) {
        return 0;
    }
    return (a.len < b.len) ? -1 : 1;
}

/// Decimal text of @p value into @p out (max 7 bytes), returns the length.
uint8_t number_to_text(int16_t value, uint8_t* out) noexcept {
    uint8_t n = 0;
    uint16_t magnitude = (value < 0) ? static_cast<uint16_t>(-static_cast<int32_t>(value))
                                     : static_cast<uint16_t>(value);
    uint8_t digits[5];
    uint8_t d = 0;
    do {
        digits[d++] = static_cast<uint8_t>('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0 && d < sizeof(digits));
    if (value < 0) {
        out[n++] = '-';
    }
    while (d > 0) {
        out[n++] = digits[--d];
    }
    return n;
}

}  // namespace

bool interpreter::apply_unary(token op, basic_value* v) noexcept {
    if (v->is_string) {
        return raise_here(error_code::type_mismatch);
    }
    switch (op) {
        case token::minus:
            v->num = static_cast<int16_t>(-static_cast<int32_t>(v->num));
            break;
        case token::not_:
            v->num = static_cast<int16_t>(~static_cast<uint16_t>(v->num));
            break;
        default:
            break;
    }
    return true;
}

bool interpreter::apply_binary(token op, basic_value* lhs, const basic_value* rhs) noexcept {
    // Strings support concatenation and comparison only.
    if (lhs->is_string || rhs->is_string) {
        if (lhs->is_string != rhs->is_string) {
            return raise_here(error_code::type_mismatch);
        }
        if (op == token::plus) {
            const uint16_t total = static_cast<uint16_t>(lhs->len) + rhs->len;
            if (total > max_string_len) {
                return raise_here(error_code::string_too_long);
            }
            for (uint8_t i = 0; i < rhs->len; ++i) {
                lhs->str[lhs->len + i] = rhs->str[i];
            }
            lhs->len = static_cast<uint8_t>(total);
            return true;
        }
        const int cmp = compare_strings(*lhs, *rhs);
        bool result = false;
        switch (op) {
            case token::equal: result = (cmp == 0); break;
            case token::ne: result = (cmp != 0); break;
            case token::less: result = (cmp < 0); break;
            case token::greater: result = (cmp > 0); break;
            case token::le: result = (cmp <= 0); break;
            case token::ge: result = (cmp >= 0); break;
            default:
                return raise_here(error_code::type_mismatch);
        }
        set_number(lhs, bool_value(result));
        return true;
    }

    const int32_t a = lhs->num;
    const int32_t b = rhs->num;
    int32_t result = 0;
    switch (op) {
        case token::plus: result = a + b; break;
        case token::minus: result = a - b; break;
        case token::star: result = a * b; break;
        case token::slash:
            if (b == 0) {
                return raise_here(error_code::division_by_zero);
            }
            result = a / b;  // integer division, truncated (core_spec sec 2)
            break;
        case token::mod:
            if (b == 0) {
                return raise_here(error_code::division_by_zero);
            }
            result = a % b;
            break;
        case token::equal: result = bool_value(a == b); break;
        case token::ne: result = bool_value(a != b); break;
        case token::less: result = bool_value(a < b); break;
        case token::greater: result = bool_value(a > b); break;
        case token::le: result = bool_value(a <= b); break;
        case token::ge: result = bool_value(a >= b); break;
        case token::and_:
            result = static_cast<int16_t>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
            break;
        case token::or_:
            result = static_cast<int16_t>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
            break;
        case token::xor_:
            result = static_cast<int16_t>(static_cast<uint16_t>(a) ^ static_cast<uint16_t>(b));
            break;
        default:
            return raise_here(error_code::syntax);
    }
    // Arithmetic wraps to 16 bits: core_spec sec 1 leaves overflow undefined,
    // so the cheapest deterministic behaviour is two's complement wrapping.
    set_number(lhs, static_cast<int16_t>(static_cast<uint16_t>(result & 0xFFFF)));
    return true;
}

bool interpreter::call_builtin(token fn, basic_value* args, uint8_t argc,
                               basic_value* out) noexcept {
    auto need_num = [&](uint8_t i, int16_t* v) noexcept -> bool {
        if (i >= argc || args[i].is_string) {
            raise_here(i >= argc ? error_code::missing_operand : error_code::type_mismatch);
            return false;
        }
        *v = args[i].num;
        return true;
    };
    auto need_str = [&](uint8_t i, const basic_value** v) noexcept -> bool {
        if (i >= argc || !args[i].is_string) {
            raise_here(i >= argc ? error_code::missing_operand : error_code::type_mismatch);
            return false;
        }
        *v = &args[i];
        return true;
    };

    switch (fn) {
        case token::abs: {
            int16_t x = 0;
            if (!need_num(0, &x)) return false;
            set_number(out, static_cast<int16_t>(x < 0 ? -static_cast<int32_t>(x) : x));
            return true;
        }
        case token::sgn: {
            int16_t x = 0;
            if (!need_num(0, &x)) return false;
            set_number(out, static_cast<int16_t>((x > 0) ? 1 : ((x < 0) ? -1 : 0)));
            return true;
        }
        case token::rnd: {
            int16_t x = 0;
            if (!need_num(0, &x)) return false;
            if (x <= 0) {
                return raise_here(error_code::illegal_function_call);
            }
            // xorshift32; seeded fixed in init() so runs are reproducible.
            rng_state_ ^= rng_state_ << 13;
            rng_state_ ^= rng_state_ >> 17;
            rng_state_ ^= rng_state_ << 5;
            set_number(out, static_cast<int16_t>((rng_state_ >> 8) % static_cast<uint32_t>(x)));
            return true;
        }
        case token::fre: {
            const uint32_t free_now = free_bytes();
            set_number(out, static_cast<int16_t>(free_now > 32767u ? 32767 : free_now));
            return true;
        }
        case token::peek: {
            int16_t addr = 0;
            if (!need_num(0, &addr)) return false;
            // Virtual memory map arrives in B4; until then every address reads 0.
            set_number(out, 0);
            return true;
        }
        case token::asc: {
            const basic_value* s = nullptr;
            if (!need_str(0, &s)) return false;
            set_number(out, static_cast<int16_t>(s->len > 0 ? s->str[0] : 0));
            return true;
        }
        case token::len: {
            const basic_value* s = nullptr;
            if (!need_str(0, &s)) return false;
            set_number(out, static_cast<int16_t>(s->len));
            return true;
        }
        case token::val: {
            const basic_value* s = nullptr;
            if (!need_str(0, &s)) return false;
            uint8_t i = 0;
            bool negative = false;
            if (i < s->len && (s->str[i] == '+' || s->str[i] == '-')) {
                negative = (s->str[i] == '-');
                ++i;
            }
            int32_t value = 0;
            if (i + 1 < s->len && s->str[i] == '&' && s->str[i + 1] == 'H') {
                i += 2;
                while (i < s->len && is_hex_digit(s->str[i])) {
                    const uint8_t d = s->str[i];
                    value = value * 16 + (is_digit(d) ? d - '0' : d - 'A' + 10);
                    if (value > 0xFFFF) {
                        return raise_here(error_code::overflow);
                    }
                    ++i;
                }
                set_number(out, static_cast<int16_t>(static_cast<uint16_t>(value)));
                return true;
            }
            while (i < s->len && is_digit(s->str[i])) {
                value = value * 10 + (s->str[i] - '0');
                if (value > 32768) {
                    return raise_here(error_code::overflow);
                }
                ++i;
            }
            if (negative) {
                value = -value;
            }
            if (value > 32767 || value < -32768) {
                return raise_here(error_code::overflow);
            }
            set_number(out, static_cast<int16_t>(value));
            return true;
        }
        case token::chr_s: {
            int16_t x = 0;
            if (!need_num(0, &x)) return false;
            if (x < 0 || x > 255) {
                return raise_here(error_code::illegal_function_call);
            }
            const uint8_t code = static_cast<uint8_t>(x);
            set_string(out, &code, 1);
            return true;
        }
        case token::str_s: {
            int16_t x = 0;
            if (!need_num(0, &x)) return false;
            uint8_t buf[8];
            uint8_t n = 0;
            if (x >= 0) {
                buf[n++] = ' ';  // positive numbers keep the sign column
            }
            n = static_cast<uint8_t>(n + number_to_text(x, buf + n));
            set_string(out, buf, n);
            return true;
        }
        case token::hex_s: {
            int16_t x = 0;
            if (!need_num(0, &x)) return false;
            const uint16_t raw = static_cast<uint16_t>(x);
            uint8_t buf[4];
            uint8_t n = 0;
            bool started = false;
            for (int shift = 12; shift >= 0; shift -= 4) {
                const uint8_t nibble = static_cast<uint8_t>((raw >> shift) & 0x0F);
                if (nibble != 0 || started || shift == 0) {
                    buf[n++] = static_cast<uint8_t>(nibble < 10 ? '0' + nibble
                                                               : 'A' + nibble - 10);
                    started = true;
                }
            }
            set_string(out, buf, n);
            return true;
        }
        case token::left_s:
        case token::right_s: {
            const basic_value* s = nullptr;
            int16_t n = 0;
            if (!need_str(0, &s) || !need_num(1, &n)) return false;
            if (n < 0 || n > 255) {
                return raise_here(error_code::illegal_function_call);
            }
            uint8_t take = (n > s->len) ? s->len : static_cast<uint8_t>(n);
            const uint8_t* from = (fn == token::left_s) ? s->str : s->str + (s->len - take);
            set_string(out, from, take);
            return true;
        }
        case token::mid_s: {
            const basic_value* s = nullptr;
            int16_t start = 0;
            if (!need_str(0, &s) || !need_num(1, &start)) return false;
            if (start < 1 || start > 255) {
                return raise_here(error_code::illegal_function_call);
            }
            int16_t count = 255;
            if (argc >= 3) {
                if (!need_num(2, &count)) return false;
                if (count < 0 || count > 255) {
                    return raise_here(error_code::illegal_function_call);
                }
            }
            if (start > s->len) {
                set_string(out, s->str, 0);
                return true;
            }
            const uint8_t offset = static_cast<uint8_t>(start - 1);
            uint8_t take = static_cast<uint8_t>(s->len - offset);
            if (count < take) {
                take = static_cast<uint8_t>(count);
            }
            set_string(out, s->str + offset, take);
            return true;
        }
        case token::instr: {
            // INSTR([P,]s1,s2) (v3_spec)
            uint8_t base = 0;
            int16_t from = 1;
            if (argc >= 3) {
                if (!need_num(0, &from)) return false;
                base = 1;
            }
            const basic_value* hay = nullptr;
            const basic_value* needle = nullptr;
            if (!need_str(base, &hay) || !need_str(static_cast<uint8_t>(base + 1), &needle)) {
                return false;
            }
            if (from < 1) {
                return raise_here(error_code::illegal_function_call);
            }
            set_number(out, 0);
            if (needle->len == 0 || needle->len > hay->len) {
                return true;
            }
            for (uint8_t i = static_cast<uint8_t>(from - 1);
                 i + needle->len <= hay->len; ++i) {
                uint8_t k = 0;
                while (k < needle->len && hay->str[i + k] == needle->str[k]) {
                    ++k;
                }
                if (k == needle->len) {
                    set_number(out, static_cast<int16_t>(i + 1));
                    return true;
                }
            }
            return true;
        }
        case token::pos:
            set_number(out, static_cast<int16_t>(cursor_x_));
            return true;
        case token::csrlin:
            set_number(out, static_cast<int16_t>(cursor_y_));
            return true;
        case token::erl:
            set_number(out, static_cast<int16_t>(error_line_ >= 0 ? error_line_ : 0));
            return true;
        case token::err:
            set_number(out, static_cast<int16_t>(error_ == error_code::none
                                                     ? 0
                                                     : static_cast<int32_t>(error_)));
            return true;
        case token::inkey_s: {
            uint8_t code = 0;
            if (next_key(&code)) {
                set_string(out, &code, 1);
                return true;
            }
            // INKEY$(0) blocks until a key arrives (core_spec sec 11). A host
            // without clock control (the test runner) cannot deliver one, so
            // it returns empty instead of hanging.
            const bool blocking = (argc >= 1 && !args[0].is_string && args[0].num == 0);
            if (blocking && host_.sleep_ms) {
                while (!next_key(&code)) {
                    if (host_.on_tick && !host_.on_tick(host_.user)) {
                        running_ = false;
                        break;
                    }
                    host_.sleep_ms(host_.user, 10);
                }
                if (code != 0) {
                    set_string(out, &code, 1);
                    return true;
                }
            }
            set_string(out, nullptr, 0);
            return true;
        }
        case token::scr_s: {
            int16_t x = 0;
            int16_t y = 0;
            if (!need_num(0, &x) || !need_num(1, &y)) {
                return false;
            }
            if (x < 0 || x >= screen_columns || y < 0 || y >= screen_rows) {
                return raise_here(error_code::illegal_function_call);
            }
            int16_t want_color = 0;
            if (argc >= 3 && !need_num(2, &want_color)) {
                return false;
            }
            // Sw=1 returns the colour attribute. The spec says SCR$ returns a
            // string in both cases, so the attribute comes back as a one
            // character string holding the value 0-3 (see the B2 report).
            const uint8_t value = (want_color != 0)
                                      ? screen_attr(static_cast<uint8_t>(x),
                                                    static_cast<uint8_t>(y))
                                      : screen_char(static_cast<uint8_t>(x),
                                                    static_cast<uint8_t>(y));
            set_string(out, &value, 1);
            return true;
        }
        case token::xpos:
        case token::ypos:
        case token::vct:
        case token::move:
        case token::crash: {
            int16_t slot = 0;
            if (!need_num(0, &slot)) {
                return false;
            }
            if (slot < 0 || slot >= move_count) {
                return raise_here(error_code::illegal_function_call);
            }
            const basic_move_state& mv = moves_[slot];
            switch (fn) {
                case token::xpos: set_number(out, mv.x); break;
                case token::ypos: set_number(out, mv.y); break;
                // VCT reports the direction while moving, 0 when stopped (v3).
                case token::vct:
                    set_number(out, static_cast<int16_t>(mv.active ? mv.direction : 0));
                    break;
                // MOVE(n): -1 while travelling, 0 once it has arrived.
                case token::move: set_number(out, mv.active ? -1 : 0); break;
                default: set_number(out, move_crash(static_cast<uint8_t>(slot))); break;
            }
            return true;
        }
        case token::stick:
        case token::strig: {
            int16_t player = 0;
            if (!need_num(0, &player)) {
                return false;
            }
            if (player < 0 || player > 1) {
                return raise_here(error_code::illegal_function_call);
            }
            set_number(out, static_cast<int16_t>(fn == token::stick
                                                     ? pad_stick_[player]
                                                     : pad_trigger_[player]));
            return true;
        }
        default:
            return raise_here(error_code::undefined_function);
    }
}

bool interpreter::eval(basic_value* out) noexcept {
    op_entry ops[max_expr_nesting];
    uint8_t op_top = 0;
    uint8_t sp = 0;
    uint8_t markers = 0;
    bool want_operand = true;

    auto push_value = [&](const basic_value& v) noexcept -> bool {
        if (sp >= expr_depth_) {
            return raise_here(error_code::formula_too_complex);
        }
        operand_stack_[sp++] = v;
        return true;
    };

    auto apply_top = [&]() noexcept -> bool {
        const op_entry e = ops[--op_top];
        if (e.kind == op_unary) {
            if (sp < 1) {
                return raise_here(error_code::missing_operand);
            }
            return apply_unary(e.op, &operand_stack_[sp - 1]);
        }
        if (sp < 2) {
            return raise_here(error_code::missing_operand);
        }
        --sp;
        return apply_binary(e.op, &operand_stack_[sp - 1], &operand_stack_[sp]);
    };

    while (true) {
        const token tk = peek_token();

        if (want_operand) {
            if (tk == token::minus || tk == token::not_) {
                if (op_top >= max_expr_nesting) {
                    return raise_here(error_code::formula_too_complex);
                }
                read_byte();
                ops[op_top++] = {tk, op_unary, 0, 0, 0};
                continue;
            }
            if (tk == token::plus) {
                read_byte();
                continue;
            }
            if (tk == token::lparen) {
                if (op_top >= max_expr_nesting) {
                    return raise_here(error_code::formula_too_complex);
                }
                read_byte();
                ops[op_top++] = {tk, mark_paren, 0, 0, 0};
                ++markers;
                continue;
            }
            if (tk == token::number) {
                read_byte();
                const uint8_t lo = read_byte();
                const uint8_t hi = read_byte();
                basic_value v;
                set_number(&v, static_cast<int16_t>(static_cast<uint16_t>(lo) |
                                                    (static_cast<uint16_t>(hi) << 8)));
                if (!push_value(v)) {
                    return false;
                }
                want_operand = false;
                continue;
            }
            if (tk == token::string) {
                read_byte();
                const uint8_t slen = read_byte();
                basic_value v;
                v.is_string = true;
                v.num = 0;
                v.len = 0;
                for (uint8_t i = 0; i < slen; ++i) {
                    const uint8_t ch = read_byte();
                    if (i < max_string_len) {
                        v.str[i] = ch;
                        v.len = static_cast<uint8_t>(i + 1);
                    }
                }
                if (slen > max_string_len) {
                    return raise_here(error_code::string_too_long);
                }
                if (!push_value(v)) {
                    return false;
                }
                want_operand = false;
                continue;
            }
            if (tk == token::var_num || tk == token::var_str) {
                read_byte();
                const uint8_t n0 = read_byte();
                const uint8_t n1 = read_byte();
                const bool is_string = (tk == token::var_str);
                if (peek_token() == token::lparen) {
                    if (op_top >= max_expr_nesting) {
                        return raise_here(error_code::formula_too_complex);
                    }
                    read_byte();
                    ops[op_top++] = {tk, mark_array, 1, n0, n1};
                    ++markers;
                    continue;
                }
                basic_var* var = find_var(n0, n1, is_string, false);
                basic_value v;
                if (var) {
                    load_value(var_data_ + var->offset, is_string, &v);
                } else if (is_string) {
                    set_string(&v, nullptr, 0);
                } else {
                    set_number(&v, 0);
                }
                if (!push_value(v)) {
                    return false;
                }
                want_operand = false;
                continue;
            }
            if (is_keyword(tk)) {
                const keyword_entry* kw = keyword_info(tk);
                // MOVE is both a statement and a function: "MOVE(n)" inside an
                // expression asks whether that character is still travelling
                // (core_spec sec 9).
                const bool as_function =
                    kw && (kw->kind == kw_kind::function ||
                           (kw->tk == token::move && peek_ahead_is_lparen()));
                if (as_function) {
                    read_byte();
                    if (peek_token() == token::lparen) {
                        if (op_top >= max_expr_nesting) {
                            return raise_here(error_code::formula_too_complex);
                        }
                        read_byte();
                        ops[op_top++] = {tk, mark_func, 1, 0, 0};
                        ++markers;
                        continue;
                    }
                    basic_value v;
                    if (!call_builtin(tk, nullptr, 0, &v) || !push_value(v)) {
                        return false;
                    }
                    want_operand = false;
                    continue;
                }
            }
            // Something that cannot start an operand: a plain syntax error.
            // MO is reserved for statements and functions that are missing a
            // required parameter.
            return raise_here(error_code::syntax);
        }

        // Expecting an operator, a separator or the end of the expression.
        const precedence prec = binary_precedence(tk);
        if (prec != precedence::none) {
            // Left associative: pop everything that binds at least as tightly.
            while (op_top > 0 && !is_marker(ops[op_top - 1].kind) &&
                   entry_precedence(ops[op_top - 1]) >= prec) {
                if (!apply_top()) {
                    return false;
                }
            }
            if (op_top >= max_expr_nesting) {
                return raise_here(error_code::formula_too_complex);
            }
            read_byte();
            ops[op_top++] = {tk, op_binary, 0, 0, 0};
            want_operand = true;
            continue;
        }

        if (tk == token::comma && markers > 0) {
            while (op_top > 0 && !is_marker(ops[op_top - 1].kind)) {
                if (!apply_top()) {
                    return false;
                }
            }
            if (op_top == 0 || ops[op_top - 1].kind == mark_paren) {
                break;  // a comma outside a call ends the expression
            }
            read_byte();
            ++ops[op_top - 1].argc;
            want_operand = true;
            continue;
        }

        if (tk == token::rparen && markers > 0) {
            while (op_top > 0 && !is_marker(ops[op_top - 1].kind)) {
                if (!apply_top()) {
                    return false;
                }
            }
            if (op_top == 0) {
                break;
            }
            read_byte();
            const op_entry marker = ops[--op_top];
            --markers;
            if (marker.kind == mark_paren) {
                continue;
            }
            if (sp < marker.argc) {
                return raise_here(error_code::missing_operand);
            }
            basic_value* args = &operand_stack_[sp - marker.argc];
            basic_value result;
            if (marker.kind == mark_func) {
                if (!call_builtin(marker.op, args, marker.argc, &result)) {
                    return false;
                }
            } else {
                const bool is_string = (marker.op == token::var_str);
                uint16_t subs[2] = {0, 0};
                if (marker.argc > 2) {
                    return raise_here(error_code::subscript_out_of_range);
                }
                for (uint8_t i = 0; i < marker.argc; ++i) {
                    if (args[i].is_string || args[i].num < 0) {
                        return raise_here(error_code::subscript_out_of_range);
                    }
                    subs[i] = static_cast<uint16_t>(args[i].num);
                }
                basic_var* var = find_var(marker.name0, marker.name1, is_string, true);
                if (!var) {
                    uint16_t sizes[2] = {10, 10};
                    var = create_var(marker.name0, marker.name1, is_string, marker.argc, sizes);
                    if (!var) {
                        return false;
                    }
                }
                uint8_t* cell = var_cell(var, subs, marker.argc);
                if (!cell) {
                    return false;
                }
                load_value(cell, is_string, &result);
            }
            sp = static_cast<uint8_t>(sp - marker.argc);
            operand_stack_[sp++] = result;
            continue;
        }

        break;  // end of expression
    }

    while (op_top > 0) {
        if (is_marker(ops[op_top - 1].kind)) {
            return raise_here(error_code::syntax);
        }
        if (!apply_top()) {
            return false;
        }
    }
    if (sp != 1) {
        return raise_here(error_code::syntax);
    }
    *out = operand_stack_[0];
    return true;
}

bool interpreter::eval_number(int16_t* out) noexcept {
    basic_value v;
    if (!eval(&v)) {
        return false;
    }
    if (v.is_string) {
        return raise_here(error_code::type_mismatch);
    }
    *out = v.num;
    return true;
}

bool interpreter::eval_string(basic_value* out) noexcept {
    if (!eval(out)) {
        return false;
    }
    if (!out->is_string) {
        return raise_here(error_code::type_mismatch);
    }
    return true;
}

}  // namespace fmrb_basic
