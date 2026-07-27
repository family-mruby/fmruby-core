/**
 * @file basic_core.hpp
 * @brief Family BASIC compatible interpreter core (host independent)
 *
 * The core is pure C++ (C++20, no exceptions / no RTTI). It must not include
 * ESP-IDF, FreeRTOS or fmruby headers: everything platform specific (memory,
 * character output, line input, time, error reporting) is supplied by the
 * embedder through ::fmrb_basic::basic_host_t. The same translation unit
 * therefore builds both inside the firmware app task and in the host golden
 * test runner (components/basic/test), where only a host g++ is needed.
 *
 * Phase B0 provides the skeleton: the host interface, the error model and a
 * program line table. Statement execution is added in Phase B1.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace fmrb_basic {

/**
 * @brief Family BASIC error codes.
 *
 * Values are the error numbers of the Family BASIC V3 error table
 * (doc/fmrb_basic/spec/family_basic_v3_spec.md). ERR returns this number and
 * the two letter mnemonic is what the screen shows, so the numbering must not
 * be reordered.
 */
enum class error_code : uint8_t {
    next_without_for = 0,    ///< NF
    syntax,                  ///< SN
    return_without_gosub,    ///< RG
    out_of_data,             ///< OD
    illegal_function_call,   ///< IL
    overflow,                ///< OV
    out_of_memory,           ///< OM
    undefined_line,          ///< UL
    subscript_out_of_range,  ///< SO
    duplicate_definition,    ///< DD
    division_by_zero,        ///< DZ
    type_mismatch,           ///< TM
    string_too_long,         ///< ST
    formula_too_complex,     ///< FT
    cant_continue,           ///< CC
    undefined_function,      ///< UF
    missing_operand,         ///< MO
    tape_read,               ///< TP
    no_resume,               ///< NR
    resume_without_error,    ///< RE
    no_bg_data,              ///< NB
    unprintable,             ///< UP
    none = 0xFF,             ///< no error pending
};

/// Number of defined error codes (0 - 21).
inline constexpr size_t error_code_count = 22;

/// Two letter mnemonic as shown on screen ("SN"), "UP" for out of range codes.
const char* error_mnemonic(error_code code) noexcept;

/// Full error message ("Syntax error"), for logs and the report path.
const char* error_message(error_code code) noexcept;

/**
 * @brief Services the embedder must provide to the interpreter core.
 *
 * All callbacks receive @ref user as their first argument. Memory comes from
 * the caller's pool (fmrb_malloc on target, malloc in the test runner); the
 * core never calls global operator new / malloc itself.
 */
struct basic_host_t {
    /// Allocate @p size bytes, NULL on failure.
    void* (*alloc)(void* user, size_t size);
    /// Release a block previously returned by alloc(). NULL is a no-op.
    void (*dealloc)(void* user, void* ptr);
    /// Emit one character of program output (PRINT, error messages).
    void (*put_char)(void* user, char c);
    /**
     * Read one line of user input (INPUT / LINPUT) without the terminator.
     * @return number of characters stored, or a negative value when no input
     *         is available (end of input, aborted).
     */
    int (*read_line)(void* user, char* buf, size_t buf_size);
    /// Monotonic milliseconds, used by WAIT / PAUSE and the PLAY tempo.
    uint32_t (*ticks_ms)(void* user);
    /**
     * Cooperative execution hook, called by the run loop. This is where the
     * embedder drives its 1/60 s work (sprite auto move, sound, input polling)
     * and where it can ask the program to stop.
     * @return false to abort the running program.
     */
    bool (*on_tick)(void* user);
    /// Notify the embedder about an error, after the core has printed it.
    void (*on_error)(void* user, error_code code, int32_t line_number);
    /// Opaque pointer handed back to every callback.
    void* user;
};

/// One stored program line: line number plus a slice of the text arena.
struct program_line {
    uint16_t number;  ///< BASIC line number (0 - 65535)
    uint32_t offset;  ///< byte offset of the statement text in the arena
    uint16_t length;  ///< statement text length in bytes (no terminator)
};

/**
 * @brief Family BASIC interpreter instance.
 *
 * Construction never allocates, so the embedder can placement new the object
 * into pool memory and then call init(). The program is held as a line table
 * sorted by line number plus a text arena; Phase B1 replaces the raw text with
 * a crunched token stream, keeping this interface.
 */
class interpreter {
public:
    /// Largest line number accepted by the loader (spec sec 1).
    static constexpr uint32_t max_line_number = 65535;
    /// Longest source line accepted by the loader (spec sec 1: 255 columns).
    static constexpr size_t max_line_length = 255;
    /// Default program capacity used by init() when the caller passes 0.
    static constexpr uint16_t default_line_capacity = 512;
    /// Default text arena size in bytes used by init() when the caller passes 0.
    static constexpr size_t default_text_capacity = 8192;

    explicit interpreter(const basic_host_t& host) noexcept;
    ~interpreter() noexcept;

    interpreter(const interpreter&) = delete;
    interpreter& operator=(const interpreter&) = delete;

    /**
     * @brief Allocate program storage through the host.
     * @param line_capacity maximum number of program lines (0 = default)
     * @param text_capacity text arena size in bytes (0 = default)
     * @return false and raises OM when the host cannot supply the memory.
     */
    bool init(uint16_t line_capacity = 0, size_t text_capacity = 0) noexcept;

    /**
     * @brief Replace the stored program with @p program.
     *
     * @p program is the whole source text, lines separated by LF or CRLF. Each
     * line must start with a line number; a line number on its own deletes
     * that line. Raises SN on a malformed line and OM when storage runs out.
     */
    bool load(const char* program) noexcept;

    /**
     * @brief Run the loaded program from the lowest line number.
     *
     * Phase B0: walks the line table and drives basic_host_t::on_tick without
     * executing statements. Statement execution arrives in Phase B1.
     */
    bool run() noexcept;

    /// Drop every program line (NEW).
    void clear_program() noexcept;

    /// Number of stored program lines.
    uint16_t line_count() const noexcept { return line_count_; }
    /// Line table entry, or NULL when @p index is out of range.
    const program_line* line_at(uint16_t index) const noexcept;
    /// Statement text of a line (not NUL terminated), NULL when out of range.
    const char* line_text(uint16_t index) const noexcept;

    /// Error raised by the last load()/run(), error_code::none when clean.
    error_code last_error() const noexcept { return error_; }
    /// Line number the last error was raised on, -1 when not line related.
    int32_t last_error_line() const noexcept { return error_line_; }
    /// Forget the pending error (CONT / next RUN).
    void clear_error() noexcept;

    /// Write a NUL terminated string through basic_host_t::put_char.
    void print(const char* text) noexcept;
    /// print() followed by a newline.
    void print_line(const char* text) noexcept;
    /// Write a signed decimal number through basic_host_t::put_char.
    void print_number(int32_t value) noexcept;

    /**
     * @brief Raise a BASIC error: print it, notify the host, remember it.
     * @param line_number line the error happened on, negative when unknown
     *        (load time / direct mode), which suppresses the "IN nnn" part.
     * @return always false, so callers can `return raise(...)`.
     */
    bool raise(error_code code, int32_t line_number) noexcept;

private:
    /// Insert or replace one line in the sorted line table.
    bool store_line(uint16_t number, const char* text, size_t length) noexcept;
    /// Remove the line with @p number if it exists.
    void delete_line(uint16_t number) noexcept;
    /// Index of @p number in the line table, or line_count_ for the slot after.
    uint16_t lower_bound(uint16_t number) const noexcept;
    /// Copy @p length bytes into the text arena, false when it is full.
    bool arena_append(const char* text, size_t length, uint32_t* out_offset) noexcept;

    basic_host_t host_;

    program_line* lines_ = nullptr;
    uint16_t line_capacity_ = 0;
    uint16_t line_count_ = 0;

    char* text_ = nullptr;
    size_t text_capacity_ = 0;
    size_t text_used_ = 0;

    error_code error_ = error_code::none;
    int32_t error_line_ = -1;
    bool running_ = false;
};

}  // namespace fmrb_basic
