// Statement execution. Control flow never recurses through the C stack:
// jumps and IF bodies move the program counter and let the run loop in
// basic_core.cpp continue (00_common memory design rule 1).

#include "basic_core.hpp"
#include "basic_charset.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

/// Arguments an extension statement can carry (screen / sprite / sound / gfx).
constexpr uint8_t max_ext_args = 8;

/// True when @p tk can start an expression, used by the extension hook parser.
bool starts_expression(token tk) noexcept {
    switch (tk) {
        case token::number:
        case token::string:
        case token::var_num:
        case token::var_str:
        case token::lparen:
        case token::minus:
        case token::plus:
            return true;
        default:
            break;
    }
    if (is_keyword(tk)) {
        const keyword_entry* kw = keyword_info(tk);
        return kw && (kw->kind == kw_kind::function || kw->tk == token::not_);
    }
    return false;
}

}  // namespace

bool interpreter::jump_to_line(uint16_t number) noexcept {
    const int32_t index = find_line(number);
    if (index < 0) {
        return raise_here(error_code::undefined_line);
    }
    pc_line_ = static_cast<uint16_t>(index);
    pc_off_ = 0;
    jumped_ = true;
    return true;
}

bool interpreter::exec_statement() noexcept {
    const token tk = peek_token();

    switch (tk) {
        case token::var_num:
        case token::var_str:
            read_byte();
            return st_assign(tk);

        case token::print:
            read_byte();
            return st_print();

        case token::input:
            read_byte();
            return st_input(false);

        case token::linput:
            read_byte();
            return st_input(true);

        case token::if_:
            read_byte();
            return st_if();

        case token::for_:
            read_byte();
            return st_for();

        case token::next:
            read_byte();
            return st_next();

        case token::goto_:
            read_byte();
            return st_goto();

        case token::gosub:
            read_byte();
            return st_gosub();

        case token::return_:
            read_byte();
            return st_return();

        case token::on:
            read_byte();
            return st_on();

        case token::dim:
            read_byte();
            return st_dim();

        case token::read:
            read_byte();
            return st_read();

        case token::restore:
            read_byte();
            return st_restore();

        case token::swap:
            read_byte();
            return st_swap();

        case token::pause:
            read_byte();
            return st_pause();

        case token::data:
            // Data is consumed by READ; skip the statement when executed.
            read_byte();
            skip_to_statement_end();
            return true;

        case token::rem:
            read_byte();
            skip_to_eol();
            return true;

        case token::end:
        case token::system:
            read_byte();
            running_ = false;
            return true;

        case token::stop: {
            read_byte();
            print("BREAK IN ");
            print_number(current_line_number());
            print_newline();
            running_ = false;
            return true;
        }

        case token::clear: {
            read_byte();
            if (!at_statement_end()) {
                int16_t dummy = 0;
                if (!eval_number(&dummy)) {  // CLEAR <top address>: no effect here
                    return false;
                }
            }
            clear_variables();
            return true;
        }

        case token::poke: {
            // The virtual memory map arrives in B4; the operands are still
            // evaluated so side effects and syntax errors behave the same.
            read_byte();
            int16_t value = 0;
            if (!eval_number(&value)) {
                return false;
            }
            while (accept(token::comma)) {
                if (!eval_number(&value)) {
                    return false;
                }
            }
            return true;
        }

        case token::cls:
            read_byte();
            // CLS [BG face] (v3_spec): the second face arrives with SCREEN in B4.
            if (!at_statement_end()) {
                int16_t face = 0;
                if (!eval_number(&face)) {
                    return false;
                }
            }
            screen_clear();
            return true;

        case token::locate:
            read_byte();
            return st_locate();

        case token::color:
            read_byte();
            return st_color();

        case token::key:
            read_byte();
            return st_key();

        case token::keylist: {
            read_byte();
            for (uint8_t i = 0; i < function_key_count; ++i) {
                const uint8_t* entry = function_keys_ + i * (1u + function_key_len);
                print_number(i + 1);
                put_fb_char(' ');
                for (uint8_t k = 0; k < entry[0]; ++k) {
                    put_fb_char(entry[1 + k]);
                }
                print_newline();
            }
            return true;
        }

        case token::click:
            read_byte();
            return st_click();

        case token::palet:
            read_byte();
            return st_palet();

        case token::def:
            read_byte();
            return st_def();

        case token::sprite:
            read_byte();
            return st_sprite();

        case token::move:
        case token::cut:
        case token::era:
        case token::can:
            read_byte();
            return st_move_group(tk);

        case token::position:
            read_byte();
            return st_position();

        case token::scrdump:
            read_byte();
            return st_scrdump();

        case token::cont:
            read_byte();
            return raise_here(error_code::cant_continue);

        case token::call:
            // Machine language is out of scope (compat_plan sec 5.6, L5).
            read_byte();
            return raise_here(error_code::illegal_function_call);

        default:
            break;
    }

    if (is_keyword(tk)) {
        const keyword_entry* kw = keyword_info(tk);
        if (kw && kw->kind == kw_kind::statement) {
            read_byte();
            return st_ext(tk);
        }
    }
    return raise_here(error_code::syntax);
}

bool interpreter::st_assign(token var_tk) noexcept {
    basic_var* var = nullptr;
    uint8_t* cell = nullptr;
    if (!parse_var_target(var_tk, &var, &cell)) {
        return false;
    }
    if (!expect(token::equal)) {
        return false;
    }
    basic_value value;
    if (!eval(&value)) {
        return false;
    }
    return store_value(cell, var->is_string, &value);
}

bool interpreter::st_print() noexcept {
    bool newline_pending = true;

    while (!at_statement_end()) {
        const token tk = peek_token();

        if (tk == token::semicolon) {
            read_byte();
            newline_pending = false;
            continue;
        }
        if (tk == token::comma) {
            read_byte();
            // Comma tabs to the next 8 column zone; past the last one it wraps
            // to the next line (core_spec sec 6).
            const uint8_t next_zone =
                static_cast<uint8_t>((cursor_x_ / print_zone_width + 1) * print_zone_width);
            if (next_zone >= screen_columns) {
                print_newline();
            } else {
                while (cursor_x_ < next_zone) {
                    put_fb_char(' ');
                }
            }
            newline_pending = false;
            continue;
        }

        // A bare string literal is printed straight from the token stream so
        // that literals longer than the 31 character value limit still work.
        if (tk == token::string) {
            const uint16_t save_off = pc_off_;
            skip_token();
            const token after = peek_token();
            if (after == token::eol || after == token::colon || after == token::comma ||
                after == token::semicolon) {
                const uint8_t* code = code_at(pc_line_);
                const uint8_t slen = code[save_off + 1];
                for (uint8_t i = 0; i < slen; ++i) {
                    put_fb_char(code[save_off + 2 + i]);
                }
                newline_pending = true;
                continue;
            }
            pc_off_ = save_off;  // part of a larger expression: evaluate it
        }

        basic_value value;
        if (!eval(&value)) {
            return false;
        }
        if (value.is_string) {
            for (uint8_t i = 0; i < value.len; ++i) {
                put_fb_char(value.str[i]);
            }
        } else {
            // Numbers print with a sign column in front and a trailing space
            // (core_spec sec 6).
            put_fb_char(value.num < 0 ? '-' : ' ');
            print_number(value.num < 0 ? -static_cast<int32_t>(value.num) : value.num);
            put_fb_char(' ');
        }
        newline_pending = true;
    }

    if (newline_pending) {
        print_newline();
    }
    return true;
}

bool interpreter::st_input(bool line_input) noexcept {
    bool show_question = true;

    if (peek_token() == token::string) {
        read_byte();
        const uint8_t slen = read_byte();
        for (uint8_t i = 0; i < slen; ++i) {
            put_fb_char(read_byte());
        }
        if (accept(token::comma)) {
            show_question = false;
        } else if (!accept(token::semicolon)) {
            return raise_here(error_code::syntax);
        }
    }
    if (line_input) {
        show_question = false;  // LINPUT never prints '?' (core_spec sec 6)
    }
    if (show_question) {
        put_fb_char('?');
        put_fb_char(' ');
    }

    // Read one line and convert it to Family BASIC character codes.
    size_t len = 0;
    if (host_.read_line) {
        char* raw = reinterpret_cast<char*>(work_code_);
        const int n = host_.read_line(host_.user, raw, work_capacity);
        if (n > 0) {
            size_t at = 0;
            while (at < static_cast<size_t>(n) && len < work_capacity - 1) {
                size_t used = 0;
                const uint32_t ucs = utf8_decode(raw + at, static_cast<size_t>(n) - at, &used);
                work_src_[len++] = unicode_to_fbcode(ucs);
                at += used;
            }
        }
    }
    work_src_[len] = 0;
    print_newline();

    size_t pos = 0;
    while (true) {
        const token var_tk = peek_token();
        if (var_tk != token::var_num && var_tk != token::var_str) {
            return raise_here(error_code::syntax);
        }
        read_byte();
        basic_var* var = nullptr;
        uint8_t* cell = nullptr;
        if (!parse_var_target(var_tk, &var, &cell)) {
            return false;
        }

        // Split the input: LINPUT takes the whole line, INPUT stops at commas
        // unless the field is quoted (core_spec sec 6).
        while (!line_input && pos < len && work_src_[pos] == ' ') {
            ++pos;
        }
        size_t start = pos;
        size_t stop = pos;
        if (!line_input && pos < len && work_src_[pos] == '"') {
            ++pos;
            start = pos;
            while (pos < len && work_src_[pos] != '"') {
                ++pos;
            }
            stop = pos;
            if (pos < len) {
                ++pos;
            }
            while (pos < len && work_src_[pos] != ',') {
                ++pos;
            }
        } else {
            while (pos < len && (line_input || work_src_[pos] != ',')) {
                ++pos;
            }
            stop = pos;
        }
        if (pos < len && work_src_[pos] == ',') {
            ++pos;
        }

        basic_value value;
        if (var->is_string) {
            size_t field = stop - start;
            if (field > max_string_len) {
                field = max_string_len;
            }
            set_string(&value, work_src_ + start, field);
        } else {
            // A field that is not a number reads as 0 (core_spec sec 6).
            int32_t number = 0;
            bool negative = false;
            size_t at = start;
            if (at < stop && (work_src_[at] == '+' || work_src_[at] == '-')) {
                negative = (work_src_[at] == '-');
                ++at;
            }
            while (at < stop && is_digit(work_src_[at])) {
                number = number * 10 + (work_src_[at] - '0');
                if (number > 32768) {
                    number = 0;
                    break;
                }
                ++at;
            }
            if (negative) {
                number = -number;
            }
            if (number > 32767 || number < -32768) {
                number = 0;
            }
            set_number(&value, static_cast<int16_t>(number));
        }
        if (!store_value(cell, var->is_string, &value)) {
            return false;
        }

        if (line_input || !accept(token::comma)) {
            break;
        }
    }
    return true;
}

bool interpreter::st_if() noexcept {
    int16_t condition = 0;
    if (!eval_number(&condition)) {
        return false;
    }

    const bool has_then = accept(token::then);
    if (!has_then && peek_token() != token::goto_) {
        return raise_here(error_code::syntax);
    }

    if (condition == 0) {
        // False: the rest of the line is skipped, including ':' chained
        // statements (phase_b0_report sec 8.2 #3).
        skip_to_eol();
        return true;
    }

    if (peek_token() == token::number) {
        read_byte();
        const uint8_t lo = read_byte();
        const uint8_t hi = read_byte();
        return jump_to_line(static_cast<uint16_t>(lo | (hi << 8)));
    }
    // THEN <statement>: let the run loop continue from here.
    jumped_ = true;
    return true;
}

bool interpreter::skip_for_body(const uint8_t* name) noexcept {
    (void)name;  // NEXT carries no variable name in Family BASIC
    uint16_t depth = 0;
    while (true) {
        if (pc_line_ >= line_count_) {
            return raise_here(error_code::next_without_for);
        }
        const token tk = peek_token();
        if (tk == token::eol) {
            ++pc_line_;
            pc_off_ = 0;
            continue;
        }
        if (tk == token::for_) {
            ++depth;
            skip_token();
            continue;
        }
        if (tk == token::next) {
            skip_token();
            if (depth == 0) {
                jumped_ = true;  // continue right after the matching NEXT
                return true;
            }
            --depth;
            continue;
        }
        skip_token();
    }
}

bool interpreter::st_for() noexcept {
    if (peek_token() != token::var_num) {
        return raise_here(error_code::syntax);
    }
    read_byte();
    const uint8_t n0 = read_byte();
    const uint8_t n1 = read_byte();

    basic_var* var = find_var(n0, n1, false, false);
    if (!var) {
        var = create_var(n0, n1, false, 0, nullptr);
        if (!var) {
            return false;
        }
    }
    if (!expect(token::equal)) {
        return false;
    }
    int16_t start = 0;
    if (!eval_number(&start)) {
        return false;
    }
    basic_value value;
    set_number(&value, start);
    if (!store_value(var_data_ + var->offset, false, &value)) {
        return false;
    }

    if (!expect(token::to)) {
        return false;
    }
    int16_t limit = 0;
    if (!eval_number(&limit)) {
        return false;
    }
    int16_t step = 1;
    if (accept(token::step)) {
        if (!eval_number(&step)) {
            return false;
        }
    }

    // The loop body is skipped entirely when the limit is already passed
    // (core_spec sec 6).
    if ((step >= 0 && start > limit) || (step < 0 && start < limit)) {
        uint8_t name[2] = {n0, n1};
        return skip_for_body(name);
    }

    if (for_top_ >= for_depth_) {
        return raise_here(error_code::out_of_memory);
    }
    basic_for_frame& frame = for_stack_[for_top_++];
    frame.name[0] = n0;
    frame.name[1] = n1;
    frame.limit = limit;
    frame.step = step;
    frame.body_line = pc_line_;
    frame.body_off = pc_off_;
    return true;
}

bool interpreter::st_next() noexcept {
    if (peek_token() == token::var_num || peek_token() == token::var_str) {
        // Family BASIC does not allow a loop variable after NEXT
        // (core_spec sec 6).
        return raise_here(error_code::syntax);
    }
    if (for_top_ == 0) {
        return raise_here(error_code::next_without_for);
    }

    basic_for_frame& frame = for_stack_[for_top_ - 1];
    basic_var* var = find_var(frame.name[0], frame.name[1], false, false);
    if (!var) {
        return raise_here(error_code::next_without_for);
    }
    basic_value value;
    load_value(var_data_ + var->offset, false, &value);
    const int32_t next = static_cast<int32_t>(value.num) + frame.step;
    set_number(&value, static_cast<int16_t>(static_cast<uint16_t>(next & 0xFFFF)));
    if (!store_value(var_data_ + var->offset, false, &value)) {
        return false;
    }

    const bool again = (frame.step >= 0) ? (value.num <= frame.limit)
                                         : (value.num >= frame.limit);
    if (again) {
        pc_line_ = frame.body_line;
        pc_off_ = frame.body_off;
        jumped_ = true;
        return true;
    }
    --for_top_;
    return true;
}

bool interpreter::st_goto() noexcept {
    if (peek_token() != token::number) {
        return raise_here(error_code::syntax);
    }
    read_byte();
    const uint8_t lo = read_byte();
    const uint8_t hi = read_byte();
    return jump_to_line(static_cast<uint16_t>(lo | (hi << 8)));
}

bool interpreter::st_gosub() noexcept {
    if (peek_token() != token::number) {
        return raise_here(error_code::syntax);
    }
    read_byte();
    const uint8_t lo = read_byte();
    const uint8_t hi = read_byte();

    if (gosub_top_ >= gosub_depth_) {
        return raise_here(error_code::out_of_memory);
    }
    basic_gosub_frame& frame = gosub_stack_[gosub_top_++];
    frame.line = pc_line_;
    frame.off = pc_off_;
    return jump_to_line(static_cast<uint16_t>(lo | (hi << 8)));
}

bool interpreter::st_return() noexcept {
    if (gosub_top_ == 0) {
        return raise_here(error_code::return_without_gosub);
    }
    const basic_gosub_frame frame = gosub_stack_[--gosub_top_];

    if (peek_token() == token::number) {
        // RETURN <line> resumes somewhere else (v3_spec).
        read_byte();
        const uint8_t lo = read_byte();
        const uint8_t hi = read_byte();
        return jump_to_line(static_cast<uint16_t>(lo | (hi << 8)));
    }
    pc_line_ = frame.line;
    pc_off_ = frame.off;
    jumped_ = true;
    return true;
}

bool interpreter::st_on() noexcept {
    int16_t selector = 0;
    if (!eval_number(&selector)) {
        return false;
    }

    const token action = peek_token();
    if (action != token::goto_ && action != token::gosub && action != token::restore &&
        action != token::return_) {
        return raise_here(error_code::syntax);
    }
    read_byte();

    int16_t index = 0;
    uint16_t target = 0;
    bool found = false;
    while (true) {
        if (peek_token() != token::number) {
            return raise_here(error_code::syntax);
        }
        read_byte();
        const uint8_t lo = read_byte();
        const uint8_t hi = read_byte();
        ++index;
        if (index == selector) {
            target = static_cast<uint16_t>(lo | (hi << 8));
            found = true;
        }
        if (!accept(token::comma)) {
            break;
        }
    }

    // 0 or out of range simply falls through to the next statement
    // (core_spec sec 6).
    if (!found) {
        return true;
    }
    if (action == token::gosub) {
        if (gosub_top_ >= gosub_depth_) {
            return raise_here(error_code::out_of_memory);
        }
        basic_gosub_frame& frame = gosub_stack_[gosub_top_++];
        frame.line = pc_line_;
        frame.off = pc_off_;
    }
    if (action == token::restore) {
        const int32_t line_index = find_line(target);
        if (line_index < 0) {
            return raise_here(error_code::undefined_line);
        }
        data_line_ = static_cast<uint16_t>(line_index);
        data_off_ = 0;
        data_pos_ = 0;
        data_valid_ = false;
        return true;
    }
    return jump_to_line(target);
}

bool interpreter::st_dim() noexcept {
    while (true) {
        const token var_tk = peek_token();
        if (var_tk != token::var_num && var_tk != token::var_str) {
            return raise_here(error_code::syntax);
        }
        read_byte();
        const uint8_t n0 = read_byte();
        const uint8_t n1 = read_byte();
        const bool is_string = (var_tk == token::var_str);

        if (!expect(token::lparen)) {
            return false;
        }
        uint16_t sizes[2] = {0, 0};
        uint8_t dims = 0;
        while (true) {
            int16_t size = 0;
            if (!eval_number(&size)) {
                return false;
            }
            if (size < 0 || dims >= 2) {
                return raise_here(error_code::subscript_out_of_range);
            }
            sizes[dims++] = static_cast<uint16_t>(size);
            if (!accept(token::comma)) {
                break;
            }
        }
        if (!expect(token::rparen)) {
            return false;
        }

        if (find_var(n0, n1, is_string, true)) {
            return raise_here(error_code::duplicate_definition);
        }
        if (!create_var(n0, n1, is_string, dims, sizes)) {
            return false;
        }
        if (!accept(token::comma)) {
            break;
        }
    }
    return true;
}

bool interpreter::read_data_value(bool want_string, basic_value* out) noexcept {
    while (true) {
        if (!data_valid_) {
            // Find the next DATA statement, starting where the pointer sits.
            bool found = false;
            while (data_line_ < line_count_ && !found) {
                const uint8_t* code = code_ + lines_[data_line_].offset;
                const uint16_t length = lines_[data_line_].length;
                uint16_t at = data_off_;
                while (at < length) {
                    const token tk = static_cast<token>(code[at]);
                    if (tk == token::eol) {
                        break;
                    }
                    if (tk == token::data) {
                        data_off_ = static_cast<uint16_t>(at + 1);
                        data_pos_ = 0;
                        data_valid_ = true;
                        found = true;
                        break;
                    }
                    // Step over this token, operands included.
                    if (tk == token::number) {
                        at += 3;
                    } else if (tk == token::var_num || tk == token::var_str) {
                        at += 3;
                    } else if (tk == token::string || tk == token::raw) {
                        at += static_cast<uint16_t>(2 + code[at + 1]);
                    } else {
                        ++at;
                    }
                }
                if (!found) {
                    ++data_line_;
                    data_off_ = 0;
                }
            }
            if (!found) {
                return raise_here(error_code::out_of_data);
            }
        }

        const uint8_t* code = code_ + lines_[data_line_].offset;
        if (static_cast<token>(code[data_off_]) != token::raw) {
            data_valid_ = false;
            data_off_ = static_cast<uint16_t>(data_off_ + 1);
            continue;
        }
        const uint8_t rlen = code[data_off_ + 1];
        const uint8_t* payload = code + data_off_ + 2;

        // Skip separators left from the previous item.
        while (data_pos_ < rlen && (payload[data_pos_] == ' ' || payload[data_pos_] == ',')) {
            if (payload[data_pos_] == ',') {
                ++data_pos_;
                break;
            }
            ++data_pos_;
        }
        while (data_pos_ < rlen && payload[data_pos_] == ' ') {
            ++data_pos_;
        }
        if (data_pos_ >= rlen) {
            // This DATA statement is exhausted: continue after it.
            data_off_ = static_cast<uint16_t>(data_off_ + 2 + rlen);
            data_valid_ = false;
            continue;
        }

        uint8_t start = data_pos_;
        uint8_t stop = data_pos_;
        if (payload[data_pos_] == '"') {
            ++data_pos_;
            start = data_pos_;
            while (data_pos_ < rlen && payload[data_pos_] != '"') {
                ++data_pos_;
            }
            stop = data_pos_;
            if (data_pos_ < rlen) {
                ++data_pos_;
            }
        } else {
            while (data_pos_ < rlen && payload[data_pos_] != ',') {
                ++data_pos_;
            }
            stop = data_pos_;
            while (stop > start && payload[stop - 1] == ' ') {
                --stop;
            }
        }

        if (want_string) {
            size_t field = static_cast<size_t>(stop - start);
            if (field > max_string_len) {
                return raise_here(error_code::string_too_long);
            }
            set_string(out, payload + start, field);
            return true;
        }

        int32_t number = 0;
        uint8_t at = start;
        bool negative = false;
        if (at < stop && (payload[at] == '+' || payload[at] == '-')) {
            negative = (payload[at] == '-');
            ++at;
        }
        if (at >= stop) {
            return raise_here(error_code::type_mismatch);
        }
        while (at < stop) {
            if (!is_digit(payload[at])) {
                return raise_here(error_code::type_mismatch);
            }
            number = number * 10 + (payload[at] - '0');
            if (number > 32768) {
                return raise_here(error_code::overflow);
            }
            ++at;
        }
        if (negative) {
            number = -number;
        }
        if (number > 32767) {
            return raise_here(error_code::overflow);
        }
        set_number(out, static_cast<int16_t>(number));
        return true;
    }
}

bool interpreter::st_read() noexcept {
    while (true) {
        const token var_tk = peek_token();
        if (var_tk != token::var_num && var_tk != token::var_str) {
            return raise_here(error_code::syntax);
        }
        read_byte();
        basic_var* var = nullptr;
        uint8_t* cell = nullptr;
        if (!parse_var_target(var_tk, &var, &cell)) {
            return false;
        }
        basic_value value;
        if (!read_data_value(var->is_string, &value)) {
            return false;
        }
        if (!store_value(cell, var->is_string, &value)) {
            return false;
        }
        if (!accept(token::comma)) {
            break;
        }
    }
    return true;
}

bool interpreter::st_restore() noexcept {
    uint16_t line_index = 0;
    if (peek_token() == token::number) {
        read_byte();
        const uint8_t lo = read_byte();
        const uint8_t hi = read_byte();
        const int32_t index = find_line(static_cast<uint16_t>(lo | (hi << 8)));
        if (index < 0) {
            return raise_here(error_code::undefined_line);
        }
        line_index = static_cast<uint16_t>(index);
    }
    data_line_ = line_index;
    data_off_ = 0;
    data_pos_ = 0;
    data_valid_ = false;
    return true;
}

bool interpreter::st_swap() noexcept {
    const token first_tk = peek_token();
    if (first_tk != token::var_num && first_tk != token::var_str) {
        return raise_here(error_code::syntax);
    }
    read_byte();
    basic_var* first_var = nullptr;
    uint8_t* first_cell = nullptr;
    if (!parse_var_target(first_tk, &first_var, &first_cell)) {
        return false;
    }
    if (!expect(token::comma)) {
        return false;
    }
    const token second_tk = peek_token();
    if (second_tk != token::var_num && second_tk != token::var_str) {
        return raise_here(error_code::syntax);
    }
    read_byte();
    basic_var* second_var = nullptr;
    uint8_t* second_cell = nullptr;
    if (!parse_var_target(second_tk, &second_var, &second_cell)) {
        return false;
    }
    if (first_var->is_string != second_var->is_string) {
        return raise_here(error_code::type_mismatch);
    }

    basic_value a;
    basic_value b;
    load_value(first_cell, first_var->is_string, &a);
    load_value(second_cell, second_var->is_string, &b);
    return store_value(first_cell, first_var->is_string, &b) &&
           store_value(second_cell, second_var->is_string, &a);
}

bool interpreter::st_pause() noexcept {
    int16_t frames = 0;
    if (!at_statement_end()) {
        if (!eval_number(&frames)) {
            return false;
        }
    }
    if (frames < 0) {
        return raise_here(error_code::illegal_function_call);
    }
    if (frames == 0) {
        // PAUSE without a count waits for a key, which needs the keyboard
        // (B2). Until then it returns immediately.
        return true;
    }

    // The unit is one frame, 1/60 s (core_spec sec 5). Counting frames rather
    // than milliseconds keeps sprite movement and PAUSE on the same clock, and
    // lets a host without clock control (the test runner) step frames directly
    // so golden tests can observe motion.
    const uint32_t target = frame_count_ + static_cast<uint32_t>(frames);
    while (frame_count_ < target && running_) {
        if (host_.on_tick && !host_.on_tick(host_.user)) {
            running_ = false;
            return true;
        }
        if (host_.sleep_ms) {
            host_.sleep_ms(host_.user, 1);
            service_frames();
        } else {
            frame_tick();
        }
    }
    frame_statements_ = 0;
    return true;
}

bool interpreter::st_locate() noexcept {
    int16_t x = 0;
    int16_t y = 0;
    if (!eval_number(&x) || !expect(token::comma) || !eval_number(&y)) {
        return false;
    }
    if (x < 0 || x >= screen_columns || y < 0 || y >= screen_rows) {
        return raise_here(error_code::illegal_function_call);
    }
    cursor_x_ = static_cast<uint8_t>(x);
    cursor_y_ = static_cast<uint8_t>(y);
    return true;
}

bool interpreter::st_color() noexcept {
    // COLOR X,Y,n sets the colour of the 2x2 character area that contains
    // (X, Y); the area origin is even in both axes (core_spec sec 7).
    int16_t x = 0;
    int16_t y = 0;
    int16_t attr = 0;
    if (!eval_number(&x) || !expect(token::comma) || !eval_number(&y) ||
        !expect(token::comma) || !eval_number(&attr)) {
        return false;
    }
    if (x < 0 || x >= screen_columns || y < 0 || y >= screen_rows || attr < 0 || attr > 3) {
        return raise_here(error_code::illegal_function_call);
    }
    const uint8_t x0 = static_cast<uint8_t>(x) & static_cast<uint8_t>(~(color_area_size - 1));
    const uint8_t y0 = static_cast<uint8_t>(y) & static_cast<uint8_t>(~(color_area_size - 1));
    for (uint8_t dy = 0; dy < color_area_size; ++dy) {
        for (uint8_t dx = 0; dx < color_area_size; ++dx) {
            const uint8_t cx = static_cast<uint8_t>(x0 + dx);
            const uint8_t cy = static_cast<uint8_t>(y0 + dy);
            if (cx < screen_columns && cy < screen_rows) {
                screen_set_cell(cx, cy, screen_char(cx, cy), static_cast<uint8_t>(attr));
            }
        }
    }
    return true;
}

bool interpreter::st_key() noexcept {
    int16_t index = 0;
    if (!eval_number(&index) || !expect(token::comma)) {
        return false;
    }
    if (index < 1 || index > function_key_count) {
        return raise_here(error_code::illegal_function_call);
    }
    basic_value text;
    if (!eval_string(&text)) {
        return false;
    }
    // Definitions longer than 15 characters are truncated (core_spec sec 5).
    uint8_t* entry = function_keys_ + (index - 1) * (1u + function_key_len);
    const uint8_t len = (text.len > function_key_len) ? function_key_len : text.len;
    entry[0] = len;
    for (uint8_t i = 0; i < len; ++i) {
        entry[1 + i] = text.str[i];
    }
    return true;
}

bool interpreter::st_click() noexcept {
    if (accept(token::on)) {
        click_on_ = true;
        return true;
    }
    if (accept(token::off)) {
        click_on_ = false;
        return true;
    }
    return raise_here(error_code::syntax);
}

bool interpreter::st_scrdump() noexcept {
    int16_t tag = 0;
    int16_t flags = 0;
    if (!at_statement_end()) {
        if (!eval_number(&tag)) {
            return false;
        }
        if (accept(token::comma) && !eval_number(&flags)) {
            return false;
        }
    }
    screen_dump(static_cast<uint16_t>(tag), (flags & 1) != 0);
    return true;
}

bool interpreter::st_palet() noexcept {
    // PALET {B|S} n,C1,C2,C3,C4 (core_spec sec 7). The selector is a bare
    // letter, so it arrives as a variable token.
    bool sprite_palette = false;
    if (peek_token() == token::var_num) {
        const uint16_t save = pc_off_;
        read_byte();
        const uint8_t n0 = read_byte();
        const uint8_t n1 = read_byte();
        if (n1 == 0 && (n0 == 'B' || n0 == 'S')) {
            sprite_palette = (n0 == 'S');
        } else {
            pc_off_ = save;  // no selector: treat it as the BG palette
        }
    }

    int16_t group = 0;
    int16_t colors[4] = {0, 0, 0, 0};
    if (!eval_number(&group)) {
        return false;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (!expect(token::comma) || !eval_number(&colors[i])) {
            return false;
        }
    }
    if (group < 0 || group > 3) {
        return raise_here(error_code::illegal_function_call);
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (colors[i] < 0 || colors[i] > 60) {
            return raise_here(error_code::illegal_function_call);
        }
    }

    // Sprite palettes belong to B3; accepting the statement keeps programs
    // running, and the colours are remembered there.
    if (sprite_palette) {
        return true;
    }
    // C1 is the backdrop and only takes effect for group 0 (core_spec sec 7).
    if (group == 0) {
        backdrop_ = static_cast<uint8_t>(colors[0]);
    }
    palette_[group][0] = static_cast<uint8_t>(colors[1]);
    palette_[group][1] = static_cast<uint8_t>(colors[2]);
    palette_[group][2] = static_cast<uint8_t>(colors[3]);
    screen_send_palette();
    screen_refresh();
    return true;
}

bool interpreter::st_ext(token tk) noexcept {
    basic_arg args[max_ext_args];
    basic_value values[max_ext_args];
    uint8_t argc = 0;

    while (!at_statement_end() && starts_expression(peek_token()) && argc < max_ext_args) {
        if (!eval(&values[argc])) {
            return false;
        }
        args[argc].is_string = values[argc].is_string;
        args[argc].num = values[argc].num;
        args[argc].str = values[argc].str;
        args[argc].len = values[argc].len;
        ++argc;
        if (!accept(token::comma)) {
            break;
        }
    }
    // Keyword operands (SPRITE ON, PALET B, ...) are not expressions; the
    // statements that need them are implemented in B2/B3.
    skip_to_statement_end();

    if (host_.ext_statement &&
        host_.ext_statement(host_.user, static_cast<uint8_t>(tk), args, argc)) {
        return true;
    }
    return raise_here(error_code::illegal_function_call);
}

}  // namespace fmrb_basic
