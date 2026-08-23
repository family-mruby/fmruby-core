#pragma once

#define FMRB_OS_VERSION "2.1.0"
// 2.1.0 / link 5: SET_SPRITE_CLIP (0x58) added, note_on stopped dropping
// noise at period 0, EXPORT_FRAME added, and SET_FONT gained the bold
// family and the 16px cut. Both checks are strict, so a GA firmware without
// them refuses to pair rather than silently losing any of those.
#define FMRB_GA_VERSION "2.1.0"
#define FMRB_LINK_VERSION 5
