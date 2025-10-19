#include "picoruby.h"
#include "../../main/lib/fmrb_gfx/fmrb_gfx.h"

// Forward declaration
extern fmrb_gfx_context_t g_gfx_context;

static mrb_value
fmrb_gfx_clear_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int color;
  mrb_get_args(mrb, "i", &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_clear(g_gfx_context, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_fill_screen_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int color;
  mrb_get_args(mrb, "i", &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_fill_screen(g_gfx_context, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_present_wrapper(mrb_state *mrb, mrb_value self)
{
  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_present(g_gfx_context);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_draw_pixel_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, color;
  mrb_get_args(mrb, "iii", &x, &y, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_draw_pixel(g_gfx_context, x, y, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_draw_string_wrapper(mrb_state *mrb, mrb_value self)
{
  char *str;
  mrb_int x, y, color;
  mrb_get_args(mrb, "siii", &str, &x, &y, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_draw_string(g_gfx_context, str, x, y, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_draw_line_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x0, y0, x1, y1, color;
  mrb_get_args(mrb, "iiiii", &x0, &y0, &x1, &y1, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_draw_line(g_gfx_context, x0, y0, x1, y1, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_draw_rect_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, w, h, color;
  mrb_get_args(mrb, "iiiii", &x, &y, &w, &h, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_rect_t rect = { x, y, w, h };
  fmrb_gfx_draw_rect(g_gfx_context, &rect, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_fill_rect_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, w, h, color;
  mrb_get_args(mrb, "iiiii", &x, &y, &w, &h, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_rect_t rect = { x, y, w, h };
  fmrb_gfx_fill_rect(g_gfx_context, &rect, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_draw_circle_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, r, color;
  mrb_get_args(mrb, "iiii", &x, &y, &r, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_draw_circle(g_gfx_context, x, y, r, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_fill_circle_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, r, color;
  mrb_get_args(mrb, "iiii", &x, &y, &r, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_fill_circle(g_gfx_context, x, y, r, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_draw_triangle_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x0, y0, x1, y1, x2, y2, color;
  mrb_get_args(mrb, "iiiiiii", &x0, &y0, &x1, &y1, &x2, &y2, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_draw_triangle(g_gfx_context, x0, y0, x1, y1, x2, y2, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_fill_triangle_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x0, y0, x1, y1, x2, y2, color;
  mrb_get_args(mrb, "iiiiiii", &x0, &y0, &x1, &y1, &x2, &y2, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_fill_triangle(g_gfx_context, x0, y0, x1, y1, x2, y2, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_draw_round_rect_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, w, h, r, color;
  mrb_get_args(mrb, "iiiiii", &x, &y, &w, &h, &r, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_draw_round_rect(g_gfx_context, x, y, w, h, r, (fmrb_color_t)color);
  return mrb_nil_value();
}

static mrb_value
fmrb_gfx_fill_round_rect_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, w, h, r, color;
  mrb_get_args(mrb, "iiiiii", &x, &y, &w, &h, &r, &color);

  if (!g_gfx_context) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
  }

  fmrb_gfx_fill_round_rect(g_gfx_context, x, y, w, h, r, (fmrb_color_t)color);
  return mrb_nil_value();
}

void
mrb_picoruby_fmrb_gfx_init(mrb_state *mrb)
{
  struct RClass *fmrb_gfx_class = mrb_define_class(mrb, "FmrbGfx", mrb->object_class);

  // Drawing methods
  mrb_define_class_method(mrb, fmrb_gfx_class, "clear", fmrb_gfx_clear_wrapper, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, fmrb_gfx_class, "fill_screen", fmrb_gfx_fill_screen_wrapper, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, fmrb_gfx_class, "present", fmrb_gfx_present_wrapper, MRB_ARGS_NONE());

  mrb_define_class_method(mrb, fmrb_gfx_class, "draw_pixel", fmrb_gfx_draw_pixel_wrapper, MRB_ARGS_REQ(3));
  mrb_define_class_method(mrb, fmrb_gfx_class, "draw_string", fmrb_gfx_draw_string_wrapper, MRB_ARGS_REQ(4));
  mrb_define_class_method(mrb, fmrb_gfx_class, "draw_line", fmrb_gfx_draw_line_wrapper, MRB_ARGS_REQ(5));

  mrb_define_class_method(mrb, fmrb_gfx_class, "draw_rect", fmrb_gfx_draw_rect_wrapper, MRB_ARGS_REQ(5));
  mrb_define_class_method(mrb, fmrb_gfx_class, "fill_rect", fmrb_gfx_fill_rect_wrapper, MRB_ARGS_REQ(5));

  mrb_define_class_method(mrb, fmrb_gfx_class, "draw_circle", fmrb_gfx_draw_circle_wrapper, MRB_ARGS_REQ(4));
  mrb_define_class_method(mrb, fmrb_gfx_class, "fill_circle", fmrb_gfx_fill_circle_wrapper, MRB_ARGS_REQ(4));

  mrb_define_class_method(mrb, fmrb_gfx_class, "draw_triangle", fmrb_gfx_draw_triangle_wrapper, MRB_ARGS_REQ(7));
  mrb_define_class_method(mrb, fmrb_gfx_class, "fill_triangle", fmrb_gfx_fill_triangle_wrapper, MRB_ARGS_REQ(7));

  mrb_define_class_method(mrb, fmrb_gfx_class, "draw_round_rect", fmrb_gfx_draw_round_rect_wrapper, MRB_ARGS_REQ(6));
  mrb_define_class_method(mrb, fmrb_gfx_class, "fill_round_rect", fmrb_gfx_fill_round_rect_wrapper, MRB_ARGS_REQ(6));

  // Color constants
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_BLACK", mrb_fixnum_value(FMRB_COLOR_BLACK));
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_WHITE", mrb_fixnum_value(FMRB_COLOR_WHITE));
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_RED", mrb_fixnum_value(FMRB_COLOR_RED));
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_GREEN", mrb_fixnum_value(FMRB_COLOR_GREEN));
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_BLUE", mrb_fixnum_value(FMRB_COLOR_BLUE));
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_YELLOW", mrb_fixnum_value(FMRB_COLOR_YELLOW));
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_CYAN", mrb_fixnum_value(FMRB_COLOR_CYAN));
  mrb_define_const(mrb, fmrb_gfx_class, "COLOR_MAGENTA", mrb_fixnum_value(FMRB_COLOR_MAGENTA));
}
