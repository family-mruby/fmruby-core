// Family BASIC interpreter core: lifecycle, program storage, loading,
// output helpers and the run loop. Statement execution lives in
// basic_exec.cpp, expressions in basic_expr.cpp, variables in basic_vars.cpp.

#include "basic_core.hpp"

#include "basic_charset.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

// Error table, indexed by the error number (v3_spec error table, 22 entries).
// constexpr so it lands in .rodata and needs no dynamic initialization.
struct error_entry {
    const char* mnemonic;
    const char* message;
};

constexpr error_entry error_table[error_code_count] = {
    {"NF", "NEXT without FOR"},
    {"SN", "Syntax error"},
    {"RG", "RETURN without GOSUB"},
    {"OD", "Out of DATA"},
    {"IL", "Illegal function call"},
    {"OV", "Overflow"},
    {"OM", "Out of memory"},
    {"UL", "Undefined line number"},
    {"SO", "Subscript out of range"},
    {"DD", "Duplicate definition"},
    {"DZ", "Division by zero"},
    {"TM", "Type mismatch"},
    {"ST", "String too long"},
    {"FT", "Formula too complex"},
    {"CC", "Can't continue"},
    {"UF", "Undefined function"},
    {"MO", "Missing operand"},
    {"TP", "Tape read error"},
    {"NR", "No RESUME"},
    {"RE", "RESUME without error"},
    {"NB", "No BG data"},
    {"UP", "Unprintable error"},
};

}  // namespace

const char* error_mnemonic(error_code code) noexcept {
    const uint8_t index = static_cast<uint8_t>(code);
    if (index >= error_code_count) {
        return error_table[static_cast<uint8_t>(error_code::unprintable)].mnemonic;
    }
    return error_table[index].mnemonic;
}

const char* error_message(error_code code) noexcept {
    const uint8_t index = static_cast<uint8_t>(code);
    if (index >= error_code_count) {
        return error_table[static_cast<uint8_t>(error_code::unprintable)].message;
    }
    return error_table[index].message;
}

const keyword_entry* keyword_info(token tk) noexcept {
    for (size_t i = 0; i < keyword_count; ++i) {
        if (keyword_table[i].tk == tk) {
            return &keyword_table[i];
        }
    }
    return nullptr;
}

// --- lifecycle ------------------------------------------------------------

interpreter::interpreter(const basic_host_t& host) noexcept : host_(host) {}

interpreter::~interpreter() noexcept {
    if (host_.dealloc) {
        host_.dealloc(host_.user, lines_);
        host_.dealloc(host_.user, code_);
        host_.dealloc(host_.user, vars_);
        host_.dealloc(host_.user, var_data_);
        host_.dealloc(host_.user, for_stack_);
        host_.dealloc(host_.user, gosub_stack_);
        host_.dealloc(host_.user, operand_stack_);
        host_.dealloc(host_.user, work_src_);
        host_.dealloc(host_.user, work_code_);
        host_.dealloc(host_.user, screen_chars_);
        host_.dealloc(host_.user, screen_attrs_);
        host_.dealloc(host_.user, function_keys_);
        host_.dealloc(host_.user, audio_buffer_);
    }
    lines_ = nullptr;
    code_ = nullptr;
    vars_ = nullptr;
    var_data_ = nullptr;
    for_stack_ = nullptr;
    gosub_stack_ = nullptr;
    operand_stack_ = nullptr;
    work_src_ = nullptr;
    work_code_ = nullptr;
    screen_chars_ = nullptr;
    screen_attrs_ = nullptr;
    function_keys_ = nullptr;
    audio_buffer_ = nullptr;
}

bool interpreter::init(const basic_config& config) noexcept {
    if (!host_.alloc || !host_.dealloc) {
        return raise(error_code::out_of_memory, -1);
    }

    lines_ = static_cast<program_line*>(
        host_.alloc(host_.user, sizeof(program_line) * config.line_capacity));
    code_ = static_cast<uint8_t*>(host_.alloc(host_.user, config.code_capacity));
    vars_ = static_cast<basic_var*>(
        host_.alloc(host_.user, sizeof(basic_var) * config.var_capacity));
    var_data_ = static_cast<uint8_t*>(host_.alloc(host_.user, config.var_data_capacity));
    for_stack_ = static_cast<basic_for_frame*>(
        host_.alloc(host_.user, sizeof(basic_for_frame) * config.for_depth));
    gosub_stack_ = static_cast<basic_gosub_frame*>(
        host_.alloc(host_.user, sizeof(basic_gosub_frame) * config.gosub_depth));
    operand_stack_ = static_cast<basic_value*>(
        host_.alloc(host_.user, sizeof(basic_value) * config.expr_depth));
    work_src_ = static_cast<uint8_t*>(host_.alloc(host_.user, work_capacity));
    work_code_ = static_cast<uint8_t*>(host_.alloc(host_.user, work_capacity));
    const size_t screen_size = static_cast<size_t>(screen_columns) * screen_rows;
    screen_chars_ = static_cast<uint8_t*>(host_.alloc(host_.user, screen_size));
    screen_attrs_ = static_cast<uint8_t*>(host_.alloc(host_.user, screen_size));
    function_keys_ = static_cast<uint8_t*>(
        host_.alloc(host_.user, function_key_count * (1u + function_key_len)));

    if (!lines_ || !code_ || !vars_ || !var_data_ || !for_stack_ || !gosub_stack_ ||
        !operand_stack_ || !work_src_ || !work_code_ || !screen_chars_ || !screen_attrs_ ||
        !function_keys_) {
        return raise(error_code::out_of_memory, -1);
    }

    line_capacity_ = config.line_capacity;
    code_capacity_ = config.code_capacity;
    var_capacity_ = config.var_capacity;
    var_data_capacity_ = config.var_data_capacity;
    for_depth_ = config.for_depth;
    gosub_depth_ = config.gosub_depth;
    expr_depth_ = config.expr_depth;
    statements_per_frame_ = config.statements_per_frame;

    line_count_ = 0;
    code_used_ = 0;
    clear_variables();
    for (uint8_t i = 0; i < function_key_count; ++i) {
        function_keys_[i * (1u + function_key_len)] = 0;  // empty definition
    }
    // Power on defaults (core_spec sec 16): cleared screen, cursor home,
    // colour attribute 0 everywhere.
    for (size_t i = 0; i < screen_size; ++i) {
        screen_chars_[i] = ' ';
        screen_attrs_[i] = 0;
    }
    cursor_x_ = 0;
    cursor_y_ = 0;
    key_head_ = 0;
    key_tail_ = 0;
    click_on_ = false;

    // Start up palette. core_spec sec 16 says the screen comes up with BG
    // palette code 1, but does not list its colour codes, so these are a
    // readable placeholder: white, red, green and blue text on black.
    static constexpr uint8_t default_palette[4][3] = {
        {2, 22, 48}, {6, 22, 54}, {10, 26, 58}, {1, 17, 49},
    };
    for (uint8_t attr = 0; attr < 4; ++attr) {
        for (uint8_t i = 0; i < 3; ++i) {
            palette_[attr][i] = default_palette[attr][i];
        }
    }
    backdrop_ = 15;  // black
    cgen_ = 2;         // BG = table B, sprites = table A (core_spec sec 16)
    cgset_bg_ = 1;
    cgset_sprite_ = 1;
    load_palette_bank();
    screen_send_palette();
    // Fixed seed: RND must produce the same sequence on every run so golden
    // tests stay reproducible (core_spec does not define the seed behaviour).
    rng_state_ = 0x1234ABCDu;
    return true;
}

void interpreter::clear_program() noexcept {
    line_count_ = 0;
    code_used_ = 0;
    data_valid_ = false;
}

void interpreter::clear_variables() noexcept {
    var_count_ = 0;
    var_data_used_ = 0;
    for_top_ = 0;
    gosub_top_ = 0;
}

void interpreter::clear_error() noexcept {
    error_ = error_code::none;
    error_line_ = -1;
}

uint32_t interpreter::free_bytes() const noexcept {
    return var_data_capacity_ - var_data_used_;
}

const program_line* interpreter::line_at(uint16_t index) const noexcept {
    if (index >= line_count_) {
        return nullptr;
    }
    return &lines_[index];
}

const uint8_t* interpreter::code_at(uint16_t line_index) const noexcept {
    return code_ + lines_[line_index].offset;
}

// --- output ---------------------------------------------------------------

void interpreter::put_fb_char(uint8_t code) noexcept {
    // The character stream mirror is what the golden test runner and the
    // legacy console consume; the shadow buffer is what the screen shows.
    if (host_.put_char) {
        char buf[4];
        const size_t n = fbcode_to_utf8(code, buf);
        for (size_t i = 0; i < n; ++i) {
            host_.put_char(host_.user, buf[i]);
        }
    }
    screen_put(code);
}

void interpreter::print(const char* text) noexcept {
    if (!text) {
        return;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        put_fb_char(static_cast<uint8_t>(*p));
    }
}

void interpreter::print_newline() noexcept {
    if (host_.put_char) {
        host_.put_char(host_.user, '\n');
    }
    screen_newline();
}

void interpreter::print_line(const char* text) noexcept {
    print(text);
    print_newline();
}

void interpreter::print_number(int32_t value) noexcept {
    char digits[12];
    size_t count = 0;
    uint32_t magnitude = (value < 0) ? (0u - static_cast<uint32_t>(value))
                                     : static_cast<uint32_t>(value);
    do {
        digits[count++] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0 && count < sizeof(digits));

    if (value < 0) {
        put_fb_char('-');
    }
    while (count > 0) {
        put_fb_char(static_cast<uint8_t>(digits[--count]));
    }
}

bool interpreter::raise(error_code code, int32_t line_number) noexcept {
    error_ = code;
    error_line_ = line_number;
    running_ = false;

    // Family BASIC screen format: "?SN ERROR IN 10", or "?SN ERROR" when the
    // error is not tied to a program line (load time / direct mode).
    if (cursor_x_ != 0) {
        print_newline();
    }
    print("?");
    print(error_mnemonic(code));
    print(" ERROR");
    if (line_number >= 0) {
        print(" IN ");
        print_number(line_number);
    }
    print_newline();

    if (host_.on_error) {
        host_.on_error(host_.user, code, line_number);
    }
    return false;
}

bool interpreter::raise_here(error_code code) noexcept {
    return raise(code, static_cast<int32_t>(current_line_number()));
}

uint16_t interpreter::current_line_number() const noexcept {
    if (pc_line_ < line_count_) {
        return lines_[pc_line_].number;
    }
    return 0;
}

// --- program storage ------------------------------------------------------

uint16_t interpreter::lower_bound(uint16_t number) const noexcept {
    uint16_t low = 0;
    uint16_t high = line_count_;
    while (low < high) {
        const uint16_t mid = static_cast<uint16_t>(low + (high - low) / 2);
        if (lines_[mid].number < number) {
            low = static_cast<uint16_t>(mid + 1);
        } else {
            high = mid;
        }
    }
    return low;
}

int32_t interpreter::find_line(uint16_t number) const noexcept {
    const uint16_t index = lower_bound(number);
    if (index < line_count_ && lines_[index].number == number) {
        return static_cast<int32_t>(index);
    }
    return -1;
}

bool interpreter::code_append(const uint8_t* code, uint16_t length,
                              uint32_t* out_offset) noexcept {
    if (code_used_ + length > code_capacity_) {
        return false;
    }
    *out_offset = code_used_;
    for (uint16_t i = 0; i < length; ++i) {
        code_[code_used_ + i] = code[i];
    }
    code_used_ += length;
    return true;
}

void interpreter::delete_line(uint16_t number) noexcept {
    const uint16_t index = lower_bound(number);
    if (index >= line_count_ || lines_[index].number != number) {
        return;
    }
    for (uint16_t i = index; i + 1 < line_count_; ++i) {
        lines_[i] = lines_[i + 1];
    }
    --line_count_;
}

bool interpreter::store_line(uint16_t number, const uint8_t* code, uint16_t length) noexcept {
    const uint16_t index = lower_bound(number);
    const bool replacing = (index < line_count_ && lines_[index].number == number);

    if (replacing && length <= lines_[index].length) {
        // Reuse the slice in place so a reload does not waste arena space.
        for (uint16_t i = 0; i < length; ++i) {
            code_[lines_[index].offset + i] = code[i];
        }
        lines_[index].length = length;
        return true;
    }

    uint32_t offset = 0;
    if (!code_append(code, length, &offset)) {
        return raise(error_code::out_of_memory, -1);
    }
    if (replacing) {
        lines_[index].offset = offset;
        lines_[index].length = length;
        return true;
    }
    if (line_count_ >= line_capacity_) {
        return raise(error_code::out_of_memory, -1);
    }
    for (uint16_t i = line_count_; i > index; --i) {
        lines_[i] = lines_[i - 1];
    }
    lines_[index].number = number;
    lines_[index].offset = offset;
    lines_[index].length = length;
    ++line_count_;
    return true;
}

bool interpreter::load(const char* program) noexcept {
    if (!lines_ || !code_) {
        return raise(error_code::out_of_memory, -1);
    }
    clear_program();
    clear_variables();
    clear_error();
    if (!program) {
        return true;
    }

    const char* cursor = program;
    while (*cursor != '\0') {
        // Split off one source line.
        const char* line_start = cursor;
        while (*cursor != '\0' && *cursor != '\n') {
            ++cursor;
        }
        const char* line_end = cursor;
        if (*cursor == '\n') {
            ++cursor;
        }
        if (line_end > line_start && *(line_end - 1) == '\r') {
            --line_end;
        }

        // UTF-8 to Family BASIC character codes.
        size_t src_len = 0;
        const char* p = line_start;
        while (p < line_end && src_len < work_capacity - 1) {
            size_t used = 0;
            const uint32_t ucs = utf8_decode(p, static_cast<size_t>(line_end - p), &used);
            work_src_[src_len++] = unicode_to_fbcode(ucs);
            p += used;
        }
        work_src_[src_len] = 0;

        size_t at = 0;
        while (at < src_len && (work_src_[at] == ' ' || work_src_[at] == '\t')) {
            ++at;
        }
        if (at >= src_len) {
            continue;  // blank line
        }
        if (!is_digit(work_src_[at])) {
            // The program loader only accepts numbered lines.
            return raise(error_code::syntax, -1);
        }

        uint32_t number = 0;
        while (at < src_len && is_digit(work_src_[at])) {
            number = number * 10 + static_cast<uint32_t>(work_src_[at] - '0');
            if (number > max_line_number) {
                return raise(error_code::syntax, -1);
            }
            ++at;
        }
        while (at < src_len && work_src_[at] == ' ') {
            ++at;
        }
        if (src_len - at > max_line_length) {
            return raise(error_code::syntax, static_cast<int32_t>(number));
        }
        if (at >= src_len) {
            delete_line(static_cast<uint16_t>(number));
            continue;
        }

        const int32_t crunched =
            crunch_line(work_src_ + at, src_len - at, work_code_, work_capacity);
        if (crunched < 0) {
            // crunch_line has raised; attach the line number for the report.
            error_line_ = static_cast<int32_t>(number);
            return false;
        }
        if (!store_line(static_cast<uint16_t>(number), work_code_,
                        static_cast<uint16_t>(crunched))) {
            return false;
        }
    }
    return true;
}

// --- token stream access --------------------------------------------------

uint8_t interpreter::peek_byte() const noexcept {
    if (pc_line_ >= line_count_ || pc_off_ >= lines_[pc_line_].length) {
        return static_cast<uint8_t>(token::eol);
    }
    return code_at(pc_line_)[pc_off_];
}

uint8_t interpreter::read_byte() noexcept {
    const uint8_t b = peek_byte();
    if (pc_line_ < line_count_ && pc_off_ < lines_[pc_line_].length) {
        ++pc_off_;
    }
    return b;
}

void interpreter::skip_token() noexcept {
    const token tk = peek_token();
    read_byte();
    switch (tk) {
        case token::number:
            read_byte();
            read_byte();
            break;
        case token::var_num:
        case token::var_str:
            read_byte();
            read_byte();
            break;
        case token::string:
        case token::raw: {
            const uint8_t len = read_byte();
            for (uint8_t i = 0; i < len; ++i) {
                read_byte();
            }
            break;
        }
        default:
            break;
    }
}

bool interpreter::accept(token tk) noexcept {
    if (peek_token() == tk) {
        read_byte();
        return true;
    }
    return false;
}

bool interpreter::expect(token tk) noexcept {
    if (accept(tk)) {
        return true;
    }
    return raise_here(error_code::syntax);
}

bool interpreter::peek_ahead_is_lparen() const noexcept {
    if (pc_line_ >= line_count_) {
        return false;
    }
    const uint16_t next = static_cast<uint16_t>(pc_off_ + 1);
    if (next >= lines_[pc_line_].length) {
        return false;
    }
    return static_cast<token>(code_at(pc_line_)[next]) == token::lparen;
}

bool interpreter::at_statement_end() const noexcept {
    const token tk = peek_token();
    return tk == token::eol || tk == token::colon;
}

void interpreter::skip_to_eol() noexcept {
    while (peek_token() != token::eol) {
        skip_token();
    }
}

void interpreter::skip_to_statement_end() noexcept {
    while (!at_statement_end()) {
        skip_token();
    }
}

// --- run loop -------------------------------------------------------------

bool interpreter::run() noexcept {
    clear_error();
    clear_variables();
    data_valid_ = false;
    pc_line_ = 0;
    pc_off_ = 0;
    statement_count_ = 0;
    running_ = true;
    last_clock_ms_ = host_.ticks_ms ? host_.ticks_ms(host_.user) : 0;
    frame_accum_us_ = 0;
    frame_count_ = 0;
    frame_statements_ = 0;

    while (running_) {
        if (pc_line_ >= line_count_) {
            break;  // ran off the end of the program: normal END
        }
        if (pc_off_ >= lines_[pc_line_].length || peek_token() == token::eol) {
            ++pc_line_;
            pc_off_ = 0;
            continue;
        }
        if (accept(token::colon)) {
            continue;  // empty statement
        }

        // Cooperative hook: lets the embedder drain input and stop the program
        // (app close, break key), then run whatever frames have come due.
        if ((statement_count_ % tick_interval) == 0) {
            if (host_.on_tick && !host_.on_tick(host_.user)) {
                running_ = false;
                break;
            }
            service_frames();
        }
        ++statement_count_;
        ++frame_statements_;

        jumped_ = false;
        if (!exec_statement()) {
            running_ = false;
            return false;
        }
        if (!running_) {
            break;
        }
        if (!jumped_) {
            // Statements stop before their separator; step over it.
            if (peek_token() == token::colon) {
                read_byte();
            } else if (peek_token() != token::eol) {
                return raise_here(error_code::syntax);
            }
        }
    }

    running_ = false;
    if (cursor_x_ != 0) {
        print_newline();
    }
    if (host_.screen_present) {
        host_.screen_present(host_.user);
    }
    return error_ == error_code::none;
}

}  // namespace fmrb_basic
