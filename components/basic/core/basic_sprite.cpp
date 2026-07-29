// Sprite plane: DEF SPRITE definitions, the DEF MOVE animation engine and the
// functions that read them back (XPOS / YPOS / MOVE / VCT / CRASH).
//
// Positions live here rather than in the renderer, so the whole movement model
// is host independent and can be pinned down by the golden tests: a program
// that POSITIONs, MOVEs and PAUSEs produces the same coordinates on the test
// runner as on the device.

#include "basic_core.hpp"
#include "basic_internal.hpp"

namespace fmrb_basic {
namespace {

/// Per direction step (core_spec sec 9: 1 = up, then clockwise to 8 = up left).
struct move_vector {
    int8_t dx;
    int8_t dy;
};

constexpr move_vector direction_vectors[9] = {
    {0, 0},    // 0: stopped
    {0, -1},   // 1: up
    {1, -1},   // 2: up right
    {1, 0},    // 3: right
    {1, 1},    // 4: down right
    {0, 1},    // 5: down
    {-1, 1},   // 6: down left
    {-1, 0},   // 7: left
    {-1, -1},  // 8: up left
};

/**
 * Built in animation characters (core_spec sec 9 kinds 0-15) mapped to their
 * tile codes in character table A. The two frame codes are the walk cycle from
 * the v3 spec's P / P2 table; every one of them is a 16x16 character, so each
 * frame occupies four consecutive codes.
 */
struct anim_character {
    uint8_t frame[2];
};

constexpr anim_character anim_characters[16] = {
    {{0, 4}},      // 0  Mario
    {{28, 32}},    // 1  Lady
    {{56, 60}},    // 2  Fighter fly
    {{64, 68}},    // 3  Achilles
    {{96, 100}},   // 4  Penpen
    {{88, 92}},    // 5  Nitanita
    {{112, 116}},  // 6  Fireball
    {{120, 124}},  // 7  Car
    {{160, 160}},  // 8  Star killer (single frame)
    {{164, 168}},  // 9  Star ship
    {{176, 180}},  // 10 Explosion
    {{184, 188}},  // 11 Shell creeper
    {{192, 196}},  // 12 Side stepper
    {{200, 204}},  // 13 Nit picker
    {{208, 210}},  // 14 Laser
    {{144, 148}},  // 15 Spinner
};

/// Frames between animation frame changes while a character is moving.
constexpr uint8_t anim_frame_period = 8;

/// A moving character is 16x16 dots (core_spec sec 9).
constexpr int16_t move_sprite_size = 16;

}  // namespace

void interpreter::set_pad(uint8_t player, uint8_t stick, uint8_t trigger) noexcept {
    if (player > 1) {
        return;
    }
    pad_stick_[player] = stick;
    pad_trigger_[player] = trigger;
}

void interpreter::notify_sprite(uint8_t index) noexcept {
    if (host_.sprite_update && index < sprite_count) {
        sprites_[index].table_a = sprites_use_table_a();
        // DEF SPRITE artwork does not animate on its own; MOVE characters do.
        sprites_[index].frame_count = 1;
        sprites_[index].frame_index = 0;
        host_.sprite_update(host_.user, &sprites_[index]);
    }
}

void interpreter::refresh_sprites() noexcept {
    for (uint8_t i = 0; i < sprite_count; ++i) {
        notify_sprite(i);
    }
    for (uint8_t i = 0; i < move_count; ++i) {
        notify_move(i);
    }
}

/**
 * Palette banks selected by CGSET.
 *
 * core_spec sec 7 says there are two BG banks and three sprite banks of four
 * colour groups each, but does not list their colour codes; these are readable
 * placeholders (see the B3 report), with bank 1 matching the start up palette.
 */
void interpreter::load_palette_bank() noexcept {
    static constexpr uint8_t bg_banks[2][4][3] = {
        {{1, 17, 33}, {5, 21, 37}, {9, 25, 41}, {13, 29, 45}},
        {{2, 22, 48}, {6, 22, 54}, {10, 26, 58}, {1, 17, 49}},
    };
    static constexpr uint8_t sprite_banks[3][4][3] = {
        {{2, 22, 54}, {6, 26, 58}, {10, 30, 48}, {1, 21, 49}},
        {{2, 22, 48}, {6, 22, 54}, {10, 26, 58}, {1, 17, 49}},
        {{5, 23, 53}, {9, 27, 57}, {1, 19, 51}, {13, 32, 48}},
    };
    for (uint8_t attr = 0; attr < 4; ++attr) {
        for (uint8_t i = 0; i < 3; ++i) {
            palette_[attr][i] = bg_banks[cgset_bg_ & 1][attr][i];
            sprite_palette_[attr][i] = sprite_banks[cgset_sprite_ % 3][attr][i];
        }
    }
}

void interpreter::notify_move(uint8_t index) noexcept {
    if (!host_.sprite_update || index >= move_count) {
        return;
    }
    const basic_move_state& mv = moves_[index];

    // Moving characters occupy the renderer's second bank of slots.
    basic_sprite_state view = {};
    view.index = static_cast<uint8_t>(sprite_count + index);
    view.defined = mv.defined;
    view.visible = mv.visible && sprite_plane_on_;
    view.size16 = true;
    view.behind = mv.behind;
    view.table_a = true;
    view.attr = mv.attr;
    // Both walk poses travel together so the renderer can hold an image for
    // each and switch between them; anim_phase only selects which one shows.
    view.frame_count = 2;
    view.frame_index = static_cast<uint8_t>(mv.anim_phase & 1);
    for (uint8_t f = 0; f < 2; ++f) {
        const uint8_t base = anim_characters[mv.character & 15].frame[f];
        for (uint8_t i = 0; i < 4; ++i) {
            view.frame_tiles[f][i] = static_cast<uint8_t>(base + i);
        }
    }
    view.x = mv.x;
    view.y = mv.y;
    host_.sprite_update(host_.user, &view);
}

bool interpreter::st_def() noexcept {
    if (accept(token::sprite)) {
        // DEF SPRITE n,(A,B,C,D,E) = <character expression>
        int16_t slot = 0;
        if (!eval_number(&slot) || !expect(token::comma) || !expect(token::lparen)) {
            return false;
        }
        int16_t param[5] = {0, 0, 0, 0, 0};
        for (uint8_t i = 0; i < 5; ++i) {
            if (i > 0 && !expect(token::comma)) {
                return false;
            }
            if (!eval_number(&param[i])) {
                return false;
            }
        }
        if (!expect(token::rparen) || !expect(token::equal)) {
            return false;
        }
        basic_value tiles;
        if (!eval_string(&tiles)) {
            return false;
        }
        if (slot < 0 || slot >= sprite_count || param[0] < 0 || param[0] > 3 ||
            param[1] < 0 || param[1] > 1) {
            return raise_here(error_code::illegal_function_call);
        }

        basic_sprite_state& sp = sprites_[slot];
        sp.index = static_cast<uint8_t>(slot);
        sp.defined = true;
        sp.attr = static_cast<uint8_t>(param[0]);
        sp.size16 = (param[1] != 0);
        sp.behind = (param[2] != 0);
        sp.flip_x = (param[3] != 0);
        sp.flip_y = (param[4] != 0);
        sp.table_a = true;
        for (uint8_t i = 0; i < 4; ++i) {
            sp.frame_tiles[0][i] = (i < tiles.len) ? tiles.str[i] : 0;
        }
        notify_sprite(static_cast<uint8_t>(slot));
        return true;
    }

    if (accept(token::move)) {
        // DEF MOVE(n) = SPRITE(A,B,C,D,E,F)
        int16_t slot = 0;
        if (!expect(token::lparen) || !eval_number(&slot) || !expect(token::rparen) ||
            !expect(token::equal) || !expect(token::sprite) || !expect(token::lparen)) {
            return false;
        }
        int16_t param[6] = {0, 0, 0, 0, 0, 0};
        for (uint8_t i = 0; i < 6; ++i) {
            if (i > 0 && !expect(token::comma)) {
                return false;
            }
            if (!eval_number(&param[i])) {
                return false;
            }
        }
        if (!expect(token::rparen)) {
            return false;
        }
        if (slot < 0 || slot >= move_count || param[0] < 0 || param[0] > 15 ||
            param[1] < 0 || param[1] > 8 || param[5] < 0 || param[5] > 3) {
            return raise_here(error_code::illegal_function_call);
        }

        basic_move_state& mv = moves_[slot];
        const bool was_defined = mv.defined;
        mv.defined = true;
        mv.character = static_cast<uint8_t>(param[0]);
        mv.direction = static_cast<uint8_t>(param[1]);
        // Speed 0 means "one step every 256 frames" (core_spec sec 9).
        mv.speed = static_cast<uint8_t>(param[2] & 0xFF);
        mv.distance = static_cast<uint8_t>(param[3] & 0xFF);
        mv.behind = (param[4] != 0);
        mv.attr = static_cast<uint8_t>(param[5]);
        mv.remaining = static_cast<uint16_t>(mv.distance) * 2u;
        mv.step_counter = 0;
        mv.anim_phase = 0;
        mv.active = false;
        if (!was_defined) {
            // Undefined slots start at the centre (core_spec sec 9, POSITION).
            mv.x = 120;
            mv.y = 120;
            mv.visible = false;
        }
        notify_move(static_cast<uint8_t>(slot));
        return true;
    }

    return raise_here(error_code::syntax);
}

bool interpreter::st_sprite() noexcept {
    if (accept(token::on)) {
        sprite_plane_on_ = true;
        if (host_.sprite_plane) {
            host_.sprite_plane(host_.user, true);
        }
        return true;
    }
    if (accept(token::off)) {
        sprite_plane_on_ = false;
        if (host_.sprite_plane) {
            host_.sprite_plane(host_.user, false);
        }
        return true;
    }

    int16_t slot = 0;
    if (!eval_number(&slot)) {
        return false;
    }
    if (slot < 0 || slot >= sprite_count) {
        return raise_here(error_code::illegal_function_call);
    }
    basic_sprite_state& sp = sprites_[slot];

    if (!accept(token::comma)) {
        sp.visible = false;  // SPRITE n alone erases that sprite
        notify_sprite(static_cast<uint8_t>(slot));
        return true;
    }
    int16_t x = 0;
    int16_t y = 0;
    if (!eval_number(&x) || !expect(token::comma) || !eval_number(&y)) {
        return false;
    }
    sp.x = x;
    sp.y = y;
    sp.visible = true;
    notify_sprite(static_cast<uint8_t>(slot));
    return true;
}

bool interpreter::parse_slot_list(uint8_t* slots, uint8_t* count) noexcept {
    *count = 0;
    while (true) {
        int16_t slot = 0;
        if (!eval_number(&slot)) {
            return false;
        }
        if (slot < 0 || slot >= move_count) {
            return raise_here(error_code::illegal_function_call);
        }
        if (*count < move_count) {
            slots[(*count)++] = static_cast<uint8_t>(slot);
        }
        if (!accept(token::comma)) {
            return true;
        }
    }
}

bool interpreter::st_move_group(token tk) noexcept {
    uint8_t slots[move_count];
    uint8_t count = 0;
    if (!parse_slot_list(slots, &count)) {
        return false;
    }

    for (uint8_t i = 0; i < count; ++i) {
        basic_move_state& mv = moves_[slots[i]];
        switch (tk) {
            case token::move:
                if (!mv.defined) {
                    return raise_here(error_code::illegal_function_call);
                }
                // Restarting a finished character gives it its travel back.
                if (mv.remaining == 0) {
                    mv.remaining = static_cast<uint16_t>(mv.distance) * 2u;
                }
                mv.active = (mv.remaining > 0 && mv.direction != 0);
                mv.visible = true;
                break;
            case token::cut:
                mv.active = false;  // stops, stays on screen
                break;
            case token::era:
                mv.active = false;
                mv.visible = false;
                break;
            case token::can:
                // CAN clears the definition and the position in one go (v3).
                mv.active = false;
                mv.visible = false;
                mv.defined = false;
                mv.x = 120;
                mv.y = 120;
                break;
            default:
                break;
        }
        notify_move(slots[i]);
    }
    return true;
}

bool interpreter::st_position() noexcept {
    int16_t slot = 0;
    int16_t x = 0;
    int16_t y = 0;
    if (!eval_number(&slot) || !expect(token::comma) || !eval_number(&x) ||
        !expect(token::comma) || !eval_number(&y)) {
        return false;
    }
    if (slot < 0 || slot >= move_count) {
        return raise_here(error_code::illegal_function_call);
    }
    basic_move_state& mv = moves_[slot];
    mv.x = x;
    mv.y = y;
    // A fresh position restarts the travel budget for the next MOVE.
    mv.remaining = static_cast<uint16_t>(mv.distance) * 2u;
    mv.step_counter = 0;
    notify_move(static_cast<uint8_t>(slot));
    return true;
}

int16_t interpreter::move_crash(uint8_t index) const noexcept {
    if (index >= move_count || !moves_[index].defined) {
        return -2;  // undefined (v3_spec)
    }
    const basic_move_state& a = moves_[index];
    if (!a.visible) {
        return -1;
    }
    for (uint8_t i = 0; i < move_count; ++i) {
        if (i == index) {
            continue;
        }
        const basic_move_state& b = moves_[i];
        if (!b.defined || !b.visible) {
            continue;
        }
        // Overlap of the two 16x16 rectangles.
        if (a.x < b.x + move_sprite_size && b.x < a.x + move_sprite_size &&
            a.y < b.y + move_sprite_size && b.y < a.y + move_sprite_size) {
            return static_cast<int16_t>(i);  // lowest number wins
        }
    }
    return -1;
}

void interpreter::advance_moves() noexcept {
    for (uint8_t i = 0; i < move_count; ++i) {
        basic_move_state& mv = moves_[i];
        if (!mv.defined || !mv.active) {
            continue;
        }

        // Speed C: two dots every 2C frames, so C = 1 is 60 dots per second
        // and C = 0 means one step every 256 frames (core_spec sec 9).
        const uint16_t period =
            static_cast<uint16_t>((mv.speed == 0) ? 512 : (mv.speed * 2u));
        if (++mv.step_counter < period) {
            continue;
        }
        mv.step_counter = 0;

        const move_vector& dir = direction_vectors[mv.direction & 15];
        mv.x = static_cast<int16_t>(mv.x + dir.dx * 2);
        mv.y = static_cast<int16_t>(mv.y + dir.dy * 2);
        if (mv.remaining >= 2) {
            mv.remaining = static_cast<uint16_t>(mv.remaining - 2);
        } else {
            mv.remaining = 0;
        }
        if (mv.remaining == 0) {
            mv.active = false;  // travel complete: MOVE(n) now reads 0
        }

        if (++mv.anim_counter >= anim_frame_period) {
            mv.anim_counter = 0;
            mv.anim_phase = static_cast<uint8_t>(mv.anim_phase ^ 1);
        }
        notify_move(i);
    }
}

}  // namespace fmrb_basic
