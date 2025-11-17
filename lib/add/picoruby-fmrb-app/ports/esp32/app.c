#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/hash.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_err.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "fmrb_hid_msg.h"
#include "fmrb_task_config.h"
#include "fmrb_gfx.h"
#include "../../include/picoruby_fmrb_app.h"
#include "app_local.h"
#include "app_debug.h"

#include "freertos/task.h"

static const char* TAG = "app";

// extern declaration to access hal.c functions without header dependency
extern void mrb_set_in_c_funcall(mrb_state *mrb, int flag);
extern int mrb_get_in_c_funcall(mrb_state *mrb);

// Helper function: Check mruby ci pointer validity
// Static variables to track cibase/ciend changes across calls
static mrb_callinfo *prev_cibase = NULL;
static mrb_callinfo *prev_ciend = NULL;

static bool check_mrb_ci_valid(mrb_state *mrb, const char* location){
    if(!mrb || !mrb->c){
        FMRB_LOGE(TAG, "[%s] ERROR: mrb or mrb->c is NULL", location ? location : "?");
        return false;
    }

    struct mrb_context *c = mrb->c;
    bool valid = true;

    // Get task information
    fmrb_app_task_context_t* ctx = fmrb_current();
    TickType_t tick = fmrb_task_get_tick_count();
    int app_id = ctx ? ctx->app_id : -1;
    const char* app_name = ctx ? ctx->app_name : "N/A";

    // Calculate mrb_callinfo size and stack capacity
    size_t ci_size = sizeof(mrb_callinfo);
    size_t range_bytes = (char*)c->ciend - (char*)c->cibase;
    size_t capacity = range_bytes / ci_size;
    size_t current = ((char*)c->ci - (char*)c->cibase) / ci_size;
    int usage_pct = capacity > 0 ? (current * 100 / capacity) : 0;

    // Log task and timing information
    FMRB_LOGI(TAG, "[%s] ===== VM STATE CHECK =====", location ? location : "?");
    FMRB_LOGI(TAG, "[%s] Tick=%u App[%d]=%s Status=%d",
              location ? location : "?",
              (unsigned)tick, app_id, app_name, c->status);

    // Check ci pointer range
    if(c->ci < c->cibase || c->ci >= c->ciend){
        FMRB_LOGE(TAG, "[%s] ERROR: ci out of range! ci=%p not in [%p, %p)",
                  location ? location : "?", c->ci, c->cibase, c->ciend);
        valid = false;
    }

    // Detect cibase/ciend changes (realloc)
    bool cibase_changed = (prev_cibase != NULL && prev_cibase != c->cibase);
    bool ciend_changed = (prev_ciend != NULL && prev_ciend != c->ciend);

    if(cibase_changed || ciend_changed){
        FMRB_LOGW(TAG, "[%s] *** REALLOC DETECTED ***", location ? location : "?");
        FMRB_LOGW(TAG, "[%s]   cibase: %p -> %p (moved=%s, delta=%td bytes)",
                  location ? location : "?",
                  prev_cibase, c->cibase,
                  cibase_changed ? "YES" : "NO",
                  (char*)c->cibase - (char*)prev_cibase);
        FMRB_LOGW(TAG, "[%s]   ciend:  %p -> %p (moved=%s, delta=%td bytes)",
                  location ? location : "?",
                  prev_ciend, c->ciend,
                  ciend_changed ? "YES" : "NO",
                  (char*)c->ciend - (char*)prev_ciend);
    }

    // Log ci information with detailed stats
    FMRB_LOGI(TAG, "[%s] sizeof(mrb_callinfo)=%zu bytes", location ? location : "?", ci_size);
    FMRB_LOGI(TAG, "[%s] cibase=%p ciend=%p (capacity=%zu frames, range=%zu bytes)",
              location ? location : "?",
              c->cibase, c->ciend, capacity, range_bytes);
    FMRB_LOGI(TAG, "[%s] ci=%p (using %zu/%zu frames, %d%%, offset=%td bytes)",
              location ? location : "?",
              c->ci, current, capacity, usage_pct,
              (ptrdiff_t)((char*)c->ci - (char*)c->cibase));

    // Check which memory pool cibase belongs to
    fmrb_mempool_check_pointer(c->cibase);

    // If ci is different from cibase, check ci as well
    if(c->ci != c->cibase){
        fmrb_mempool_check_pointer(c->ci);
    }

    FMRB_LOGI(TAG, "[%s] ===== END VM STATE =====", location ? location : "?");

    // Update previous values for next comparison
    prev_cibase = c->cibase;
    prev_ciend = c->ciend;

    return valid;
}

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
// Dispatch HID event to Ruby on_event() method
bool dispatch_hid_event_to_ruby(mrb_state *mrb, mrb_value self, const fmrb_msg_t *msg)
{
    FMRB_LOGI(TAG, "=== dispatch_hid_event_to_ruby START ===");

    // Validate minimum size
    if (msg->size < 1) {
        FMRB_LOGW(TAG, "HID event message too small: size=%d", msg->size);
        return false;
    }

    // Read subtype from first byte
    uint8_t subtype = msg->data[0];
    FMRB_LOGI(TAG, "HID event subtype=%d", subtype);

    // Save GC arena before creating objects
    //int ai = mrb_gc_arena_save(mrb);

    // // Create event hash
    //mrb_value event_hash = mrb_hash_new(mrb);

    // switch (subtype) {
    //     case HID_MSG_KEY_DOWN:
    //     case HID_MSG_KEY_UP: {
    //         // Validate size before casting
    //         if (msg->size < sizeof(fmrb_hid_key_event_t)) {
    //             FMRB_LOGW(TAG, "Key event message too small: expected=%d, actual=%d",
    //                      sizeof(fmrb_hid_key_event_t), msg->size);
    //             goto cleanup;
    //         }

    //         // Cast to struct and access fields from the struct
    //         const fmrb_hid_key_event_t *key_event = (const fmrb_hid_key_event_t*)msg->data;

    //         mrb_value type_sym = (key_event->subtype == HID_MSG_KEY_DOWN)
    //             ? mrb_symbol_value(mrb_intern_cstr(mrb, "key_down"))
    //             : mrb_symbol_value(mrb_intern_cstr(mrb, "key_up"));

    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")), type_sym);
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "keycode")),
    //                     mrb_fixnum_value(key_event->keycode));
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "scancode")),
    //                     mrb_fixnum_value(key_event->scancode));
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "modifier")),
    //                     mrb_fixnum_value(key_event->modifier));
    //         break;
    //     }

    //     case HID_MSG_MOUSE_BUTTON_DOWN:
    //     case HID_MSG_MOUSE_BUTTON_UP: {
    //         // Validate size before casting
    //         if (msg->size < sizeof(fmrb_hid_mouse_button_event_t)) {
    //             FMRB_LOGW(TAG, "Mouse button event message too small: expected=%d, actual=%d",
    //                      sizeof(fmrb_hid_mouse_button_event_t), msg->size);
    //             goto cleanup;
    //         }

    //         // Cast to struct and access fields from the struct
    //         const fmrb_hid_mouse_button_event_t *mouse_event =
    //             (const fmrb_hid_mouse_button_event_t*)msg->data;

    //         mrb_value type_sym = (mouse_event->subtype == HID_MSG_MOUSE_BUTTON_DOWN)
    //             ? mrb_symbol_value(mrb_intern_cstr(mrb, "mouse_down"))
    //             : mrb_symbol_value(mrb_intern_cstr(mrb, "mouse_up"));

    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")), type_sym);
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "button")),
    //                     mrb_fixnum_value(mouse_event->button));
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "x")),
    //                     mrb_fixnum_value(mouse_event->x));
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "y")),
    //                     mrb_fixnum_value(mouse_event->y));
    //         break;
    //     }

    //     case HID_MSG_MOUSE_MOVE: {
    //         // Validate size before casting
    //         if (msg->size < sizeof(fmrb_hid_mouse_motion_event_t)) {
    //             FMRB_LOGW(TAG, "Mouse motion event message too small: expected=%d, actual=%d",
    //                      sizeof(fmrb_hid_mouse_motion_event_t), msg->size);
    //             goto cleanup;
    //         }

    //         // Cast to struct and access fields from the struct
    //         const fmrb_hid_mouse_motion_event_t *motion_event =
    //             (const fmrb_hid_mouse_motion_event_t*)msg->data;

    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")),
    //                     mrb_symbol_value(mrb_intern_cstr(mrb, "mouse_move")));
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "x")),
    //                     mrb_fixnum_value(motion_event->x));
    //         mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "y")),
    //                     mrb_fixnum_value(motion_event->y));
    //         break;
    //     }

    //     default:
    //         FMRB_LOGW(TAG, "Unknown HID event subtype: %d", subtype);
    //         goto cleanup;
    // }

    // Call Ruby on_event(event_hash) - picoruby standard pattern
    FMRB_LOGI(TAG, "=== BEFORE mrb_funcall ===");
    if (mrb->c && mrb->c->ci) {
        // Dump previous frame (ci-1) if exists
        size_t ci_offset = (char*)mrb->c->ci - (char*)mrb->c->cibase;
        if (ci_offset >= 48) {
            mrb_callinfo *prev_ci = (mrb_callinfo*)((char*)mrb->c->ci - 48);
            FMRB_LOGI(TAG, "Previous frame (ci-1):");
            app_debug_log_proc_details(mrb, prev_ci->proc, TAG);
        }

        // Dump current frame (ci)
        FMRB_LOGI(TAG, "Current frame (ci):");
        app_debug_log_proc_details(mrb, mrb->c->ci->proc, TAG);

        // Dump call stack
        app_debug_dump_callstack(mrb, TAG);
    } else {
        FMRB_LOGE(TAG, "mrb->c or mrb->c->ci is NULL");
    }
    check_mrb_ci_valid(mrb, "before_funcall");

    int ai = mrb_gc_arena_save(mrb);
    //FMRB_LOGI(TAG, "GC arena saved: ai=%d", ai);


    //mrb_funcall(mrb, self, "on_event", 1, event_hash);
    mrb_funcall(mrb, self, "on_event", 1, mrb_nil_value());


    FMRB_LOGI(TAG, "=== AFTER mrb_funcall ===");

    // Log ci->proc detailed information using debug helper
    if (mrb->c && mrb->c->ci) {
        // Dump previous frame (ci-1) if exists
        size_t ci_offset = (char*)mrb->c->ci - (char*)mrb->c->cibase;
        if (ci_offset >= 48) {
            mrb_callinfo *prev_ci = (mrb_callinfo*)((char*)mrb->c->ci - 48);
            FMRB_LOGI(TAG, "Previous frame (ci-1):");
            app_debug_log_proc_details(mrb, prev_ci->proc, TAG);
        }

        // Dump current frame (ci)
        FMRB_LOGI(TAG, "Current frame (ci):");
        app_debug_log_proc_details(mrb, mrb->c->ci->proc, TAG);

        // Dump call stack
        app_debug_dump_callstack(mrb, TAG);
    } else {
        FMRB_LOGE(TAG, "mrb->c or mrb->c->ci is NULL");
    }

    check_mrb_ci_valid(mrb, "after_funcall");

    mrb_gc_arena_restore(mrb, ai);
    //FMRB_LOGI(TAG, "GC arena restored to: ai=%d", ai);

    // Check for exception - picoruby standard pattern
    if (mrb->exc) {
        FMRB_LOGE(TAG, "Exception in on_event()");
        mrb_print_error(mrb);
        mrb->exc = NULL;
        return false;
    }

//cleanup:
    // Restore GC arena
    //FMRB_LOGI(TAG, "Restoring GC arena (ai=%d)", ai);
    //mrb_gc_arena_restore(mrb, ai);

    FMRB_LOGI(TAG, "=== dispatch_hid_event_to_ruby END ===");
    return true;
}

static mrb_value mrb_fmrb_app_spin(mrb_state *mrb, mrb_value self)
{
    // UBaseType_t hw = uxTaskGetStackHighWaterMark(NULL);
    // FMRB_LOGI(TAG, "FmrbApp stack high water mark = %u words (~%u bytes)",
    //           (unsigned)hw, (unsigned)(hw * sizeof(StackType_t)));
    
    // Set in_c_funcall flag to prevent mrb_tick() interference

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }
    FMRB_LOGI(TAG, ">>>>>>>>> _spin(%s) START >>>>>>>>>>>>>",ctx->app_name);
    mrb_set_in_c_funcall(mrb, 1);

    mrb_int timeout_ms;
    mrb_get_args(mrb, "i", &timeout_ms);

    // Record start time to ensure we wait for the full timeout period
    TickType_t start_tick = fmrb_task_get_tick_count();
    TickType_t target_tick = start_tick + FMRB_MS_TO_TICKS(timeout_ms);

    // Save GC arena before loop - standard pattern for repeated mrb_funcall
    //int ai = mrb_gc_arena_save(mrb);


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

            // Dispatch message based on type
            if (msg.type == FMRB_MSG_TYPE_HID_EVENT) {
                bool bret = dispatch_hid_event_to_ruby(mrb, self, &msg);
                if(bret == false){
                    return mrb_nil_value();
                }
            }

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

    // Restore GC arena after loop
    //mrb_gc_arena_restore(mrb, ai);


    // Clear in_c_funcall flag
    mrb_set_in_c_funcall(mrb, 0);

    FMRB_LOGI(TAG, "<<<<<<<<< _spin(%s) END <<<<<<<<<<<<<",ctx->app_name);
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
