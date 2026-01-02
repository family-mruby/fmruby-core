#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/hash.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
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

#include "hal.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#else
#include <sys/sysinfo.h>
#endif

static const char* TAG = "app";

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
    fmrb_tick_t tick = fmrb_task_get_tick_count();
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

    // Set @pos_x and @pos_y instance variables
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@pos_x"),
               mrb_fixnum_value(ctx->window_pos_x));
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@pos_y"),
               mrb_fixnum_value(ctx->window_pos_y));

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

// Dispatch HID event to Ruby on_event() method
bool dispatch_hid_event_to_ruby(mrb_state *mrb, mrb_value self, const fmrb_msg_t *msg)
{
    //FMRB_LOGI(TAG, "=== dispatch_hid_event_to_ruby START ===");
    if (msg->size < 1) {
        FMRB_LOGW(TAG, "HID event message too small: size=%d", msg->size);
        return false;
    }

    uint8_t subtype = msg->data[0];
    //FMRB_LOGI(TAG, "HID event subtype=%d", subtype);

    //Save GC arena before creating objects
    int ai = mrb_gc_arena_save(mrb);

    mrb_value event_hash = mrb_hash_new(mrb);

    switch (subtype) {
        case HID_MSG_KEY_DOWN:
        case HID_MSG_KEY_UP: {
            // Validate size before casting
            if (msg->size < sizeof(fmrb_hid_key_event_t)) {
                FMRB_LOGW(TAG, "Key event message too small: expected=%d, actual=%d",
                         sizeof(fmrb_hid_key_event_t), msg->size);
                goto cleanup;
            }

            // Cast to struct and access fields from the struct
            const fmrb_hid_key_event_t *key_event = (const fmrb_hid_key_event_t*)msg->data;

            mrb_value type_sym = (key_event->subtype == HID_MSG_KEY_DOWN)
                ? mrb_symbol_value(mrb_intern_cstr(mrb, "key_down"))
                : mrb_symbol_value(mrb_intern_cstr(mrb, "key_up"));

            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")), type_sym);
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "keycode")),
                        mrb_fixnum_value(key_event->keycode));
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "scancode")),
                        mrb_fixnum_value(key_event->scancode));
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "modifier")),
                        mrb_fixnum_value(key_event->modifier));
            break;
        }

        case HID_MSG_MOUSE_BUTTON_DOWN:
        case HID_MSG_MOUSE_BUTTON_UP: {
            // Validate size before casting
            if (msg->size < sizeof(fmrb_hid_mouse_button_event_t)) {
                FMRB_LOGW(TAG, "Mouse button event message too small: expected=%d, actual=%d",
                         sizeof(fmrb_hid_mouse_button_event_t), msg->size);
                goto cleanup;
            }

            // Cast to struct and access fields from the struct
            const fmrb_hid_mouse_button_event_t *mouse_event =
                (const fmrb_hid_mouse_button_event_t*)msg->data;

            mrb_value type_sym = (mouse_event->subtype == HID_MSG_MOUSE_BUTTON_DOWN)
                ? mrb_symbol_value(mrb_intern_cstr(mrb, "mouse_down"))
                : mrb_symbol_value(mrb_intern_cstr(mrb, "mouse_up"));

            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")), type_sym);
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "button")),
                        mrb_fixnum_value(mouse_event->button));
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "x")),
                        mrb_fixnum_value(mouse_event->x));
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "y")),
                        mrb_fixnum_value(mouse_event->y));
            break;
        }

        case HID_MSG_MOUSE_MOVE: {
            // Validate size before casting
            if (msg->size < sizeof(fmrb_hid_mouse_motion_event_t)) {
                FMRB_LOGW(TAG, "Mouse motion event message too small: expected=%d, actual=%d",
                         sizeof(fmrb_hid_mouse_motion_event_t), msg->size);
                goto cleanup;
            }

            // Cast to struct and access fields from the struct
            const fmrb_hid_mouse_motion_event_t *motion_event =
                (const fmrb_hid_mouse_motion_event_t*)msg->data;

            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")),
                        mrb_symbol_value(mrb_intern_cstr(mrb, "mouse_move")));
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "x")),
                        mrb_fixnum_value(motion_event->x));
            mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "y")),
                        mrb_fixnum_value(motion_event->y));
            break;
        }

        default:
            FMRB_LOGW(TAG, "Unknown HID event subtype: %d", subtype);
            goto cleanup;
    }

    #if 0
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
    #endif 

    mrb_funcall(mrb, self, "on_event", 1, event_hash);
    //mrb_funcall(mrb, self, "on_event", 1, mrb_nil_value());

    #if 0
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
    #endif

    // Check for exception - picoruby standard pattern
    if (mrb->exc) {
        FMRB_LOGE(TAG, "Exception in on_event()");
        mrb_print_error(mrb);
        mrb->exc = NULL;
        return false;
    }

cleanup:
    //FMRB_LOGI(TAG, "=== dispatch_hid_event_to_ruby END ===");
    mrb_gc_arena_restore(mrb, ai);
    return true;
}

static mrb_value mrb_fmrb_app_spin(mrb_state *mrb, mrb_value self)
{

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }
    FMRB_LOGD(TAG, ">>>>>>>>> _spin(%s) START >>>>>>>>>>>>>",ctx->app_name);
    mrb_set_in_c_funcall(mrb, MRB_C_FUNCALL_ENTER);

    mrb_int timeout_ms;
    mrb_get_args(mrb, "i", &timeout_ms);

    // Record start time to ensure we wait for the full timeout period
    fmrb_tick_t start_tick = fmrb_task_get_tick_count();
    fmrb_tick_t target_tick = start_tick + FMRB_MS_TO_TICKS(timeout_ms);

    // Spin Loop - process messages until timeout expires
    while(true){
        // Calculate remaining time
        fmrb_tick_t current_tick = fmrb_task_get_tick_count();
        if (current_tick >= target_tick) {
            // Timeout expired, exit spin loop
            break;
        }

        fmrb_tick_t remaining_ticks = target_tick - current_tick;

        // Try to receive message with remaining timeout
        fmrb_msg_t msg;
        fmrb_err_t ret = fmrb_msg_receive(ctx->app_id, &msg, remaining_ticks);

        if (ret == FMRB_OK) {
            // Message received
            FMRB_LOGI(TAG, "App %s received message: type=%d", ctx->app_name, msg.type);

            // Dispatch message based on type
            if (msg.type == FMRB_MSG_TYPE_HID_EVENT) {
                //mrb_set_in_c_funcall(mrb, MRB_C_FUNCALL_ENTER);
                bool bret = dispatch_hid_event_to_ruby(mrb, self, &msg);
                //mrb_set_in_c_funcall(mrb, MRB_C_FUNCALL_EXIT);
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

    // Clear in_c_funcall flag
    mrb_set_in_c_funcall(mrb, MRB_C_FUNCALL_EXIT);

    FMRB_LOGD(TAG, "<<<<<<<<< _spin(%s) END <<<<<<<<<<<<<",ctx->app_name);
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

// FmrbApp#_set_window_param(param_sym, value) -> self
// Set window parameter (pos_x, pos_y)
static mrb_value mrb_fmrb_app_set_window_param(mrb_state *mrb, mrb_value self)
{
    mrb_sym param_sym;
    mrb_int value;
    mrb_get_args(mrb, "ni", &param_sym, &value);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }

    const char* param_name = mrb_sym2name(mrb, param_sym);

    // Update context and instance variable
    if (strcmp(param_name, "pos_x") == 0) {
        ctx->window_pos_x = (uint16_t)value;
        mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@pos_x"), mrb_fixnum_value(value));
        FMRB_LOGI(TAG, "Set window pos_x=%d for app %s", (int)value, ctx->app_name);
    } else if (strcmp(param_name, "pos_y") == 0) {
        ctx->window_pos_y = (uint16_t)value;
        mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@pos_y"), mrb_fixnum_value(value));
        FMRB_LOGI(TAG, "Set window pos_y=%d for app %s", (int)value, ctx->app_name);
    } else {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "Unknown window parameter: %s", param_name);
    }

    return self;
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

// FmrbApp.ps() -> Array[Hash]
// Get process list with memory statistics
static mrb_value mrb_fmrb_app_s_ps(mrb_state *mrb, mrb_value self)
{
    fmrb_app_info_t list[FMRB_MAX_APPS];
    int32_t count = fmrb_app_ps(list, FMRB_MAX_APPS);

    mrb_value result = mrb_ary_new_capa(mrb, count);

    for (int32_t i = 0; i < count; i++) {
        mrb_value hash = mrb_hash_new_capa(mrb, 12);

        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "id")),
                     mrb_fixnum_value(list[i].app_id));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "name")),
                     mrb_str_new_cstr(mrb, list[i].app_name));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "state")),
                     mrb_fixnum_value(list[i].state));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")),
                     mrb_fixnum_value(list[i].type));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "vm_type")),
                     mrb_fixnum_value(list[i].vm_type));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "gen")),
                     mrb_fixnum_value(list[i].gen));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "stack_water")),
                     mrb_fixnum_value(list[i].stack_high_water));

        // Memory statistics
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "mem_total")),
                     mrb_fixnum_value(list[i].mem_total));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "mem_used")),
                     mrb_fixnum_value(list[i].mem_used));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "mem_free")),
                     mrb_fixnum_value(list[i].mem_free));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "mem_frag")),
                     mrb_fixnum_value(list[i].mem_frag));

        mrb_ary_push(mrb, result, hash);
    }

    return result;
}

// FmrbApp.sys_pool_info() -> Hash
// Get system pool (fmrb_sys_malloc) information (TLSF allocator)
static mrb_value mrb_fmrb_app_s_sys_pool_info(mrb_state *mrb, mrb_value self)
{
    fmrb_pool_stats_t stats;
    mrb_value hash = mrb_hash_new_capa(mrb, 5);

    if (fmrb_sys_mem_get_stats(&stats) == 0) {
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "total")),
                     mrb_fixnum_value(stats.total_size));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "used")),
                     mrb_fixnum_value(stats.used_size));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free")),
                     mrb_fixnum_value(stats.free_size));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "used_blocks")),
                     mrb_fixnum_value(stats.used_blocks));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free_blocks")),
                     mrb_fixnum_value(stats.free_blocks));
    } else {
        // Return zeros on error
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "total")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "used")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "used_blocks")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free_blocks")),
                     mrb_fixnum_value(0));
    }

    return hash;
}

// FmrbApp.heap_info() -> Hash
// Get system heap information (ESP32 heap)
static mrb_value mrb_fmrb_app_s_heap_info(mrb_state *mrb, mrb_value self)
{
    mrb_value hash = mrb_hash_new_capa(mrb, 4);

#ifndef CONFIG_IDF_TARGET_LINUX
    // ESP32: Use ESP-IDF heap API
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free")),
                 mrb_fixnum_value(free_heap));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "total")),
                 mrb_fixnum_value(total_heap));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "min_free")),
                 mrb_fixnum_value(min_free_heap));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "largest_block")),
                 mrb_fixnum_value(largest_free_block));
#else
    // Linux: Use sysinfo to get system memory information
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        size_t total_ram = si.totalram * si.mem_unit;
        size_t free_ram = si.freeram * si.mem_unit;

        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free")),
                     mrb_fixnum_value(free_ram));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "total")),
                     mrb_fixnum_value(total_ram));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "min_free")),
                     mrb_fixnum_value(free_ram));  // Linux has no equivalent, use current free
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "largest_block")),
                     mrb_fixnum_value(free_ram));  // Linux has no equivalent, use current free
    } else {
        // If sysinfo fails, return zeros
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "total")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "min_free")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "largest_block")),
                     mrb_fixnum_value(0));
    }
#endif

    return hash;
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
    mrb_define_method(mrb, app_class, "_set_window_param", mrb_fmrb_app_set_window_param, MRB_ARGS_REQ(2));

    // Class methods
    mrb_define_class_method(mrb, app_class, "ps", mrb_fmrb_app_s_ps, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "heap_info", mrb_fmrb_app_s_heap_info, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "sys_pool_info", mrb_fmrb_app_s_sys_pool_info, MRB_ARGS_NONE());

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

    // Path length constant
    mrb_define_const(mrb, app_class, "MAX_PATH_LEN", mrb_fixnum_value(FMRB_MAX_PATH_LEN));

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
