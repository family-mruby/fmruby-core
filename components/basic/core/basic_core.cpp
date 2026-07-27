// Family BASIC compatible interpreter core (host independent).
// See basic_core.hpp for the contract. No IDF / fmruby headers here.

#include "basic_core.hpp"

namespace fmrb_basic {
namespace {

// Error table, indexed by the error number (spec: V3 error table, 22 entries).
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

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t';
}

bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

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

interpreter::interpreter(const basic_host_t& host) noexcept : host_(host) {}

interpreter::~interpreter() noexcept {
    if (host_.dealloc) {
        host_.dealloc(host_.user, lines_);
        host_.dealloc(host_.user, text_);
    }
    lines_ = nullptr;
    text_ = nullptr;
}

bool interpreter::init(uint16_t line_capacity, size_t text_capacity) noexcept {
    if (line_capacity == 0) {
        line_capacity = default_line_capacity;
    }
    if (text_capacity == 0) {
        text_capacity = default_text_capacity;
    }
    if (!host_.alloc || !host_.dealloc) {
        return raise(error_code::out_of_memory, -1);
    }

    void* line_mem = host_.alloc(host_.user, sizeof(program_line) * line_capacity);
    void* text_mem = host_.alloc(host_.user, text_capacity);
    if (!line_mem || !text_mem) {
        host_.dealloc(host_.user, line_mem);
        host_.dealloc(host_.user, text_mem);
        return raise(error_code::out_of_memory, -1);
    }

    lines_ = static_cast<program_line*>(line_mem);
    line_capacity_ = line_capacity;
    line_count_ = 0;
    text_ = static_cast<char*>(text_mem);
    text_capacity_ = text_capacity;
    text_used_ = 0;
    return true;
}

void interpreter::clear_program() noexcept {
    line_count_ = 0;
    text_used_ = 0;
}

void interpreter::clear_error() noexcept {
    error_ = error_code::none;
    error_line_ = -1;
}

const program_line* interpreter::line_at(uint16_t index) const noexcept {
    if (index >= line_count_) {
        return nullptr;
    }
    return &lines_[index];
}

const char* interpreter::line_text(uint16_t index) const noexcept {
    if (index >= line_count_) {
        return nullptr;
    }
    return text_ + lines_[index].offset;
}

void interpreter::print(const char* text) noexcept {
    if (!text || !host_.put_char) {
        return;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        host_.put_char(host_.user, *p);
    }
}

void interpreter::print_line(const char* text) noexcept {
    print(text);
    if (host_.put_char) {
        host_.put_char(host_.user, '\n');
    }
}

void interpreter::print_number(int32_t value) noexcept {
    if (!host_.put_char) {
        return;
    }
    // int32_t worst case is 11 characters ("-2147483648").
    char digits[12];
    size_t count = 0;
    uint32_t magnitude = (value < 0) ? (0u - static_cast<uint32_t>(value))
                                     : static_cast<uint32_t>(value);
    do {
        digits[count++] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0 && count < sizeof(digits));

    if (value < 0) {
        host_.put_char(host_.user, '-');
    }
    while (count > 0) {
        host_.put_char(host_.user, digits[--count]);
    }
}

bool interpreter::raise(error_code code, int32_t line_number) noexcept {
    error_ = code;
    error_line_ = line_number;
    running_ = false;

    // Family BASIC screen format: "?SN ERROR IN 10", or "?SN ERROR" when the
    // error is not tied to a program line (load time / direct mode).
    print("?");
    print(error_mnemonic(code));
    print(" ERROR");
    if (line_number >= 0) {
        print(" IN ");
        print_number(line_number);
    }
    print_line("");

    if (host_.on_error) {
        host_.on_error(host_.user, code, line_number);
    }
    return false;
}

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

bool interpreter::arena_append(const char* text, size_t length, uint32_t* out_offset) noexcept {
    if (text_used_ + length > text_capacity_) {
        return false;
    }
    *out_offset = static_cast<uint32_t>(text_used_);
    for (size_t i = 0; i < length; ++i) {
        text_[text_used_ + i] = text[i];
    }
    text_used_ += length;
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

bool interpreter::store_line(uint16_t number, const char* text, size_t length) noexcept {
    const uint16_t index = lower_bound(number);
    const bool replacing = (index < line_count_ && lines_[index].number == number);

    if (replacing && length <= lines_[index].length) {
        // Reuse the slice in place so a reload does not waste arena space.
        for (size_t i = 0; i < length; ++i) {
            text_[lines_[index].offset + i] = text[i];
        }
        lines_[index].length = static_cast<uint16_t>(length);
        return true;
    }

    uint32_t offset = 0;
    if (!arena_append(text, length, &offset)) {
        return raise(error_code::out_of_memory, -1);
    }

    if (replacing) {
        lines_[index].offset = offset;
        lines_[index].length = static_cast<uint16_t>(length);
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
    lines_[index].length = static_cast<uint16_t>(length);
    ++line_count_;
    return true;
}

bool interpreter::load(const char* program) noexcept {
    if (!lines_ || !text_) {
        return raise(error_code::out_of_memory, -1);
    }
    clear_program();
    clear_error();
    if (!program) {
        return true;
    }

    const char* cursor = program;
    while (*cursor != '\0') {
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

        const char* p = line_start;
        while (p < line_end && is_space(*p)) {
            ++p;
        }
        if (p == line_end) {
            continue;  // blank line
        }
        if (!is_digit(*p)) {
            // Direct mode statements are not accepted by the program loader.
            return raise(error_code::syntax, -1);
        }

        uint32_t number = 0;
        while (p < line_end && is_digit(*p)) {
            number = number * 10 + static_cast<uint32_t>(*p - '0');
            if (number > max_line_number) {
                return raise(error_code::syntax, -1);
            }
            ++p;
        }
        while (p < line_end && is_space(*p)) {
            ++p;
        }

        const size_t length = static_cast<size_t>(line_end - p);
        if (length == 0) {
            delete_line(static_cast<uint16_t>(number));
            continue;
        }
        if (length > max_line_length) {
            return raise(error_code::syntax, static_cast<int32_t>(number));
        }
        if (!store_line(static_cast<uint16_t>(number), p, length)) {
            return false;
        }
    }
    return true;
}

bool interpreter::run() noexcept {
    clear_error();
    running_ = true;

    // Phase B0: no statement is implemented yet, so the run loop only walks the
    // line table and gives the host its per line tick. Phase B1 replaces the
    // body with token stream execution.
    for (uint16_t index = 0; index < line_count_ && running_; ++index) {
        if (host_.on_tick && !host_.on_tick(host_.user)) {
            running_ = false;
            return true;  // host asked to stop: not an error
        }
    }

    running_ = false;
    return error_ == error_code::none;
}

}  // namespace fmrb_basic
