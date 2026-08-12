/*
 * Where picoruby-ti's 16KB working arena lives on the ESP32 targets.
 *
 * The engine asks the host for this (TI_ARENA_INCLUDE / TI_ARENA_ATTR); the
 * default is an ordinary static array, which on a chip with external RAM
 * would spend internal RAM that nothing else can replace. The S3 in
 * particular has none to spare (doc/internal_ram_budget.md), and the arena is
 * CPU-only scratch -- no DMA touches it -- so external RAM costs it nothing
 * but a slower access it never notices between keystrokes.
 *
 * The section name is spelled out rather than taken from the IDF's
 * EXT_RAM_BSS_ATTR: this file is compiled by the rake-side mruby build, which
 * has neither the IDF include tree nor the generated sdkconfig.h that
 * esp_attr.h needs. The IDF's linker fragment matches ".ext_ram.bss+", so a
 * plain section of that name lands in external RAM exactly like the attribute
 * would, provided CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is set -- which
 * every Family mruby target sets (config/sdkconfig.defaults.{n8r8,n16r8,p4}).
 *
 * Copied into the engine's source tree by `rake setup`; reached from there
 * because the gem puts its own src/ on the include path.
 */
#ifndef FMRB_TI_ARENA_ESP32_H
#define FMRB_TI_ARENA_ESP32_H

#define TI_ARENA_ATTR __attribute__((section(".ext_ram.bss")))

#endif
