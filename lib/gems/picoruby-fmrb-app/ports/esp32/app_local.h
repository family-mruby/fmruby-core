#pragma once

void mrb_fmrb_gfx_init(mrb_state *mrb);
void mrb_fmrb_gfx_final(mrb_state *mrb);
// The drawing origin of an FmrbGfx (gfx.c), for the pointer events app.c makes.
bool fmrb_gfx_origin_of(mrb_state *mrb, mrb_value gfx, int *ox, int *oy);
void mrb_fmrb_gfx_block_init(mrb_state *mrb);
void mrb_fmrb_toml_init(mrb_state *mrb);

//void mrb_fmrb_audio_init(mrb_state *mrb);
//void mrb_fmrb_audio_final(mrb_state *mrb);

