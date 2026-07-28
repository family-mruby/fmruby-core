// PLAY: MML text to an FMSQ sequence.
//
// The audio backend plays FMSQ (a frame indexed command stream for the NES
// APU), so PLAY converts its MML string once and hands the result over; the
// sequence then runs on the audio side independently of the interpreter, which
// is what makes PLAY asynchronous the way the listings expect
// (phase_b0_report sec 9.1-2). Conversion is deterministic and the golden
// tests compare the produced bytes.

#include "basic_core.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

// --- FMSQ encoding (components/apu_emu/include/fmsq_format.h) --------------

constexpr uint8_t fmsq_ch_pulse1 = 0;
constexpr uint8_t fmsq_ch_pulse2 = 1;
constexpr uint8_t fmsq_ch_triangle = 2;

constexpr uint8_t fmsq_note_on(uint8_t ch) {
    return static_cast<uint8_t>(0x80 | (ch << 4));
}
constexpr uint8_t fmsq_note_off(uint8_t ch) {
    return static_cast<uint8_t>(0x80 | (ch << 4) | 0x01);
}
constexpr uint8_t fmsq_cmd_end = 0xFE;
constexpr uint8_t fmsq_header_size = 12;
constexpr uint16_t fmsq_max_wait = 128;

/**
 * Timer values for octave 5, one per semitone (C, #C, D ... B).
 *
 * The pulse channel counts CPU/(16*f) - 1 at 1.789773 MHz. Lower octaves
 * double the period, so the table only needs one octave. The octave mapping
 * itself is provisional: O0 C is taken as C1 and O5 C as C6, which puts the
 * default O3 at middle C (see the B3 report).
 */
constexpr uint16_t pulse_timer_o5[12] = {106, 100, 94, 89, 84, 79, 75, 70, 66, 63, 59, 56};

/// Note length codes 0-9 in eighths of a quarter note (core_spec sec 10).
constexpr uint8_t length_eighths[10] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};

/**
 * Frames per quarter note for tempo T1-T8.
 *
 * core_spec sec 10 leaves the absolute tempo to the implementation ("T1 fast,
 * T8 slow") and asks for calibration against real recordings. T4 is taken as
 * a 0.5 s quarter note, the rest scale linearly.
 */
constexpr uint16_t quarter_frames(uint8_t tempo) {
    return static_cast<uint16_t>((15u * (tempo == 0 ? 1u : tempo)) / 2u);
}

/// Semitone index of a note letter, 0xFF when it is not one.
constexpr uint8_t note_index(uint8_t c) {
    switch (c) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default: return 0xFF;
    }
}

/// One MML voice: its text, its current parameters and when it needs servicing.
struct mml_voice {
    const uint8_t* text;
    uint8_t len;
    uint8_t pos;

    uint8_t octave;
    uint8_t length;   // note length code 0-9
    uint8_t tempo;    // T1-T8
    uint8_t volume;   // V0-V15
    uint8_t duty;     // Y0-Y3
    bool envelope;    // M1

    uint16_t wait;    // frames until this voice needs its next event
    bool finished;
};

}  // namespace

uint16_t interpreter::mml_to_fmsq(const uint8_t* mml, uint8_t mml_len, uint8_t* out,
                                  uint16_t out_capacity) noexcept {
    if (out_capacity < fmsq_header_size + 4) {
        return 0;
    }

    // Split the string into channels on ':' (core_spec sec 10: up to three,
    // pulse 1, pulse 2 and triangle).
    mml_voice voice[3] = {};
    uint8_t voices = 0;
    uint8_t start = 0;
    for (uint8_t i = 0; i <= mml_len; ++i) {
        if (i == mml_len || mml[i] == ':') {
            if (voices < 3) {
                mml_voice& v = voice[voices++];
                v.text = mml + start;
                v.len = static_cast<uint8_t>(i - start);
                v.pos = 0;
                v.octave = 3;
                v.length = 5;
                v.tempo = 4;
                v.volume = 15;
                v.duty = 2;
                v.envelope = false;
                v.wait = 0;
                v.finished = (v.len == 0);
            }
            start = static_cast<uint8_t>(i + 1);
        }
    }
    if (voices == 0) {
        return 0;
    }

    uint16_t at = fmsq_header_size;
    uint32_t total_frames = 0;
    bool overflow = false;

    auto emit = [&](uint8_t byte) noexcept {
        if (at < out_capacity) {
            out[at++] = byte;
        } else {
            overflow = true;
        }
    };

    // Parse one element of a voice; returns false when the voice is done.
    auto step_voice = [&](mml_voice& v, uint8_t ch) noexcept -> bool {
        while (v.pos < v.len) {
            const uint8_t c = v.text[v.pos];

            // Parameter letters take a numeric argument.
            if (c == 'T' || c == 'V' || c == 'O' || c == 'M' || c == 'Y') {
                ++v.pos;
                uint16_t value = 0;
                bool has_digit = false;
                while (v.pos < v.len && is_digit(v.text[v.pos])) {
                    value = static_cast<uint16_t>(value * 10 + (v.text[v.pos] - '0'));
                    ++v.pos;
                    has_digit = true;
                }
                if (!has_digit) {
                    continue;
                }
                switch (c) {
                    case 'T': v.tempo = static_cast<uint8_t>(value > 8 ? 8 : (value < 1 ? 1 : value)); break;
                    case 'V': v.volume = static_cast<uint8_t>(value > 15 ? 15 : value); break;
                    case 'O': v.octave = static_cast<uint8_t>(value > 5 ? 5 : value); break;
                    case 'M': v.envelope = (value != 0); break;
                    default: v.duty = static_cast<uint8_t>(value & 3); break;
                }
                continue;
            }

            if (c == ' ') {
                ++v.pos;
                continue;
            }

            // A note, optionally sharpened, or a rest. Both take an optional
            // length digit; without one the previous length is reused.
            bool sharp = false;
            if (c == '#') {
                sharp = true;
                ++v.pos;
                if (v.pos >= v.len) {
                    break;
                }
            }
            const uint8_t letter = v.text[v.pos];
            const bool rest = (letter == 'R');
            const uint8_t index = note_index(letter);
            if (!rest && index == 0xFF) {
                ++v.pos;  // unknown character: ignore it
                continue;
            }
            ++v.pos;
            if (v.pos < v.len && is_digit(v.text[v.pos])) {
                v.length = static_cast<uint8_t>(v.text[v.pos] - '0');
                ++v.pos;
            }

            const uint16_t frames = static_cast<uint16_t>(
                (quarter_frames(v.tempo) * length_eighths[v.length]) / 8u);
            v.wait = (frames == 0) ? 1 : frames;

            if (rest) {
                emit(fmsq_note_off(ch));
                return true;
            }

            const uint8_t semitone = static_cast<uint8_t>(index + (sharp ? 1 : 0));
            const uint16_t base = pulse_timer_o5[semitone % 12];
            const uint8_t shift = static_cast<uint8_t>(5 - (v.octave > 5 ? 5 : v.octave));
            uint32_t timer = ((static_cast<uint32_t>(base) + 1u) << shift) - 1u;
            if (ch == fmsq_ch_triangle) {
                // The triangle divides the clock by 32 instead of 16, so the
                // same pitch needs half the timer value.
                timer = (timer + 1u) / 2u - 1u;
            }
            if (timer > 0x7FF) {
                timer = 0x7FF;
            }

            emit(fmsq_note_on(ch));
            emit(static_cast<uint8_t>(timer & 0xFF));
            // Timer high bits plus a full length counter load.
            emit(static_cast<uint8_t>(0xF8 | ((timer >> 8) & 0x07)));
            if (ch == fmsq_ch_triangle) {
                // Linear counter: halt so the note sustains for its length.
                emit(0xFF);
            } else {
                // $4000: duty, length halt, constant volume, volume. M1 hands
                // the volume field to the envelope generator instead.
                const uint8_t vol_env = static_cast<uint8_t>(
                    (v.duty << 6) | (v.envelope ? 0x00 : 0x30) | (v.volume & 0x0F));
                emit(vol_env);
                emit(0x00);  // no sweep
            }
            return true;
        }

        v.finished = true;
        return false;
    };

    // Merge the voices frame by frame: service whoever is due, then wait for
    // the nearest next event.
    while (!overflow) {
        bool any_active = false;
        for (uint8_t i = 0; i < voices; ++i) {
            mml_voice& v = voice[i];
            if (v.finished) {
                continue;
            }
            if (v.wait == 0 && !step_voice(v, i)) {
                emit(fmsq_note_off(i));  // silence the voice when it ends
                continue;
            }
            any_active = true;
        }
        if (!any_active) {
            break;
        }

        uint16_t gap = 0xFFFF;
        for (uint8_t i = 0; i < voices; ++i) {
            if (!voice[i].finished && voice[i].wait < gap) {
                gap = voice[i].wait;
            }
        }
        if (gap == 0 || gap == 0xFFFF) {
            break;  // nothing to wait for: avoid spinning
        }

        uint16_t left = gap;
        while (left > 0 && !overflow) {
            const uint16_t chunk = (left > fmsq_max_wait) ? fmsq_max_wait : left;
            emit(static_cast<uint8_t>(chunk - 1));  // WAIT is 0xxxxxxx, 1-128 frames
            left = static_cast<uint16_t>(left - chunk);
        }
        total_frames += gap;
        for (uint8_t i = 0; i < voices; ++i) {
            if (!voice[i].finished) {
                voice[i].wait = static_cast<uint16_t>(voice[i].wait - gap);
            }
        }
    }

    emit(fmsq_cmd_end);
    if (overflow) {
        return 0;
    }

    const uint16_t data_size = static_cast<uint16_t>(at - fmsq_header_size);
    out[0] = 'F';
    out[1] = 'M';
    out[2] = 'S';
    out[3] = 'Q';
    out[4] = 1;  // version
    out[5] = 0;  // flags
    out[6] = static_cast<uint8_t>(total_frames & 0xFF);
    out[7] = static_cast<uint8_t>((total_frames >> 8) & 0xFF);
    out[8] = static_cast<uint8_t>(data_size & 0xFF);
    out[9] = static_cast<uint8_t>((data_size >> 8) & 0xFF);
    out[10] = 0;  // no loop
    out[11] = 0;
    return at;
}

bool interpreter::st_play() noexcept {
    basic_value text;
    if (!eval_string(&text)) {
        return false;
    }
    if (!host_.audio_play) {
        return raise_here(error_code::illegal_function_call);
    }
    if (!audio_buffer_) {
        audio_buffer_ = static_cast<uint8_t*>(host_.alloc(host_.user, audio_buffer_size));
        if (!audio_buffer_) {
            return raise_here(error_code::out_of_memory);
        }
    }

    const uint16_t size = mml_to_fmsq(text.str, text.len, audio_buffer_, audio_buffer_size);
    if (size == 0) {
        return raise_here(error_code::illegal_function_call);
    }
    host_.audio_play(host_.user, audio_buffer_, size);
    return true;
}

bool interpreter::st_beep() noexcept {
    if (host_.audio_beep) {
        host_.audio_beep(host_.user);
        return true;
    }
    return raise_here(error_code::illegal_function_call);
}

}  // namespace fmrb_basic
