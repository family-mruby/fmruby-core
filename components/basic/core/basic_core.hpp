/**
 * @file basic_core.hpp
 * @brief Family BASIC compatible interpreter core (host independent)
 *
 * The core is pure C++ (C++20, no exceptions / no RTTI). It must not include
 * ESP-IDF, FreeRTOS or fmruby headers: everything platform specific (memory,
 * character output, line input, time, error reporting) is supplied by the
 * embedder through ::fmrb_basic::basic_host_t. The same translation units
 * therefore build both inside the firmware app task and in the host golden
 * test runner (components/basic/test), where only a host g++ is needed.
 *
 * Internally every character is a Family BASIC character code (core_spec
 * sec 12 table B). UTF-8 conversion happens on the way in (load) and on the
 * way out (put_char), see basic_charset.hpp.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "basic_tokens.hpp"

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

/// Longest string a Family BASIC value can hold (core_spec sec 1, ST error).
inline constexpr uint8_t max_string_len = 31;

/// Text screen width, which also fixes the PRINT comma zones (core_spec sec 6).
inline constexpr uint8_t screen_columns = 28;
/// Text screen height (core_spec sec 13).
inline constexpr uint8_t screen_rows = 24;
/// PRINT comma zone width: 4 zones of 8 columns starting at 0, 8, 16, 24.
inline constexpr uint8_t print_zone_width = 8;
/// Colour attributes cover 2x2 character areas (core_spec sec 7, COLOR).
inline constexpr uint8_t color_area_size = 2;
/// Character that toggles kana input (TAB). See interpreter::push_key().
inline constexpr uint8_t kana_toggle_key = 0x09;
/// Sprites the hardware exposes to BASIC (core_spec sec 8).
inline constexpr uint8_t sprite_count = 8;
/// DEF MOVE animation slots (core_spec sec 9).
inline constexpr uint8_t move_count = 8;
/// Sprite plane origin measured in text plane pixels (core_spec sec 8,
/// provisional: sprite x = 8*X + 16, y = 8*Y + 24).
inline constexpr int16_t sprite_origin_x = 16;
inline constexpr int16_t sprite_origin_y = 24;
/// Function keys held by KEY / KEYLIST (core_spec sec 5).
inline constexpr uint8_t function_key_count = 8;
/// Longest function key definition (core_spec sec 5: 15 characters).
inline constexpr uint8_t function_key_len = 15;

/**
 * @brief A BASIC value: 16 bit integer or a string of at most 31 characters.
 *
 * Strings are held inline. Family BASIC caps every string at 31 characters, so
 * a fixed cell removes the need for a string heap and its compaction, and
 * makes memory use exactly predictable (compat_plan sec 4.1-3 asked for a
 * fixed size pool; this is that pool, spread over the values themselves).
 */
struct basic_value {
    bool is_string = false;
    int16_t num = 0;
    uint8_t len = 0;
    uint8_t str[max_string_len] = {};
};

/// What the renderer needs to show one sprite (DEF SPRITE or DEF MOVE).
struct basic_sprite_state {
    uint8_t index;    ///< slot 0-7
    bool defined;     ///< a definition exists
    bool visible;     ///< currently on screen
    bool size16;      ///< 16x16 (four tiles) instead of 8x8
    bool behind;      ///< drawn behind the BG plane
    bool flip_x;
    bool flip_y;
    bool table_a;     ///< tiles come from character table A (animation set)
    uint8_t attr;     ///< colour attribute 0-3
    uint8_t tiles[4]; ///< character codes: [0] for 8x8, all four for 16x16
    int16_t x;        ///< sprite plane coordinates (core_spec sec 8)
    int16_t y;
};

/// One DEF MOVE slot: definition plus where the character currently is.
struct basic_move_state {
    bool defined;
    bool active;         ///< still travelling (MOVE(n) reports -1)
    bool visible;
    bool behind;
    uint8_t character;   ///< built in animation character 0-15
    uint8_t direction;   ///< 0 = stopped, 1-8 clockwise from up
    uint8_t speed;       ///< C: two dots every 2C frames
    uint8_t distance;    ///< D: total travel is 2D dots
    uint8_t attr;        ///< colour attribute 0-3
    uint16_t remaining;  ///< dots of travel left
    uint16_t step_counter;
    uint8_t anim_counter;
    uint8_t anim_phase;
    int16_t x;
    int16_t y;
};

/// One argument handed to basic_host_t::ext_statement.
struct basic_arg {
    bool is_string;
    int16_t num;
    const uint8_t* str;  ///< Family BASIC character codes, not NUL terminated
    uint8_t len;
};

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
    /// Emit one byte of UTF-8 program output (PRINT, error messages).
    void (*put_char)(void* user, char c);
    /**
     * Read one line of user input (INPUT / LINPUT) as UTF-8, without the
     * terminator.
     * @return number of bytes stored, or a negative value when no input is
     *         available (end of input, aborted).
     */
    int (*read_line)(void* user, char* buf, size_t buf_size);
    /// Monotonic milliseconds, used by PAUSE and (from B3) the PLAY tempo.
    uint32_t (*ticks_ms)(void* user);
    /// Yield for @p ms milliseconds (PAUSE). May be a no-op in tests.
    void (*sleep_ms)(void* user, uint32_t ms);
    /**
     * Cooperative execution hook, called every few statements. This is where
     * the embedder drives its 1/60 s work (sprite auto move, sound, input
     * polling) and where it can ask the program to stop.
     * @return false to abort the running program.
     */
    bool (*on_tick)(void* user);
    /// Notify the embedder about an error, after the core has printed it.
    void (*on_error)(void* user, error_code code, int32_t line_number);
    /**
     * Statement the core recognizes but does not implement itself: the fmruby
     * specific graphics statements, and the screen / sprite / sound statements
     * until the phase that implements them.
     * @param tk   keyword token value (::fmrb_basic::token)
     * @return false when the embedder does not handle @p tk; the core then
     *         raises IL.
     */
    bool (*ext_statement)(void* user, uint8_t tk, const basic_arg* args, uint8_t argc);
    /**
     * One text screen cell changed. The shadow buffer inside the core is the
     * truth; the embedder redraws just this cell (8x8 pixels).
     * @param attr colour attribute of the cell, 0-3
     */
    void (*screen_cell)(void* user, uint8_t x, uint8_t y, uint8_t code, uint8_t attr);
    /// The screen is consistent again: a good moment to present the canvas.
    void (*screen_present)(void* user);
    /**
     * Every cell became @p code with attribute @p attr (CLS). Renderers can
     * do this with one fill instead of 672 cell updates.
     * @return false when the embedder wants the cells reported one by one.
     */
    bool (*screen_fill)(void* user, uint8_t code, uint8_t attr);
    /**
     * Play a converted FMSQ sequence (PLAY). The data stays valid only for the
     * duration of the call, so the embedder must copy or forward it.
     * @return false when the backend refused it.
     */
    bool (*audio_play)(void* user, const uint8_t* data, uint16_t len);
    /// Short fixed tone (BEEP).
    void (*audio_beep)(void* user);
    /// FILTER: tint the whole BG plane, 0 = none, 1-7 the filter colours.
    void (*screen_filter)(void* user, uint8_t color);
    /// The text plane switched character table (CGEN): true = table A.
    void (*screen_charset)(void* user, bool table_a);
    /// A sprite was defined, moved, shown or hidden.
    void (*sprite_update)(void* user, const basic_sprite_state* sprite);
    /// SPRITE ON / SPRITE OFF: show or hide the whole sprite plane.
    void (*sprite_plane)(void* user, bool on);
    /// One line of debug output (_SCRDUMP). Never program output.
    void (*debug_line)(void* user, const char* text);
    /**
     * Write a program (SAVE). The embedder decides where programs live and how
     * @p name maps to a file; @p text is the listing, NUL terminated.
     * @return false when the write failed (the core raises TP).
     */
    bool (*program_write)(void* user, const char* name, const char* text);
    /**
     * Text palette changed (PALET B, and once at start up). Colour codes are
     * the 0-60 values of core_spec sec 7; the renderer maps them to pixels.
     * @param attr     colour attribute group 0-3
     * @param backdrop backdrop colour code, shared by every group
     * @param c1 c2 c3 the three colours of the group
     */
    void (*screen_palette)(void* user, uint8_t attr, uint8_t backdrop, uint8_t c1,
                           uint8_t c2, uint8_t c3);
    /**
     * Sprite plane palette (PALET S). Same colour codes, but its own bank: the
     * sprite plane has its own palette and FILTER does not tint it.
     */
    void (*sprite_palette)(void* user, uint8_t attr, uint8_t c1, uint8_t c2, uint8_t c3);
    /// Opaque pointer handed back to every callback.
    void* user;
};

/// One stored program line: line number plus a slice of the token arena.
struct program_line {
    uint16_t number;  ///< BASIC line number (0 - 65535)
    uint32_t offset;  ///< byte offset of the crunched tokens in the arena
    uint16_t length;  ///< token byte count, including the trailing eol token
};

/// Storage limits, all taken from the host pool by interpreter::init().
struct basic_config {
    uint16_t line_capacity = 512;      ///< program lines
    uint32_t code_capacity = 8192;     ///< crunched token bytes
    uint16_t var_capacity = 96;        ///< distinct variable names
    uint32_t var_data_capacity = 8192; ///< variable and array storage bytes
    uint8_t for_depth = 16;            ///< nested FOR loops
    uint8_t gosub_depth = 24;          ///< nested GOSUB calls
    uint8_t expr_depth = 24;           ///< expression operand / operator stacks
    /**
     * Statements the program may execute per 1/60 s frame.
     *
     * Family BASIC runs on a 1.79 MHz 6502 interpreting its own tokens, which
     * lands somewhere around 60 statements per frame. A modern build is orders
     * of magnitude faster, and a game written for the original pacing would be
     * unplayable at that speed, so the interpreter spends at most this many
     * statements per frame and then waits for the next one (compat_plan
     * sec 4.1-6). The value is a calibration knob: B5 compares it against real
     * hardware footage. 0 disables the throttle.
     */
    uint16_t statements_per_frame = 60;
};

/// Variable table entry. Storage lives in the variable data arena.
struct basic_var {
    uint8_t name[2];   ///< first two significant characters, 0 padded
    bool is_string;    ///< value type
    uint8_t dims;      ///< 0 = scalar, 1 or 2 = array rank
    uint16_t dim[2];   ///< highest valid subscript per dimension (0 based)
    uint32_t offset;   ///< byte offset into the variable data arena
};

/// FOR loop frame.
struct basic_for_frame {
    uint8_t name[2];
    int16_t limit;
    int16_t step;
    uint16_t body_line;  ///< line index of the statement after FOR
    uint16_t body_off;   ///< token offset of the statement after FOR
};

/// GOSUB frame.
struct basic_gosub_frame {
    uint16_t line;
    uint16_t off;
};

/**
 * @brief Family BASIC interpreter instance.
 *
 * Construction never allocates, so the embedder can placement new the object
 * into pool memory and then call init().
 */
class interpreter {
public:
    /// Largest line number accepted by the loader (core_spec sec 1).
    static constexpr uint32_t max_line_number = 65535;
    /// Longest source line accepted by the loader (core_spec sec 1).
    static constexpr size_t max_line_length = 255;

    explicit interpreter(const basic_host_t& host) noexcept;
    ~interpreter() noexcept;

    interpreter(const interpreter&) = delete;
    interpreter& operator=(const interpreter&) = delete;

    /**
     * @brief Allocate program and variable storage through the host.
     * @return false and raises OM when the host cannot supply the memory.
     */
    bool init(const basic_config& config = basic_config{}) noexcept;

    /**
     * @brief Crunch and store a whole program (UTF-8 source).
     *
     * Lines are separated by LF or CRLF and must start with a line number; a
     * line number on its own deletes that line. Raises SN on a malformed line
     * and OM when storage runs out.
     */
    bool load(const char* program) noexcept;

    /// Run the loaded program from the lowest line number.
    bool run() noexcept;

    /// Drop every program line (NEW).
    void clear_program() noexcept;
    /// Drop every variable and array (CLEAR, and the start of RUN).
    void clear_variables() noexcept;

    /// Number of stored program lines.
    uint16_t line_count() const noexcept { return line_count_; }
    /// Line table entry, or NULL when @p index is out of range.
    const program_line* line_at(uint16_t index) const noexcept;

    /**
     * @brief Expand one stored line back to source text (LIST, tests).
     * @return number of bytes written to @p out, excluding the NUL.
     */
    size_t decrunch_line(uint16_t index, char* out, size_t out_size) const noexcept;

    /// Error raised by the last load()/run(), error_code::none when clean.
    error_code last_error() const noexcept { return error_; }
    /// Line number the last error was raised on, -1 when not line related.
    int32_t last_error_line() const noexcept { return error_line_; }
    /// Forget the pending error.
    void clear_error() noexcept;

    /// Bytes still free in the variable data arena (FRE).
    uint32_t free_bytes() const noexcept;

    /**
     * @brief Raise a BASIC error: print it, notify the host, remember it.
     * @param line_number line the error happened on, negative when unknown
     *        (load time), which suppresses the "IN nnn" part.
     * @return always false, so callers can `return raise(...)`.
     */
    bool raise(error_code code, int32_t line_number) noexcept;
    /// raise() for the line currently being executed.
    bool raise_here(error_code code) noexcept;

    // --- output helpers, also used by the statement implementations ---

    // --- text screen (B2) ---

    /// Character code at (x, y), 0 when out of range.
    uint8_t screen_char(uint8_t x, uint8_t y) const noexcept;
    /// Colour attribute (0-3) at (x, y), 0 when out of range.
    uint8_t screen_attr(uint8_t x, uint8_t y) const noexcept;
    /// Cursor column (POS) and row (CSRLIN).
    uint8_t cursor_x() const noexcept { return cursor_x_; }
    uint8_t cursor_y() const noexcept { return cursor_y_; }
    /// Clear the screen and home the cursor (CLS).
    void screen_clear() noexcept;
    /// Repaint every cell through basic_host_t::screen_cell (window restore).
    void screen_refresh() noexcept;
    /// Dump the screen through basic_host_t::debug_line (_SCRDUMP).
    void screen_dump(uint16_t tag, bool with_colors) noexcept;
    /// Push the whole text palette to the host (start up, window restore).
    void screen_send_palette() noexcept;
    /// Tell the host which character table the text plane uses (CGEN).
    void notify_charset() noexcept;

    /// One 1/60 s frame of work: sprite auto move, collision, present.
    void frame_tick() noexcept;
    /// True while a DEF MOVE animation still has movement left.
    bool moves_active() const noexcept;
    /// Frames elapsed since run() started (diagnostics, benchmarks).
    uint32_t frame_count() const noexcept { return frame_count_; }

    /**
     * @brief Queue one key press for INKEY$, called by the embedder.
     *
     * The kana toggle character (::fmrb_basic::kana_toggle_key) switches kana
     * input on and off instead of being queued; while it is on, letters are
     * combined into kana (see basic_screen.cpp).
     */
    void push_key(uint8_t code) noexcept;
    /// True while kana input is active.
    bool kana_mode() const noexcept { return kana_mode_; }
    /**
     * @brief Set the controller state the embedder sees.
     *
     * Bits follow core_spec sec 11 / compat_plan sec 3.3: stick 1 = right,
     * 2 = left, 4 = down, 8 = up; trigger 1 = START, 2 = SELECT, 4 = B, 8 = A.
     * @param player 0 = controller I, 1 = controller II
     */
    void set_pad(uint8_t player, uint8_t stick, uint8_t trigger) noexcept;

    /// True while CLICK ON is in effect (key click sound, B3 wires the sound).
    bool click_enabled() const noexcept { return click_on_; }

    /// Write one Family BASIC character code as UTF-8 through the host.
    void put_fb_char(uint8_t code) noexcept;
    /// Write a NUL terminated ASCII string (already Family BASIC compatible).
    void print(const char* text) noexcept;
    /// print() followed by a newline.
    void print_line(const char* text) noexcept;
    /// Write a signed decimal number.
    void print_number(int32_t value) noexcept;
    /// End the current output line and reset the cursor column.
    void print_newline() noexcept;

private:
    // --- program storage (basic_core.cpp) ---
    bool store_line(uint16_t number, const uint8_t* code, uint16_t length) noexcept;
    void delete_line(uint16_t number) noexcept;
    uint16_t lower_bound(uint16_t number) const noexcept;
    bool code_append(const uint8_t* code, uint16_t length, uint32_t* out_offset) noexcept;
    /// Line table index for @p number, or -1 when there is no such line.
    int32_t find_line(uint16_t number) const noexcept;

    // --- tokenizer (basic_tokenizer.cpp) ---
    /**
     * Crunch one source line (Family BASIC codes, no line number) into
     * @p out. Returns the byte count, or -1 after raising an error.
     */
    int32_t crunch_line(const uint8_t* src, size_t len, uint8_t* out, size_t out_size) noexcept;

    // --- token stream access (inline, basic_internal.hpp) ---
    const uint8_t* code_at(uint16_t line_index) const noexcept;
    uint8_t peek_byte() const noexcept;
    uint8_t read_byte() noexcept;
    token peek_token() const noexcept { return static_cast<token>(peek_byte()); }
    void skip_token() noexcept;
    bool accept(token tk) noexcept;
    bool expect(token tk) noexcept;
    bool at_statement_end() const noexcept;
    /// True when the token after the current one is '(' (MOVE statement vs function).
    bool peek_ahead_is_lparen() const noexcept;
    void skip_to_eol() noexcept;
    void skip_to_statement_end() noexcept;
    uint16_t current_line_number() const noexcept;

    // --- expressions (basic_expr.cpp) ---
    bool eval(basic_value* out) noexcept;
    bool eval_number(int16_t* out) noexcept;
    bool eval_string(basic_value* out) noexcept;
    bool apply_binary(token op, basic_value* lhs, const basic_value* rhs) noexcept;
    bool apply_unary(token op, basic_value* v) noexcept;
    bool call_builtin(token fn, basic_value* args, uint8_t argc, basic_value* out) noexcept;

    // --- variables (basic_vars.cpp) ---
    basic_var* find_var(uint8_t n0, uint8_t n1, bool is_string, bool array) noexcept;
    basic_var* create_var(uint8_t n0, uint8_t n1, bool is_string, uint8_t dims,
                          const uint16_t* sizes) noexcept;
    /// Storage address of a scalar or array element, NULL after raising.
    uint8_t* var_cell(basic_var* var, const uint16_t* subs, uint8_t nsubs) noexcept;
    bool store_value(uint8_t* cell, bool is_string, const basic_value* value) noexcept;
    void load_value(const uint8_t* cell, bool is_string, basic_value* out) noexcept;

    // --- statements (basic_exec.cpp) ---
    bool exec_statement() noexcept;
    bool st_assign(token var_tk) noexcept;
    bool st_print() noexcept;
    bool st_input(bool line_input) noexcept;
    bool st_if() noexcept;
    bool st_for() noexcept;
    bool st_next() noexcept;
    bool st_goto() noexcept;
    bool st_gosub() noexcept;
    bool st_return() noexcept;
    bool st_on() noexcept;
    bool st_on_error() noexcept;
    bool st_resume() noexcept;
    bool st_error() noexcept;
    /// POKE / PEEK backing store. Returns nullptr when the address is not mapped.
    uint8_t* mem_cell(uint16_t address, bool for_write) noexcept;
    void mem_warn_once(uint16_t address, bool write) noexcept;
    bool st_screen() noexcept;
    bool st_filter() noexcept;
    bool st_bgget() noexcept;
    bool st_bgput() noexcept;
    bool st_load(bool verify) noexcept;
    /**
     * Announce that the program is (or is no longer) waiting for a key press.
     *
     * Emitted through basic_host_t::debug_line with its own prefix, so a test
     * harness can wait for the program to be ready before injecting keys
     * instead of racing its start up (B4 T4-9). Edge triggered.
     */
    void input_wait(bool waiting) noexcept;
    bool st_save() noexcept;
    /// Read the name operand of LOAD / SAVE into name_buf (max 16 chars + NUL).
    bool read_program_name(char* out, size_t out_size, bool* present) noexcept;
    /// Render the whole program as a listing into buf; false when it does not fit.
    bool listing(char* buf, size_t buf_size, size_t* out_len) noexcept;
    bool st_dim() noexcept;
    bool st_read() noexcept;
    bool st_restore() noexcept;
    bool st_swap() noexcept;
    bool st_pause() noexcept;
    bool st_locate() noexcept;
    bool st_color() noexcept;
    bool st_key() noexcept;
    bool st_click() noexcept;
    bool st_scrdump() noexcept;
    bool st_palet() noexcept;
    bool st_def() noexcept;
    bool st_sprite() noexcept;
    bool st_move_group(token tk) noexcept;
    bool st_position() noexcept;
    bool st_play() noexcept;
    bool st_cgen() noexcept;
    bool st_cgset() noexcept;
    /// True when the BG plane draws from character table A (CGEN 0 / 1).
    bool bg_uses_table_a() const noexcept { return cgen_ == 0 || cgen_ == 1; }
    /// True when the sprite plane draws from character table A (CGEN 0 / 2).
    bool sprites_use_table_a() const noexcept { return cgen_ == 0 || cgen_ == 2; }
    /// Re-send every sprite (palette or character table changed).
    void refresh_sprites() noexcept;
    /// Load the colour set CGSET selected into the working palettes.
    void load_palette_bank() noexcept;
    bool st_beep() noexcept;
    /// Convert an MML string to an FMSQ sequence; returns the byte count.
    uint16_t mml_to_fmsq(const uint8_t* mml, uint8_t mml_len, uint8_t* out,
                         uint16_t out_capacity) noexcept;
    bool parse_slot_list(uint8_t* slots, uint8_t* count) noexcept;
    void notify_sprite(uint8_t index) noexcept;
    void notify_move(uint8_t index) noexcept;
    void advance_moves() noexcept;
    int16_t move_crash(uint8_t index) const noexcept;
    bool st_ext(token tk) noexcept;

    // --- text screen internals (basic_screen.cpp) ---
    void screen_put(uint8_t code) noexcept;
    void screen_newline() noexcept;
    void screen_scroll() noexcept;
    void screen_set_cell(uint8_t x, uint8_t y, uint8_t code, uint8_t attr) noexcept;
    bool next_key(uint8_t* out) noexcept;
    void push_key_raw(uint8_t code) noexcept;
    /// Run the frames that have come due, and throttle to real machine pacing.
    void service_frames() noexcept;
    uint8_t kana_translate(uint8_t code) noexcept;
    bool jump_to_line(uint16_t number) noexcept;
    bool skip_for_body(const uint8_t* name) noexcept;
    bool read_data_value(bool want_string, basic_value* out) noexcept;
    bool parse_var_target(token var_tk, basic_var** out_var, uint8_t** out_cell) noexcept;

    basic_host_t host_;

    // program
    program_line* lines_ = nullptr;
    uint16_t line_capacity_ = 0;
    uint16_t line_count_ = 0;
    uint8_t* code_ = nullptr;
    uint32_t code_capacity_ = 0;
    uint32_t code_used_ = 0;

    // variables
    basic_var* vars_ = nullptr;
    uint16_t var_capacity_ = 0;
    uint16_t var_count_ = 0;
    uint8_t* var_data_ = nullptr;
    uint32_t var_data_capacity_ = 0;
    uint32_t var_data_used_ = 0;

    // stacks
    basic_for_frame* for_stack_ = nullptr;
    uint8_t for_depth_ = 0;
    uint8_t for_top_ = 0;
    basic_gosub_frame* gosub_stack_ = nullptr;
    uint8_t gosub_depth_ = 0;
    uint8_t gosub_top_ = 0;
    basic_value* operand_stack_ = nullptr;
    uint8_t expr_depth_ = 0;

    // execution position
    uint16_t pc_line_ = 0;
    uint16_t pc_off_ = 0;
    bool running_ = false;
    bool jumped_ = false;

    // DATA pointer
    uint16_t data_line_ = 0;
    uint16_t data_off_ = 0;
    uint8_t data_pos_ = 0;
    bool data_valid_ = false;

    // Scratch buffers, taken from the pool rather than the C stack (00_common
    // memory design rule 2: only small hot locals may live on the stack).
    // work_src_ holds one source / input line, work_code_ its crunched form.
    static constexpr size_t work_capacity = 288;
    uint8_t* work_src_ = nullptr;
    uint8_t* work_code_ = nullptr;

    // text screen: the shadow buffer is the truth, drawing only mirrors it
    // (compat_plan sec 5.1). Both arrays are screen_columns * screen_rows.
    uint8_t* screen_chars_ = nullptr;
    uint8_t* screen_attrs_ = nullptr;
    uint8_t cursor_x_ = 0;
    uint8_t cursor_y_ = 0;

    // Sprite plane. DEF SPRITE slots and DEF MOVE slots are kept apart: the
    // spec numbers them separately and a 16x16 animation character would need
    // several hardware sprites anyway (see the B3 report).
    basic_sprite_state sprites_[sprite_count] = {};
    basic_move_state moves_[move_count] = {};
    bool sprite_plane_on_ = false;

    // PLAY converts into this buffer, allocated on first use.
    static constexpr uint16_t audio_buffer_size = 2048;
    uint8_t* audio_buffer_ = nullptr;

    // Controller state, refreshed by the embedder from HID events.
    uint8_t pad_stick_[2] = {0, 0};
    uint8_t pad_trigger_[2] = {0, 0};

    // Kana input: the pending consonant of a two key romaji sequence.
    bool kana_mode_ = false;
    uint8_t kana_pending_ = 0;

    // INKEY$ queue, filled by the embedder from HID events.
    static constexpr uint8_t key_queue_size = 16;
    uint8_t key_queue_[key_queue_size] = {};
    uint8_t key_head_ = 0;
    uint8_t key_tail_ = 0;

    // Palettes: 4 attribute groups of 3 colour codes each for the BG plane and
    // for the sprite plane, plus the shared backdrop (core_spec sec 7).
    uint8_t palette_[4][3] = {};
    uint8_t sprite_palette_[4][3] = {};
    uint8_t backdrop_ = 0;
    // CGEN selects which character table each plane draws from; CGSET selects
    // the palette bank (core_spec sec 7, defaults from sec 16).
    uint8_t cgen_ = 2;
    uint8_t cgset_bg_ = 1;
    uint8_t cgset_sprite_ = 1;

    // KEY / KEYLIST definitions and the CLICK flag.
    uint8_t* function_keys_ = nullptr;  // function_key_count * (1 + function_key_len)
    bool click_on_ = false;

    uint32_t rng_state_ = 0;
    uint32_t statement_count_ = 0;

    // Frame pacing. The interpreter drives 1/60 s frames from the host clock
    // and limits how much program runs inside one frame.
    uint32_t last_clock_ms_ = 0;
    uint32_t frame_accum_us_ = 0;
    uint32_t frame_count_ = 0;
    uint16_t frame_statements_ = 0;
    uint16_t statements_per_frame_ = 60;

    error_code error_ = error_code::none;
    int32_t error_line_ = -1;

    // ON ERROR GOTO (v3_spec). 0 = disarmed. When armed, raise() records the
    // error and asks the run loop to branch instead of reporting, so the
    // handler runs in the same task with the program state intact.
    uint16_t error_handler_line_ = 0;
    bool in_error_handler_ = false;
    bool error_pending_handler_ = false;
    // ERR / ERL survive RESUME, which clears the run state.
    uint8_t err_number_ = 0;
    uint16_t err_line_ = 0;
    // Edge state for the input wait marker (see input_wait).
    bool input_waiting_ = false;

    // TRON / TROFF: print "*line" as each line starts.
    bool trace_on_ = false;
    uint16_t traced_line_ = 0;

    // --- virtual memory map for POKE / PEEK (core_spec sec 14, v3_spec) ---
    // Two plain RAM windows, allocated on first touch so a program that never
    // pokes pays nothing: the machine RAM page group and the user / work RAM.
    // The BG screen window is mapped onto the text shadow buffer instead, which
    // is what makes POKE to the screen behave.
    static constexpr uint16_t mem_low_base = 0x0000;
    static constexpr uint16_t mem_low_size = 0x0800;   // 0x0000-0x07FF
    static constexpr uint16_t mem_user_base = 0x6000;
    static constexpr uint16_t mem_user_size = 0x2000;  // 0x6000-0x7FFF
    static constexpr uint16_t mem_sys_first = 0x7000;  // 0x7000-0x703F: POKE forbidden
    static constexpr uint16_t mem_sys_last = 0x703F;
    static constexpr uint16_t mem_bg_base = 0xD000;    // nametable, 32 columns
    static constexpr uint8_t mem_bg_stride = 32;
    uint8_t* mem_low_ = nullptr;
    uint8_t* mem_user_ = nullptr;
    // One warning per 256 byte page keeps an unmapped POKE loop from flooding
    // the log while still telling the user which area a program wanted.
    uint8_t mem_warned_[32] = {};

    // --- second BG plane and the BG snapshot (T4-5) ---
    // SCREEN picks which plane is displayed and which one PRINT writes to. The
    // shadow buffer pointers above always refer to the active plane; the other
    // plane lives here and the two swap.
    uint8_t* plane_b_chars_ = nullptr;
    uint8_t* plane_b_attrs_ = nullptr;
    uint8_t screen_display_ = 0;
    uint8_t screen_active_ = 0;
    // BGGET copies the active plane here (user RAM on the real machine), BGPUT
    // copies it back. BACKUP is a no-op: /home is already persistent.
    uint8_t* bg_snapshot_ = nullptr;
    bool bg_snapshot_valid_ = false;
    // FILTER tint (0-7). Kept so PEEK-like state and a later renderer can use
    // it; the current renderer has no tint stage.
    uint8_t filter_color_ = 0;
};

}  // namespace fmrb_basic
