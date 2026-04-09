#include <stdarg.h>
#include <stdio.h>
#include <mruby.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <string.h>
#include "fmrb_log.h"
#include "fmrb_app.h"

// Log levels
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE
} log_level_t;

/**
 * Set log level for all tags
 * @param level [Integer] Log level (0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE)
 *
 * Example:
 *   Log.set_level(Log::LEVEL_DEBUG)
 */
static mrb_value
mrb_log_set_level(mrb_state *mrb, mrb_value self)
{
  mrb_int level;
  
  mrb_get_args(mrb, "i", &level);

  int fmrb_level;
  switch (level) {
    case LOG_LEVEL_NONE:    fmrb_level = FMRB_LOG_NONE; break;
    case LOG_LEVEL_ERROR:   fmrb_level = FMRB_LOG_ERROR; break;
    case LOG_LEVEL_WARN:    fmrb_level = FMRB_LOG_WARN; break;
    case LOG_LEVEL_INFO:    fmrb_level = FMRB_LOG_INFO; break;
    case LOG_LEVEL_DEBUG:   fmrb_level = FMRB_LOG_DEBUG; break;
    case LOG_LEVEL_VERBOSE: fmrb_level = FMRB_LOG_VERBOSE; break;
    default:
      mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid log level");
  }

  fmrb_log_level_set("*", fmrb_level);
  
  return mrb_nil_value();
}

/**
 * Set log level for a specific tag
 * @param tag [String] Tag name
 * @param level [Integer] Log level
 *
 * Example:
 *   Log.set_level_for_tag("KERNEL", Log::LEVEL_DEBUG)
 */
static mrb_value
mrb_log_set_level_for_tag(mrb_state *mrb, mrb_value self)
{
  char *tag;
  mrb_int level;

  mrb_get_args(mrb, "zi", &tag, &level);

  int fmrb_level;
  switch (level) {
    case LOG_LEVEL_NONE:    fmrb_level = FMRB_LOG_NONE; break;
    case LOG_LEVEL_ERROR:   fmrb_level = FMRB_LOG_ERROR; break;
    case LOG_LEVEL_WARN:    fmrb_level = FMRB_LOG_WARN; break;
    case LOG_LEVEL_INFO:    fmrb_level = FMRB_LOG_INFO; break;
    case LOG_LEVEL_DEBUG:   fmrb_level = FMRB_LOG_DEBUG; break;
    case LOG_LEVEL_VERBOSE: fmrb_level = FMRB_LOG_VERBOSE; break;
    default:
      mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid log level");
  }

  fmrb_log_level_set(tag, fmrb_level);

  return mrb_nil_value();
}

/**
 * Get current app name from TLS for use as log tag
 */
static const char* get_current_tag(void)
{
  fmrb_app_task_context_t* ctx = fmrb_current();
  if (ctx && ctx->app_name[0] != '\0') {
    return ctx->app_name;
  }
  return "APP";  // Default tag if no context
}

/**
 * Log error message
 * @param message [String] Log message (auto-tag from TLS)
 * @param tag [String] Tag name (optional)
 * @param message [String] Log message
 *
 * Example:
 *   Log.e("Failed to initialize")  # Uses app name from TLS
 *   Log.e("KERNEL", "Failed to initialize")  # Explicit tag
 */
static mrb_value
mrb_log_e(mrb_state *mrb, mrb_value self)
{
  mrb_value arg1, arg2;
  int argc = mrb_get_args(mrb, "o|o", &arg1, &arg2);

  const char *tag;
  const char *msg;

  if (argc == 1) {
    // Single argument: auto-tag from TLS
    tag = get_current_tag();
    msg = (const char *)RSTRING_PTR(arg1);
  } else {
    // Two arguments: explicit tag
    tag = (const char *)RSTRING_PTR(arg1);
    msg = (const char *)RSTRING_PTR(arg2);
  }

  FMRB_LOGE(tag, "%s", msg);

  return mrb_nil_value();
}

/**
 * Log warning message
 * @param message [String] Log message (auto-tag from TLS)
 * @param tag [String] Tag name (optional)
 * @param message [String] Log message
 *
 * Example:
 *   Log.w("Low memory warning")  # Uses app name from TLS
 *   Log.w("KERNEL", "Low memory warning")  # Explicit tag
 */
static mrb_value
mrb_log_w(mrb_state *mrb, mrb_value self)
{
  mrb_value arg1, arg2;
  int argc = mrb_get_args(mrb, "o|o", &arg1, &arg2);

  const char *tag;
  const char *msg;

  if (argc == 1) {
    tag = get_current_tag();
    msg = (const char *)RSTRING_PTR(arg1);
  } else {
    tag = (const char *)RSTRING_PTR(arg1);
    msg = (const char *)RSTRING_PTR(arg2);
  }

  FMRB_LOGW(tag, "%s", msg);

  return mrb_nil_value();
}

/**
 * Log info message
 * @param message [String] Log message (auto-tag from TLS)
 * @param tag [String] Tag name (optional)
 * @param message [String] Log message
 *
 * Example:
 *   Log.i("System initialized")  # Uses app name from TLS
 *   Log.i("KERNEL", "System initialized")  # Explicit tag
 */
static mrb_value
mrb_log_i(mrb_state *mrb, mrb_value self)
{
  mrb_value arg1, arg2;
  int argc = mrb_get_args(mrb, "o|o", &arg1, &arg2);

  const char *tag;
  const char *msg;

  if (argc == 1) {
    tag = get_current_tag();
    msg = (const char *)RSTRING_PTR(arg1);
  } else {
    tag = (const char *)RSTRING_PTR(arg1);
    msg = (const char *)RSTRING_PTR(arg2);
  }

  FMRB_LOGI(tag, "%s", msg);

  return mrb_nil_value();
}

/**
 * Log debug message
 * @param message [String] Log message (auto-tag from TLS)
 * @param tag [String] Tag name (optional)
 * @param message [String] Log message
 *
 * Example:
 *   Log.d("Debug info")  # Uses app name from TLS
 *   Log.d("KERNEL", "Debug info")  # Explicit tag
 */
static mrb_value
mrb_log_d(mrb_state *mrb, mrb_value self)
{
  mrb_value arg1, arg2;
  int argc = mrb_get_args(mrb, "o|o", &arg1, &arg2);

  const char *tag;
  const char *msg;

  if (argc == 1) {
    tag = get_current_tag();
    msg = (const char *)RSTRING_PTR(arg1);
  } else {
    tag = (const char *)RSTRING_PTR(arg1);
    msg = (const char *)RSTRING_PTR(arg2);
  }

  FMRB_LOGD(tag, "%s", msg);

  return mrb_nil_value();
}

// Log.read_lines(max_lines, read_pos) -> [lines_array, new_read_pos]
static mrb_value
mrb_log_read_lines(mrb_state *mrb, mrb_value self)
{
  mrb_int max_lines = 20;
  mrb_int read_pos_val = 0;
  mrb_get_args(mrb, "|ii", &max_lines, &read_pos_val);

  if (max_lines <= 0) max_lines = 1;
  if (max_lines > 100) max_lines = 100;

  // Allocate temporary buffer on stack (limit to reasonable size)
  size_t buf_size = (size_t)max_lines * 256;
  if (buf_size > 8192) buf_size = 8192;
  char *buf = (char *)mrb_malloc(mrb, buf_size);

  uint32_t rp = (uint32_t)read_pos_val;
  int count = fmrb_log_buffer_read_lines(buf, buf_size, (int)max_lines, &rp);

  // Build result array of strings
  mrb_value lines = mrb_ary_new_capa(mrb, count);
  char *p = buf;
  for (int i = 0; i < count; i++) {
    char *nl = strchr(p, '\n');
    if (!nl) break;
    mrb_ary_push(mrb, lines, mrb_str_new(mrb, p, nl - p));
    p = nl + 1;
  }

  mrb_free(mrb, buf);

  // Return [lines_array, new_read_pos]
  mrb_value result = mrb_ary_new_capa(mrb, 2);
  mrb_ary_push(mrb, result, lines);
  mrb_ary_push(mrb, result, mrb_fixnum_value((mrb_int)rp));
  return result;
}

// Log.write_pos -> Integer
static mrb_value
mrb_log_write_pos(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value((mrb_int)fmrb_log_buffer_get_write_pos());
}

// Log.set_buffer_level(level_string) -> nil
// level_string: "E", "W", "I", "D"
static mrb_value
mrb_log_set_buffer_level(mrb_state *mrb, mrb_value self)
{
  char *level_str;
  mrb_get_args(mrb, "z", &level_str);
  if (level_str && level_str[0]) {
    fmrb_log_buffer_set_level(level_str[0]);
  }
  return mrb_nil_value();
}

// Log.buffer_level -> String
static mrb_value
mrb_log_get_buffer_level(mrb_state *mrb, mrb_value self)
{
  char buf[2] = { fmrb_log_buffer_get_level(), '\0' };
  return mrb_str_new_cstr(mrb, buf);
}

void
mrb_fmrb_log_init(mrb_state *mrb)
{
  struct RClass *log_module = mrb_define_module(mrb, "Log");
  
  // Log level constants
  mrb_define_const(mrb, log_module, "LEVEL_NONE", mrb_fixnum_value(LOG_LEVEL_NONE));
  mrb_define_const(mrb, log_module, "LEVEL_ERROR", mrb_fixnum_value(LOG_LEVEL_ERROR));
  mrb_define_const(mrb, log_module, "LEVEL_WARN", mrb_fixnum_value(LOG_LEVEL_WARN));
  mrb_define_const(mrb, log_module, "LEVEL_INFO", mrb_fixnum_value(LOG_LEVEL_INFO));
  mrb_define_const(mrb, log_module, "LEVEL_DEBUG", mrb_fixnum_value(LOG_LEVEL_DEBUG));
  mrb_define_const(mrb, log_module, "LEVEL_VERBOSE", mrb_fixnum_value(LOG_LEVEL_VERBOSE));
  
  // Log level management methods
  mrb_define_module_function(mrb, log_module, "set_level", mrb_log_set_level, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, log_module, "set_level_for_tag", mrb_log_set_level_for_tag, MRB_ARGS_REQ(2));
  
  // Logging methods (support both 1-arg auto-tag and 2-arg explicit tag)
  mrb_define_module_function(mrb, log_module, "error", mrb_log_e, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function(mrb, log_module, "warn", mrb_log_w, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function(mrb, log_module, "info", mrb_log_i, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function(mrb, log_module, "debug", mrb_log_d, MRB_ARGS_ARG(1, 1));

  // Log buffer methods
  mrb_define_module_function(mrb, log_module, "read_lines", mrb_log_read_lines, MRB_ARGS_OPT(2));
  mrb_define_module_function(mrb, log_module, "write_pos", mrb_log_write_pos, MRB_ARGS_NONE());
  mrb_define_module_function(mrb, log_module, "set_buffer_level", mrb_log_set_buffer_level, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, log_module, "buffer_level", mrb_log_get_buffer_level, MRB_ARGS_NONE());

  // Short aliases
  mrb_define_module_function(mrb, log_module, "e", mrb_log_e, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function(mrb, log_module, "w", mrb_log_w, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function(mrb, log_module, "i", mrb_log_i, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function(mrb, log_module, "d", mrb_log_d, MRB_ARGS_ARG(1, 1));
}

void
mrb_fmrb_log_final(mrb_state *mrb)
{
  // Cleanup if needed
}
