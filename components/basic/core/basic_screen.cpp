// Text screen: the 28x24 shadow buffer is the single source of truth
// (compat_plan sec 5.1). Drawing is a mirror of it, SCR$ and the debug dump
// read from it, and scrolling happens here rather than in the renderer.

#include "basic_core.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

constexpr uint16_t screen_cells = static_cast<uint16_t>(screen_columns) * screen_rows;

constexpr char hex_digit(uint8_t v) noexcept {
    return static_cast<char>(v < 10 ? '0' + v : 'A' + (v - 10));
}

}  // namespace

uint8_t interpreter::screen_char(uint8_t x, uint8_t y) const noexcept {
    if (!screen_chars_ || x >= screen_columns || y >= screen_rows) {
        return 0;
    }
    return screen_chars_[y * screen_columns + x];
}

uint8_t interpreter::screen_attr(uint8_t x, uint8_t y) const noexcept {
    if (!screen_attrs_ || x >= screen_columns || y >= screen_rows) {
        return 0;
    }
    return screen_attrs_[y * screen_columns + x];
}

void interpreter::screen_set_cell(uint8_t x, uint8_t y, uint8_t code, uint8_t attr) noexcept {
    if (!screen_chars_ || x >= screen_columns || y >= screen_rows) {
        return;
    }
    const uint16_t index = static_cast<uint16_t>(y) * screen_columns + x;
    screen_chars_[index] = code;
    screen_attrs_[index] = attr;
    if (host_.screen_cell) {
        host_.screen_cell(host_.user, x, y, code, attr);
    }
}

void interpreter::screen_clear() noexcept {
    if (!screen_chars_) {
        return;
    }
    for (uint16_t i = 0; i < screen_cells; ++i) {
        screen_chars_[i] = ' ';
        screen_attrs_[i] = 0;
    }
    cursor_x_ = 0;
    cursor_y_ = 0;
    if (host_.screen_fill && host_.screen_fill(host_.user, ' ', 0)) {
        if (host_.screen_present) {
            host_.screen_present(host_.user);
        }
        return;
    }
    screen_refresh();
}

void interpreter::screen_refresh() noexcept {
    if (!screen_chars_ || !host_.screen_cell) {
        return;
    }
    for (uint8_t y = 0; y < screen_rows; ++y) {
        for (uint8_t x = 0; x < screen_columns; ++x) {
            const uint16_t index = static_cast<uint16_t>(y) * screen_columns + x;
            host_.screen_cell(host_.user, x, y, screen_chars_[index], screen_attrs_[index]);
        }
    }
    if (host_.screen_present) {
        host_.screen_present(host_.user);
    }
}

void interpreter::screen_scroll() noexcept {
    if (!screen_chars_) {
        return;
    }
    // Characters and attributes scroll together. On the real hardware the
    // attribute grid covers 2x2 areas, so a one row scroll shifts it by half
    // an area; matching that exactly needs hardware confirmation (see report).
    for (uint16_t i = screen_columns; i < screen_cells; ++i) {
        screen_chars_[i - screen_columns] = screen_chars_[i];
        screen_attrs_[i - screen_columns] = screen_attrs_[i];
    }
    for (uint16_t i = screen_cells - screen_columns; i < screen_cells; ++i) {
        screen_chars_[i] = ' ';
        screen_attrs_[i] = 0;
    }
    screen_refresh();
}

void interpreter::screen_newline() noexcept {
    cursor_x_ = 0;
    if (cursor_y_ + 1 >= screen_rows) {
        screen_scroll();
        cursor_y_ = screen_rows - 1;
    } else {
        ++cursor_y_;
    }
}

void interpreter::screen_put(uint8_t code) noexcept {
    if (!screen_chars_) {
        return;
    }
    if (cursor_x_ >= screen_columns) {
        screen_newline();
    }
    const uint16_t index = static_cast<uint16_t>(cursor_y_) * screen_columns + cursor_x_;
    screen_set_cell(cursor_x_, cursor_y_, code, screen_attrs_[index]);
    ++cursor_x_;
}

void interpreter::service_frames() noexcept {
    if (!host_.ticks_ms) {
        return;  // no clock: the host drives frames itself (tests)
    }

    const uint32_t now = host_.ticks_ms(host_.user);
    const uint32_t elapsed_ms = now - last_clock_ms_;
    last_clock_ms_ = now;
    frame_accum_us_ += elapsed_ms * 1000u;

    // Run the frames that are due. A long stall (breakpoint, heavy statement)
    // must not turn into a burst of catch up frames, so the backlog is capped.
    uint8_t ran = 0;
    while (frame_accum_us_ >= frame_period_us) {
        frame_accum_us_ -= frame_period_us;
        frame_tick();
        if (++ran >= max_catchup_frames) {
            frame_accum_us_ = 0;
            break;
        }
    }
    if (ran > 0) {
        frame_statements_ = 0;
    }

    // Real machine pacing: once the frame's statement budget is spent, wait
    // for the next frame instead of running ahead of the original hardware.
    if (statements_per_frame_ == 0 || frame_statements_ < statements_per_frame_ ||
        !host_.sleep_ms) {
        return;
    }
    while (frame_statements_ >= statements_per_frame_ && running_) {
        host_.sleep_ms(host_.user, 1);
        const uint32_t after = host_.ticks_ms(host_.user);
        frame_accum_us_ += (after - last_clock_ms_) * 1000u;
        last_clock_ms_ = after;
        if (frame_accum_us_ >= frame_period_us) {
            frame_accum_us_ -= frame_period_us;
            frame_tick();
            frame_statements_ = 0;
        }
        if (host_.on_tick && !host_.on_tick(host_.user)) {
            running_ = false;
        }
    }
}

void interpreter::frame_tick() noexcept {
    ++frame_count_;
    advance_moves();
    // The screen is presented once per frame rather than per changed cell.
    if (host_.screen_present) {
        host_.screen_present(host_.user);
    }
}

bool interpreter::moves_active() const noexcept {
    for (uint8_t i = 0; i < move_count; ++i) {
        if (moves_[i].defined && moves_[i].active) {
            return true;
        }
    }
    return false;
}

void interpreter::screen_send_palette() noexcept {
    if (!host_.screen_palette) {
        return;
    }
    for (uint8_t attr = 0; attr < 4; ++attr) {
        host_.screen_palette(host_.user, attr, backdrop_, palette_[attr][0],
                             palette_[attr][1], palette_[attr][2]);
    }
}

namespace {

/// First character code of each kana row (core_spec sec 12): A I U E O.
struct kana_row {
    uint8_t consonant;  ///< 0 for the bare vowel row
    uint8_t first;      ///< code of the "a" column
    uint8_t columns;    ///< how many of the five columns exist
};

// Every row is contiguous in the code table, so one base code per consonant is
// enough. YA/YU/YO and WA/WO are the only gappy rows.
constexpr kana_row kana_rows[] = {
    {0, 96, 5},    {'K', 101, 5}, {'S', 106, 5}, {'T', 111, 5}, {'N', 116, 5},
    {'H', 121, 5}, {'M', 126, 5}, {'R', 134, 5}, {'G', 151, 5}, {'Z', 156, 5},
    {'D', 161, 5}, {'B', 166, 5}, {'P', 171, 5},
};

constexpr uint8_t kana_ya[3] = {131, 132, 133};  // YA YU YO
constexpr uint8_t kana_wa = 139;
constexpr uint8_t kana_wo = 141;
constexpr uint8_t kana_n = 140;

/// Column index of a vowel letter, 0xFF when it is not a vowel.
constexpr uint8_t vowel_index(uint8_t c) noexcept {
    switch (c) {
        case 'A': return 0;
        case 'I': return 1;
        case 'U': return 2;
        case 'E': return 3;
        case 'O': return 4;
        default: return 0xFF;
    }
}

}  // namespace

/**
 * Romaji style kana input: a consonant waits for the vowel that completes it,
 * "NN" produces N. Anything else passes through unchanged. This is a
 * deliberately simple scheme; a full JIS kana keyboard layout is a separate
 * job (see the B2 report).
 */
uint8_t interpreter::kana_translate(uint8_t code) noexcept {
    const uint8_t vowel = vowel_index(code);

    if (kana_pending_ != 0) {
        // J and F are the usual romaji spellings of the ZI and HU sounds.
        const uint8_t consonant = (kana_pending_ == 'J')   ? 'Z'
                                  : (kana_pending_ == 'F') ? 'H'
                                                           : kana_pending_;
        kana_pending_ = 0;
        if (consonant == 'N' && code == 'N') {
            return kana_n;
        }
        if (vowel != 0xFF) {
            if (consonant == 'Y') {
                return (vowel == 0 || vowel == 2 || vowel == 4)
                           ? kana_ya[vowel == 0 ? 0 : (vowel == 2 ? 1 : 2)]
                           : 0;
            }
            if (consonant == 'W') {
                return (vowel == 0) ? kana_wa : ((vowel == 4) ? kana_wo : 0);
            }
            for (const kana_row& row : kana_rows) {
                if (row.consonant == consonant && vowel < row.columns) {
                    return static_cast<uint8_t>(row.first + vowel);
                }
            }
            return 0;
        }
        // Not a vowel: drop the pending consonant and reconsider this key.
    }

    if (vowel != 0xFF) {
        return static_cast<uint8_t>(kana_rows[0].first + vowel);
    }
    switch (code) {
        case 'K': case 'S': case 'T': case 'N': case 'H': case 'M':
        case 'Y': case 'R': case 'W': case 'G': case 'Z': case 'D':
        case 'B': case 'P': case 'J': case 'F':
            kana_pending_ = code;
            return 0;
        default:
            return code;
    }
}

void interpreter::push_key(uint8_t code) noexcept {
    if (code == kana_toggle_key) {
        kana_mode_ = !kana_mode_;
        kana_pending_ = 0;
        return;
    }
    if (kana_mode_) {
        code = kana_translate(code);
        if (code == 0) {
            return;  // waiting for the rest of the sequence
        }
    }
    push_key_raw(code);
}

void interpreter::push_key_raw(uint8_t code) noexcept {
    const uint8_t next = static_cast<uint8_t>((key_tail_ + 1) % key_queue_size);
    if (next == key_head_) {
        return;  // queue full: drop the oldest key press
    }
    key_queue_[key_tail_] = code;
    key_tail_ = next;
}

bool interpreter::next_key(uint8_t* out) noexcept {
    if (key_head_ == key_tail_) {
        return false;
    }
    *out = key_queue_[key_head_];
    key_head_ = static_cast<uint8_t>((key_head_ + 1) % key_queue_size);
    return true;
}

void interpreter::screen_dump(uint16_t tag, bool with_colors) noexcept {
    if (!host_.debug_line || !screen_chars_) {
        return;
    }

    // Every line carries the "SCRD|<tag>|" prefix so the harness can pull a
    // dump out of an interleaved log with a line filter instead of trying to
    // slice a range (phase_b0_report sec 3.3).
    char* line = reinterpret_cast<char*>(work_code_);
    size_t at = 0;
    auto put = [&](char ch) noexcept {
        if (at + 1 < work_capacity) {
            line[at++] = ch;
        }
    };
    auto put_text = [&](const char* text) noexcept {
        for (const char* p = text; *p != '\0'; ++p) {
            put(*p);
        }
    };
    auto put_u16 = [&](uint16_t value) noexcept {
        char digits[5];
        uint8_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value != 0 && n < sizeof(digits));
        while (n > 0) {
            put(digits[--n]);
        }
    };
    auto start_line = [&]() noexcept {
        at = 0;
        put_text("SCRD|");
        put_u16(tag);
        put('|');
    };
    auto end_line = [&]() noexcept {
        line[at] = '\0';
        host_.debug_line(host_.user, line);
    };

    uint16_t body_lines = 0;

    start_line();
    put_text("BEGIN 28x24 bg=0");
    end_line();

    for (uint8_t y = 0; y < screen_rows; ++y) {
        // Character codes in hex: this row is what comparisons use, because
        // kana and background patterns have no faithful ASCII form.
        start_line();
        put('T');
        put(static_cast<char>('0' + y / 10));
        put(static_cast<char>('0' + y % 10));
        for (uint8_t x = 0; x < screen_columns; ++x) {
            const uint8_t code = screen_chars_[y * screen_columns + x];
            put(' ');
            put(hex_digit(static_cast<uint8_t>(code >> 4)));
            put(hex_digit(static_cast<uint8_t>(code & 0x0F)));
        }
        end_line();
        ++body_lines;

        // Readable transliteration for eyeballing diffs, always 28 columns.
        start_line();
        put('A');
        put(static_cast<char>('0' + y / 10));
        put(static_cast<char>('0' + y % 10));
        put(' ');
        put('|');
        for (uint8_t x = 0; x < screen_columns; ++x) {
            const uint8_t code = screen_chars_[y * screen_columns + x];
            // Only the ASCII part of the code table (0x20-0x5F) is shown as
            // itself. Kana starts at 0x60 and would otherwise be mistaken for
            // lower case letters, which this character set does not have.
            put((code >= 0x20 && code <= 0x5F) ? static_cast<char>(code) : '.');
        }
        put('|');
        end_line();
        ++body_lines;

        if (with_colors) {
            start_line();
            put('C');
            put(static_cast<char>('0' + y / 10));
            put(static_cast<char>('0' + y % 10));
            put(' ');
            for (uint8_t x = 0; x < screen_columns; ++x) {
                put(static_cast<char>('0' + (screen_attrs_[y * screen_columns + x] & 3)));
            }
            end_line();
            ++body_lines;
        }
    }

    // The line count lets the harness notice a dropped line, which a plain
    // line filter cannot detect on its own.
    start_line();
    put_text("END ");
    put_u16(body_lines);
    end_line();
}

}  // namespace fmrb_basic
