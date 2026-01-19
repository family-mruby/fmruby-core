#include <stdarg.h>
#include <stdio.h>
#include "picoruby.h"
#include "esp_log.h"

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
 * Log error message
 * @param tag [String] Tag name
 * @param message [String] Log message
 *
 * Example:
 *   Log.e("KERNEL", "Failed to initialize")
 */
static mrb_value
mrb_log_e(mrb_state *mrb, mrb_value self)
{
  char *tag, *msg;
  
  mrb_get_args(mrb, "zz", &tag, &msg);
  
  ESP_LOGE(tag, "%s", msg);
  
  return mrb_nil_value();
}

/**
 * Log warning message
 * @param tag [String] Tag name
 * @param message [String] Log message
 *
 * Example:
 *   Log.w("KERNEL", "Low memory warning")
 */
static mrb_value
mrb_log_w(mrb_state *mrb, mrb_value self)
{
  char *tag, *msg;
  
  mrb_get_args(mrb, "zz", &tag, &msg);
  
  ESP_LOGW(tag, "%s", msg);
  
  return mrb_nil_value();
}

/**
 * Log info message
 * @param tag [String] Tag name
 * @param message [String] Log message
 *
 * Example:
 *   Log.i("KERNEL", "System initialized")
 */
static mrb_value
mrb_log_i(mrb_state *mrb, mrb_value self)
{
  char *tag, *msg;
  
  mrb_get_args(mrb, "zz", &tag, &msg);
  
  ESP_LOGI(tag, "%s", msg);
  
  return mrb_nil_value();
}

/**
 * Log debug message
 * @param tag [String] Tag name
 * @param message [String] Log message
 *
 * Example:
 *   Log.d("KERNEL", "Debug info")
 */
static mrb_value
mrb_log_d(mrb_state *mrb, mrb_value self)
{
  char *tag, *msg;
  
  mrb_get_args(mrb, "zz", &tag, &msg);
  
  ESP_LOGD(tag, "%s", msg);
  
  return mrb_nil_value();
}

/**
 * Log verbose message
 * @param tag [String] Tag name
 * @param message [String] Log message
 *
 * Example:
 *   Log.v("KERNEL", "Verbose debug info")
 */
static mrb_value
mrb_log_v(mrb_state *mrb, mrb_value self)
{
  char *tag, *msg;
  
  mrb_get_args(mrb, "zz", &tag, &msg);
  
  ESP_LOGV(tag, "%s", msg);
  
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
  
  // Logging methods
  mrb_define_module_function(mrb, log_module, "e", mrb_log_e, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, log_module, "w", mrb_log_w, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, log_module, "i", mrb_log_i, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, log_module, "d", mrb_log_d, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, log_module, "v", mrb_log_v, MRB_ARGS_REQ(2));
}

void
mrb_fmrb_log_final(mrb_state *mrb)
{
  // Cleanup if needed
}
