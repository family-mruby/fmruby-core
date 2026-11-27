#pragma once

#define FMRB_OS_VERSION "3.1.0"
// 3.1.0 / link 7: DRAW_TILE_SCALED and SPRITE_IMAGE_DATA, and a PNG read into
// a sprite image keeps its transparent pixels (0x01). The GA was numbered
// 3.2.0 for them while 3.1.0 was being made, and went back to 3.1.0 for the
// release (3.1.0 had not been released): link 7 alone tells a GA built
// before them (link 6), which would drop the two commands without a word.
// 3.0.0 / link 6: the mute and volume commands at the GA's last output stage
// (SET_MAIN_VOLUME and the mute/volume of the APU instances). A 2.1.0 GA would
// pass a link-5 check and drop them without a word, so both numbers move.
// 2.1.0 / link 5: SET_SPRITE_CLIP (0x58) added, note_on stopped dropping
// noise at period 0, EXPORT_FRAME added, and SET_FONT gained the bold
// family and the 16px cut. Both checks are strict, so a GA firmware without
// them refuses to pair rather than silently losing any of those.
#define FMRB_GA_VERSION "3.1.0"
// FMRB_LINK_VERSION lives with the wire format (components/fmrb_wire).
#include "fmrb_link_version.h"
