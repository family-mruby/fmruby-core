#include "fmrb_keymap.h"
#include <string.h>

// Current keyboard layout (default: JP)
static fmrb_keymap_layout_t g_current_layout = FMRB_KEYMAP_LAYOUT_JP;

// USB HID scancode to character mapping
// Index: scancode, [0]=no shift, [1]=with shift
typedef struct {
    char normal;
    char shifted;
} keymap_entry_t;

// US keyboard layout mapping
static const keymap_entry_t us_keymap[] = {
    [4]   = {'a', 'A'},    // 0x04: a/A
    [5]   = {'b', 'B'},    // 0x05: b/B
    [6]   = {'c', 'C'},    // 0x06: c/C
    [7]   = {'d', 'D'},    // 0x07: d/D
    [8]   = {'e', 'E'},    // 0x08: e/E
    [9]   = {'f', 'F'},    // 0x09: f/F
    [10]  = {'g', 'G'},    // 0x0A: g/G
    [11]  = {'h', 'H'},    // 0x0B: h/H
    [12]  = {'i', 'I'},    // 0x0C: i/I
    [13]  = {'j', 'J'},    // 0x0D: j/J
    [14]  = {'k', 'K'},    // 0x0E: k/K
    [15]  = {'l', 'L'},    // 0x0F: l/L
    [16]  = {'m', 'M'},    // 0x10: m/M
    [17]  = {'n', 'N'},    // 0x11: n/N
    [18]  = {'o', 'O'},    // 0x12: o/O
    [19]  = {'p', 'P'},    // 0x13: p/P
    [20]  = {'q', 'Q'},    // 0x14: q/Q
    [21]  = {'r', 'R'},    // 0x15: r/R
    [22]  = {'s', 'S'},    // 0x16: s/S
    [23]  = {'t', 'T'},    // 0x17: t/T
    [24]  = {'u', 'U'},    // 0x18: u/U
    [25]  = {'v', 'V'},    // 0x19: v/V
    [26]  = {'w', 'W'},    // 0x1A: w/W
    [27]  = {'x', 'X'},    // 0x1B: x/X
    [28]  = {'y', 'Y'},    // 0x1C: y/Y
    [29]  = {'z', 'Z'},    // 0x1D: z/Z
    [30]  = {'1', '!'},    // 0x1E: 1/!
    [31]  = {'2', '@'},    // 0x1F: 2/@
    [32]  = {'3', '#'},    // 0x20: 3/#
    [33]  = {'4', '$'},    // 0x21: 4/$
    [34]  = {'5', '%'},    // 0x22: 5/%
    [35]  = {'6', '^'},    // 0x23: 6/^
    [36]  = {'7', '&'},    // 0x24: 7/&
    [37]  = {'8', '*'},    // 0x25: 8/*
    [38]  = {'9', '('},    // 0x26: 9/(
    [39]  = {'0', ')'},    // 0x27: 0/)
    [40]  = {'\n', '\n'},  // 0x28: Enter
    [42]  = {'\b', '\b'},  // 0x2A: Backspace
    [43]  = {'\t', '\t'},  // 0x2B: Tab
    [44]  = {' ', ' '},    // 0x2C: Space
    [45]  = {'-', '_'},    // 0x2D: -/_
    [46]  = {'=', '+'},    // 0x2E: =/+
    [47]  = {'[', '{'},    // 0x2F: [/{
    [48]  = {']', '}'},    // 0x30: ]/}
    [49]  = {'\\', '|'},   // 0x31: \/|
    [51]  = {';', ':'},    // 0x33: ;/:
    [52]  = {'\'', '"'},   // 0x34: '/"
    [53]  = {'`', '~'},    // 0x35: `/~
    [54]  = {',', '<'},    // 0x36: ,/<
    [55]  = {'.', '>'},    // 0x37: ./>
    [56]  = {'/', '?'},    // 0x38: //?
};

// Japanese (JIS) keyboard layout mapping
static const keymap_entry_t jp_keymap[] = {
    [4]   = {'a', 'A'},    // 0x04: a/A
    [5]   = {'b', 'B'},    // 0x05: b/B
    [6]   = {'c', 'C'},    // 0x06: c/C
    [7]   = {'d', 'D'},    // 0x07: d/D
    [8]   = {'e', 'E'},    // 0x08: e/E
    [9]   = {'f', 'F'},    // 0x09: f/F
    [10]  = {'g', 'G'},    // 0x0A: g/G
    [11]  = {'h', 'H'},    // 0x0B: h/H
    [12]  = {'i', 'I'},    // 0x0C: i/I
    [13]  = {'j', 'J'},    // 0x0D: j/J
    [14]  = {'k', 'K'},    // 0x0E: k/K
    [15]  = {'l', 'L'},    // 0x0F: l/L
    [16]  = {'m', 'M'},    // 0x10: m/M
    [17]  = {'n', 'N'},    // 0x11: n/N
    [18]  = {'o', 'O'},    // 0x12: o/O
    [19]  = {'p', 'P'},    // 0x13: p/P
    [20]  = {'q', 'Q'},    // 0x14: q/Q
    [21]  = {'r', 'R'},    // 0x15: r/R
    [22]  = {'s', 'S'},    // 0x16: s/S
    [23]  = {'t', 'T'},    // 0x17: t/T
    [24]  = {'u', 'U'},    // 0x18: u/U
    [25]  = {'v', 'V'},    // 0x19: v/V
    [26]  = {'w', 'W'},    // 0x1A: w/W
    [27]  = {'x', 'X'},    // 0x1B: x/X
    [28]  = {'y', 'Y'},    // 0x1C: y/Y
    [29]  = {'z', 'Z'},    // 0x1D: z/Z
    [30]  = {'1', '!'},    // 0x1E: 1/!
    [31]  = {'2', '"'},    // 0x1F: 2/" (JP: Shift+2 = ")
    [32]  = {'3', '#'},    // 0x20: 3/#
    [33]  = {'4', '$'},    // 0x21: 4/$
    [34]  = {'5', '%'},    // 0x22: 5/%
    [35]  = {'6', '&'},    // 0x23: 6/& (JP: Shift+6 = &)
    [36]  = {'7', '\''},   // 0x24: 7/' (JP: Shift+7 = ')
    [37]  = {'8', '('},    // 0x25: 8/(
    [38]  = {'9', ')'},    // 0x26: 9/)
    [39]  = {'0', 0},      // 0x27: 0 (JP: Shift+0 = no char)
    [40]  = {'\n', '\n'},  // 0x28: Enter
    [42]  = {'\b', '\b'},  // 0x2A: Backspace
    [43]  = {'\t', '\t'},  // 0x2B: Tab
    [44]  = {' ', ' '},    // 0x2C: Space
    [45]  = {'-', '='},    // 0x2D: -/= (JP: Shift+- = =)
    [46]  = {'^', '~'},    // 0x2E: ^/~ (JP)
    [47]  = {'@', '`'},    // 0x2F: @/` (JP)
    [48]  = {'[', '{'},    // 0x30: [/{
    [51]  = {';', '+'},    // 0x33: ;/+ (JP: Shift+; = +)
    [52]  = {':', '*'},    // 0x34: :/* (JP)
    [53]  = {0, 0},        // 0x35: Half-width/Full-width (JP IME toggle - not printable)
    [54]  = {',', '<'},    // 0x36: ,/<
    [55]  = {'.', '>'},    // 0x37: ./>
    [56]  = {'/', '?'},    // 0x38: //?
    [135] = {'\\', '_'},   // 0x87: \/_ (JP: backslash key)
    [136] = {']', '}'},    // 0x88: ]/} (JP: right bracket key)
};

char fmrb_keymap_scancode_to_char(uint8_t scancode, uint8_t modifier, fmrb_keymap_layout_t layout)
{
    // Check if Shift is pressed
    int shift_pressed = (modifier & (FMRB_KEYMAP_MOD_LSHIFT | FMRB_KEYMAP_MOD_RSHIFT)) != 0;

    // Select keymap based on layout
    const keymap_entry_t *keymap;
    size_t keymap_size;

    if (layout == FMRB_KEYMAP_LAYOUT_JP) {
        keymap = jp_keymap;
        keymap_size = sizeof(jp_keymap) / sizeof(jp_keymap[0]);
    } else {
        keymap = us_keymap;
        keymap_size = sizeof(us_keymap) / sizeof(us_keymap[0]);
    }

    // Check if scancode is in range
    if (scancode >= keymap_size) {
        return 0;
    }

    // Get entry
    const keymap_entry_t *entry = &keymap[scancode];

    // Return character based on shift state
    return shift_pressed ? entry->shifted : entry->normal;
}

void fmrb_keymap_set_layout(fmrb_keymap_layout_t layout)
{
    g_current_layout = layout;
}

fmrb_keymap_layout_t fmrb_keymap_get_layout(void)
{
    return g_current_layout;
}
