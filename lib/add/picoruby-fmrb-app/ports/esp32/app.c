#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_log.h"
#include "fmrb_err.h"
#include "fmrb_msg.h"
#include "../../include/picoruby_fmrb_app.h"
#include "app_local.h"

static const char* TAG = "app";

// FmrbApp#_init() - Initialize app instance from C context
// Sets @name and @canvas instance variables, creates message queue
static mrb_value mrb_fmrb_app_init(mrb_state *mrb, mrb_value self)
{
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }

    FMRB_LOGI(TAG, "_init: app_id=%d, name=%s", ctx->app_id, ctx->app_name);

    // Set @name instance variable
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@name"),
               mrb_str_new_cstr(mrb, ctx->app_name));

    // Set @canvas instance variable
    // Canvas ID = app_id + 1 (0 is reserved for screen)
    // TODO: Canvas allocation should be done by Kernel
    mrb_int canvas_id = ctx->app_id + 1;
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@canvas"),
               mrb_fixnum_value(canvas_id));

    // Set @window_width and @window_height instance variables
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@window_width"),
               mrb_fixnum_value(ctx->window_width));
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@window_height"),
               mrb_fixnum_value(ctx->window_height));

    FMRB_LOGI(TAG, "Assigned canvas_id=%d, window=%dx%d to app %s",
             (int)canvas_id, ctx->window_width, ctx->window_height, ctx->app_name);

    // Create message queue for this app
    fmrb_msg_queue_config_t queue_config = {
        .queue_length = FMRB_USER_APP_MSG_QUEUE_LEN,
        .message_size = sizeof(fmrb_msg_t)
    };

    fmrb_err_t ret = fmrb_msg_create_queue(ctx->app_id, &queue_config);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "Failed to create message queue: %d", ret);
    }

    return self;
}

// FmrbApp#_spin(timeout_ms) - Process messages and wait
// Receives messages from queue with timeout, called from Ruby main_loop()
static mrb_value mrb_fmrb_app_spin(mrb_state *mrb, mrb_value self)
{
    mrb_int timeout_ms;
    mrb_get_args(mrb, "i", &timeout_ms);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }

    // Record start time to ensure we wait for the full timeout period
    TickType_t start_tick = fmrb_task_get_tick_count();
    TickType_t target_tick = start_tick + FMRB_MS_TO_TICKS(timeout_ms);

    // Spin Loop - process messages until timeout expires
    while(true){
        // Calculate remaining time
        TickType_t current_tick = fmrb_task_get_tick_count();
        if (current_tick >= target_tick) {
            // Timeout expired, exit spin loop
            break;
        }

        TickType_t remaining_ticks = target_tick - current_tick;

        // Try to receive message with remaining timeout
        fmrb_msg_t msg;
        fmrb_err_t ret = fmrb_msg_receive(ctx->app_id, &msg, remaining_ticks);

        if (ret == FMRB_OK) {
            // Message received
            FMRB_LOGI(TAG, "App %s received message: type=%d", ctx->app_name, msg.type);

            // TODO: Convert message to Ruby event and call on_event()
            // For now, just log it
            // Continue loop to process more messages or wait for remaining time
        } else if (ret == FMRB_ERR_TIMEOUT) {
            // Timeout - normal case when no messages
            // Exit spin loop (full timeout period has elapsed)
            break;
        } else {
            FMRB_LOGW(TAG, "App %s message receive error: %d", ctx->app_name, ret);
            break;
        }
    }

    return mrb_nil_value();
}

void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb)
{
    // Define FmrbApp class
    struct RClass *app_class = mrb_define_class(mrb, "FmrbApp", mrb->object_class);

    // Instance methods (called from Ruby instances)
    mrb_define_method(mrb, app_class, "_init", mrb_fmrb_app_init, MRB_ARGS_NONE());
    mrb_define_method(mrb, app_class, "_spin", mrb_fmrb_app_spin, MRB_ARGS_REQ(1));

    // Initialize graphics subsystem
    mrb_fmrb_gfx_init(mrb);

    // Audio subsystem will be initialized when needed
    //mrb_fmrb_audio_init(mrb);
}

void mrb_picoruby_fmrb_app_final_impl(mrb_state *mrb)
{
    mrb_fmrb_gfx_final(mrb);
    //mrb_fmrb_audio_final(mrb);
}
