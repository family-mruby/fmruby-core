#pragma once

#include <mruby.h>

/*
 * EditorCore: the editor's document model in C.
 *
 * The real implementation lives in ports/esp32/editor_core.c, compiled by the
 * picoruby-esp32 CMake component because it needs the fmrb_mem / fmrb_hal_file
 * headers from the firmware side. src/ only carries the gem hooks.
 */
void mrb_picoruby_fmrb_editor_core_init_impl(mrb_state *mrb);
void mrb_picoruby_fmrb_editor_core_final_impl(mrb_state *mrb);
