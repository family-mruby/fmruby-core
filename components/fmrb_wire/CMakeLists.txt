# The wire format between fmruby-core and fmruby-graphics-audio: the link
# protocol (commands, structs, frames, COBS), the HID event packets, and the
# shared memory the Linux simulation's display process reads.
#
# This directory is the one copy. fmruby-graphics-audio and its sdl2-display
# image read it from the fmruby-core checkout next to them, so a header here
# must stand on its own: libc headers only, nothing from the rest of this
# firmware (fmrb.h, fmrb_err.h, ...). `rake wire:check` compiles each one
# alone to keep it that way.
idf_component_register(
    INCLUDE_DIRS "include"
)
