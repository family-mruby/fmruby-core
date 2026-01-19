#include <stdarg.h>
#include <stdio.h>
#include <mruby.h>
#include <mruby/string.h>
#include "esp_log.h"
#include "fmrb_app.h"

// Log levels matching ESP-IDF
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
  
  esp_log_level_t esp_level;
  switch (level) {
    case LOG_LEVEL_NONE:    esp_level = ESP_LOG_NONE; break;
    case LOG_LEVEL_ERROR:   esp_level = ESP_LOG_ERROR; break;
    case LOG_LEVEL_WARN:    esp_level = ESP_LOG_WARN; break;
    case LOG_LEVEL_INFO:    esp_level = ESP_LOG_INFO; break;
    case LOG_LEVEL_DEBUG:   esp_level = ESP_LOG_DEBUG; break;
    case LOG_LEVEL_VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
    default:
      mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid log level");
  }
  
  esp_log_level_set("*", esp_level);
  
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
  
  esp_log_level_t esp_level;
  switch (level) {
    case LOG_LEVEL_NONE:    esp_level = ESP_LOG_NONE; break;
    case LOG_LEVEL_ERROR:   esp_level = ESP_LOG_ERROR; break;
    case LOG_LEVEL_WARN:    esp_level = ESP_LOG_WARN; break;
    case LOG_LEVEL_INFO:    esp_level = ESP_LOG_INFO; break;
    case LOG_LEVEL_DEBUG:   esp_level = ESP_LOG_DEBUG; break;
    case LOG_LEVEL_VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
    default:
      mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid log level");
  }
  
  esp_log_level_set(tag, esp_level);
  
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

  ESP_LOGE(tag, "%s", msg);

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

  ESP_LOGW(tag, "%s", msg);

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

  ESP_LOGI(tag, "%s", msg);

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

  ESP_LOGD(tag, "%s", msg);

  return mrb_nil_value();
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
