#pragma once

// Version of the link protocol between fmruby-core and fmruby-graphics-audio
// (fmrb_link_protocol.h and the frames under it). Both sides compare it in
// the version handshake and refuse to pair on a mismatch, so bump it when a
// command or a struct changes in a way an older peer would misread.
//
//   6  mute and volume at the last output stage (SET_MAIN_VOLUME and the
//      per-APU mute/volume)
//   5  SET_SPRITE_CLIP (0x58)
#define FMRB_LINK_VERSION 6
