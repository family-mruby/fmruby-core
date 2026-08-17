# Standard MicroPython modules that live in extmod/ and are worth having.
#
# The embed port copies only py/ into the generated package, so an extmod
# module has to be pulled in on purpose: listed here it goes through the qstr
# extractor and its MP_REGISTER_MODULE lands in genhdr/, and the copy step in
# port/Makefile puts the source into mp_embed/extmod so the firmware build
# compiles it like any other generated file.
#
# Only self-contained modules belong here. Anything that wants a HAL (asyncio's
# tick source, for instance) needs port work first.

MICROPYTHON_TOP ?= $(abspath $(USERMOD_DIR)/../micropython)

# random: games and demos want it, and it depends on nothing but py/runtime.h.
SRC_USERMOD += $(MICROPYTHON_TOP)/extmod/modrandom.c
