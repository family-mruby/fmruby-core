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
#include "fmrb_msg_payload.h"
#include "fmrb_task_config.h"
#include "fmrb_gfx.h"
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

    // Set @window_width and @window_height instance variables
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@window_width"),
               mrb_fixnum_value(ctx->window_width));
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@window_height"),
               mrb_fixnum_value(ctx->window_height));

    // Allocate Canvas for non-headless apps
    if (!ctx->headless) {
        fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;

        // Get global graphics context
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (!gfx_ctx) {
            mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
        }

        // Create canvas for app window
        fmrb_gfx_err_t ret = fmrb_gfx_create_canvas(
            gfx_ctx,
            ctx->window_width,
            ctx->window_height,
            &canvas_id
        );

        if (ret != FMRB_GFX_OK) {
            mrb_raisef(mrb, E_RUNTIME_ERROR,
                       "Failed to create canvas: %d", ret);
        }

        // Set @canvas instance variable
        mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@canvas"),
                   mrb_fixnum_value(canvas_id));

        FMRB_LOGI(TAG, "Created canvas %u (%dx%d) for app %s",
                 canvas_id, ctx->window_width, ctx->window_height, ctx->app_name);
    } else {
        // Headless app: no canvas, @canvas remains unset (nil)
        FMRB_LOGI(TAG, "Headless app %s: no canvas allocated", ctx->app_name);
    }

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

// FmrbApp#_cleanup() - Cleanup app resources (canvas, message queue)
// Called from Ruby destroy() method when app terminates
static mrb_value mrb_fmrb_app_cleanup(mrb_state *mrb, mrb_value self)
{
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        return mrb_nil_value();
    }

    FMRB_LOGI(TAG, "_cleanup: app_id=%d, name=%s", ctx->app_id, ctx->app_name);

    // Get @canvas instance variable
    mrb_value canvas_val = mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "@canvas"));

    // Delete canvas if allocated (not screen)
    if (mrb_fixnum_p(canvas_val)) {
        fmrb_canvas_handle_t canvas_id = (fmrb_canvas_handle_t)mrb_fixnum(canvas_val);

        // Only delete user-allocated canvases (not screen)
        if (canvas_id != FMRB_CANVAS_SCREEN) {
            fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
            if (gfx_ctx) {
                fmrb_gfx_err_t ret = fmrb_gfx_delete_canvas(gfx_ctx, canvas_id);
                if (ret == FMRB_GFX_OK) {
                    FMRB_LOGI(TAG, "Deleted canvas %u for app %s",
                             canvas_id, ctx->app_name);
                } else {
                    FMRB_LOGW(TAG, "Failed to delete canvas %u: %d",
                             canvas_id, ret);
                }
            }
        }
    }

    // Delete message queue
    fmrb_err_t ret = fmrb_msg_delete_queue(ctx->app_id);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to delete message queue for app %s: %d",
                 ctx->app_name, ret);
    }

    return mrb_nil_value();
}

// FmrbApp#_send_message(dest_pid, msg_type, data) -> bool
// Send a message to another task
static mrb_value mrb_fmrb_app_send_message(mrb_state *mrb, mrb_value self)
{
    mrb_int dest_pid, msg_type;
    mrb_value data_val;
    mrb_get_args(mrb, "iiS", &dest_pid, &msg_type, &data_val);

    FMRB_LOGD(TAG, "send_message: dest_pid=%d, msg_type=%d, data_len=%d",
              (int)dest_pid, (int)msg_type, RSTRING_LEN(data_val));

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        FMRB_LOGE(TAG, "send_message: No app context available");
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }

    FMRB_LOGD(TAG, "send_message: ctx->app_id=%d, ctx->app_name=%s", ctx->app_id, ctx->app_name);

    // Build message
    fmrb_msg_t msg = {
        .type = (fmrb_msg_type_t)msg_type,
        .src_pid = ctx->app_id,
        .size = RSTRING_LEN(data_val),
    };

    // Check payload size
    if (msg.size > FMRB_MAX_MSG_PAYLOAD_SIZE) {
        FMRB_LOGE(TAG, "send_message: Payload too large: %d > %d",
                 (int)msg.size, FMRB_MAX_MSG_PAYLOAD_SIZE);
        mrb_raisef(mrb, E_ARGUMENT_ERROR,
                   "Message payload too large: %d > %d",
                   (int)msg.size, FMRB_MAX_MSG_PAYLOAD_SIZE);
    }

    // Copy payload
    memcpy(msg.data, RSTRING_PTR(data_val), msg.size);

    // Send message with 1 second timeout
    fmrb_err_t ret = fmrb_msg_send((fmrb_proc_id_t)dest_pid, &msg, 1000);

    if (ret == FMRB_OK) {
        return mrb_true_value();
    } else {
        FMRB_LOGE(TAG, "App %s failed to send message to pid=%d: %d",
                 ctx->app_name, (int)dest_pid, ret);
        return mrb_false_value();
    }
}

void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb)
{
    // Define FmrbApp class
    struct RClass *app_class = mrb_define_class(mrb, "FmrbApp", mrb->object_class);

    // Instance methods (called from Ruby instances)
    mrb_define_method(mrb, app_class, "_init", mrb_fmrb_app_init, MRB_ARGS_NONE());
    mrb_define_method(mrb, app_class, "_spin", mrb_fmrb_app_spin, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, app_class, "_cleanup", mrb_fmrb_app_cleanup, MRB_ARGS_NONE());
    mrb_define_method(mrb, app_class, "_send_message", mrb_fmrb_app_send_message, MRB_ARGS_REQ(3));

    // Process ID constants
    mrb_define_const(mrb, app_class, "PROC_ID_KERNEL", mrb_fixnum_value(PROC_ID_KERNEL));
    mrb_define_const(mrb, app_class, "PROC_ID_HOST", mrb_fixnum_value(PROC_ID_HOST));
    mrb_define_const(mrb, app_class, "PROC_ID_SYSTEM_APP", mrb_fixnum_value(PROC_ID_SYSTEM_APP));
    mrb_define_const(mrb, app_class, "PROC_ID_USER_APP0", mrb_fixnum_value(PROC_ID_USER_APP0));
    mrb_define_const(mrb, app_class, "PROC_ID_USER_APP1", mrb_fixnum_value(PROC_ID_USER_APP1));
    mrb_define_const(mrb, app_class, "PROC_ID_USER_APP2", mrb_fixnum_value(PROC_ID_USER_APP2));

    // Message type constants
    mrb_define_const(mrb, app_class, "MSG_TYPE_APP_CONTROL", mrb_fixnum_value(FMRB_MSG_TYPE_APP_CONTROL));
    mrb_define_const(mrb, app_class, "MSG_TYPE_APP_GFX", mrb_fixnum_value(FMRB_MSG_TYPE_APP_GFX));
    mrb_define_const(mrb, app_class, "MSG_TYPE_APP_AUDIO", mrb_fixnum_value(FMRB_MSG_TYPE_APP_AUDIO));

    // App control message subtypes
    mrb_define_const(mrb, app_class, "APP_CTRL_SPAWN", mrb_fixnum_value(FMRB_APP_CTRL_SPAWN));
    mrb_define_const(mrb, app_class, "APP_CTRL_KILL", mrb_fixnum_value(FMRB_APP_CTRL_KILL));
    mrb_define_const(mrb, app_class, "APP_CTRL_SUSPEND", mrb_fixnum_value(FMRB_APP_CTRL_SUSPEND));
    mrb_define_const(mrb, app_class, "APP_CTRL_RESUME", mrb_fixnum_value(FMRB_APP_CTRL_RESUME));

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
