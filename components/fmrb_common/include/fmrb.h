#pragma once

#define FMRB_OS_VERSION "2.0.1"
// 2.1.0 / link 5: SET_SPRITE_CLIP (0x58) added, and note_on stopped dropping
// noise at period 0. Both checks are strict, so a GA firmware without them
// refuses to pair rather than silently losing sprite clipping and the SE.
#define FMRB_GA_VERSION "2.1.0"
#define FMRB_LINK_VERSION 5
