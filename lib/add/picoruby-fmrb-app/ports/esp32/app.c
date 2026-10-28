#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/hash.h>
#include <mruby/array.h>
// mrb_gc_scheduler_pending / mrb_gc_step / mrb_gc_scheduler_jitter: the
// embedder-generic primitives a custom scheduler uses to drive collection
// from its own idle points (see the header's contract).
#include <mruby/gc.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_err.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "fmrb_file_transfer_msg.h"
#include "hw_proxy.h"
#include "fmrb_hid_msg.h"
#include "fmrb_task_config.h"
#include "fmrb_hid_event.h"
#include "fmrb_gfx.h"
#include "fmrb_app_canvas.h"
#include "fmrb_kernel.h"
#include "fmrb_platform_sys.h"
#include "fmrb_clipboard.h"
#include "fmrb_hal_time.h"
#include "../../include/picoruby_fmrb_app.h"
#include "app_local.h"
#include "app_debug.h"
#include "host_task.h"
#include "usb_task.h"

#include "hal.h"
#include "task.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#include "esp_system.h"
#else
#include <sys/sysinfo.h>
#include <stdlib.h>
#endif

#if defined(CONFIG_IDF_TARGET_ESP32P4)
// Microphone (doc/mic_spectrum). Modern only: the ES7210 is Tab5 hardware and
// the driver is compiled for that target alone. Everywhere else FmrbMic still
// answers -- available? is false and the rest are no-ops -- so an app can ask
// without knowing which machine it is on.
#include "audio_p4.h"
#endif

static const char* TAG = "app";

// What one GC step has been costing this app. Biased upward: it jumps to any
// step that runs longer and decays by an eighth otherwise, so a slow step
// stops the app from starting another it cannot afford, while a single
// outlier fades after a few steps. Per app id, because the pause depends on
// the size of that app's heap. See spin_idle_gc.
static uint32_t s_gc_step_est_us[FMRB_MAX_APPS];

// Helper function: Check mruby ci pointer validity
// Static variables to track cibase/ciend changes across calls
#if 0
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
#endif

// FmrbApp#_init() - Initialize app instance from C context
// Sets @name and @canvas instance variables, creates message queue
static mrb_value mrb_fmrb_app_init(mrb_state *mrb, mrb_value self)
{
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }

    FMRB_LOGI(TAG, "_init: app_id=%d, name=%s", ctx->app_id, ctx->app_name);

    // A new occupant of this slot has its own heap; do not inherit what a
    // step cost the previous one (see spin_idle_gc).
    if (ctx->app_id >= 0 && ctx->app_id < FMRB_MAX_APPS) {
        s_gc_step_est_us[ctx->app_id] = 0;
    }

    // Set @name instance variable
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@_name"),
               mrb_str_new_cstr(mrb, ctx->app_name));

    // Set @fullscreen flag
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@_fullscreen"),
               ctx->fullscreen ? mrb_true_value() : mrb_false_value());

    // Set @rounded_corners flag (drives composite-region-based corner
    // shaping in fmrb-app.rb). Always false for fullscreen apps.
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@_rounded_corners"),
               (!ctx->fullscreen && ctx->rounded_corners) ? mrb_true_value() : mrb_false_value());

    // Set @platform symbol (:linux or :esp32)
#ifdef CONFIG_IDF_TARGET_LINUX
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@platform"),
               mrb_symbol_value(mrb_intern_cstr(mrb, "linux")));
#else
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@platform"),
               mrb_symbol_value(mrb_intern_cstr(mrb, "esp32")));
#endif

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

    // Canvases (window + optional wallpaper layer) and their registration on
    // the context; a headless app gets none and leaves @canvas nil.
    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_canvas_handle_t bg_canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_err_t canvas_ret = fmrb_app_canvas_init(ctx, &canvas_id, &bg_canvas_id);
    if (canvas_ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to create canvas: %d", canvas_ret);
    }
    if (canvas_id != FMRB_CANVAS_SCREEN) {
        mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@_canvas"),
                   mrb_fixnum_value(canvas_id));
    }
    if (bg_canvas_id != FMRB_CANVAS_SCREEN) {
        mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@_bg_canvas"),
                   mrb_fixnum_value(bg_canvas_id));
    }

    // Message queue is created at task spawn time in fmrb_app.c (before the
    // RUNNING state transition) so senders cannot race ahead of the queue's
    // existence.

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

    fmrb_hid_event_t ev;
    if (!fmrb_hid_event_decode(msg->data, msg->size, &ev)) {
        FMRB_LOGW(TAG, "Undecodable HID event: subtype=%d size=%d",
                  msg->data[0], msg->size);
        return false;
    }

    //Save GC arena before creating objects
    int ai = mrb_gc_arena_save(mrb);

    // Mouse moves arrive as a 30Hz stream and dominate event-path garbage
    // (~160B per event). Reuse one per-VM Hash for them, rooted through a
    // global variable so the GC keeps it alive. Handlers consume move
    // events on the spot and must not retain them across events; every
    // other event type still gets a fresh Hash.
    mrb_value event_hash;
    if (ev.type == FMRB_HID_EVENT_MOUSE_MOVE) {
        mrb_sym gv_sym = mrb_intern_lit(mrb, "$fmrb_move_ev");
        event_hash = mrb_gv_get(mrb, gv_sym);
        if (!mrb_hash_p(event_hash)) {
            event_hash = mrb_hash_new_capa(mrb, 4);
            mrb_gv_set(mrb, gv_sym, event_hash);
        }
    } else {
        event_hash = mrb_hash_new(mrb);
    }

#define HID_SET(key, value) \
    mrb_hash_set(mrb, event_hash, mrb_symbol_value(mrb_intern_cstr(mrb, key)), (value))
#define HID_SET_INT(key, value) HID_SET(key, mrb_fixnum_value(value))
#define HID_SET_TYPE(name) HID_SET("type", mrb_symbol_value(mrb_intern_cstr(mrb, name)))

    switch (ev.type) {
        case FMRB_HID_EVENT_KEY_DOWN:
        case FMRB_HID_EVENT_KEY_UP:
            HID_SET_TYPE(ev.type == FMRB_HID_EVENT_KEY_DOWN ? "key_down" : "key_up");
            HID_SET_INT("keycode", ev.keycode);
            HID_SET_INT("scancode", ev.scancode);
            HID_SET_INT("modifier", ev.modifier);
            HID_SET_INT("character", (uint8_t)ev.character);
            break;

        case FMRB_HID_EVENT_MOUSE_DOWN:
        case FMRB_HID_EVENT_MOUSE_UP:
            HID_SET_TYPE(ev.type == FMRB_HID_EVENT_MOUSE_DOWN ? "mouse_down" : "mouse_up");
            HID_SET_INT("button", ev.button);
            HID_SET_INT("x", ev.x);
            HID_SET_INT("y", ev.y);
            break;

        case FMRB_HID_EVENT_MOUSE_MOVE:
            HID_SET_TYPE("mouse_move");
            HID_SET_INT("x", ev.x);
            HID_SET_INT("y", ev.y);
            break;

        // The wheel carries notches, positive away from the user. x and y are
        // where the pointer was; the event itself was delivered by focus, so
        // they are for the app's information rather than for hit testing.
        case FMRB_HID_EVENT_MOUSE_WHEEL:
            HID_SET_TYPE("mouse_wheel");
            HID_SET_INT("delta", ev.wheel);
            HID_SET_INT("x", ev.x);
            HID_SET_INT("y", ev.y);
            break;

        case FMRB_HID_EVENT_GAMEPAD_DOWN:
        case FMRB_HID_EVENT_GAMEPAD_UP:
            HID_SET_TYPE(ev.type == FMRB_HID_EVENT_GAMEPAD_DOWN ? "gamepad_down" : "gamepad_up");
            HID_SET_INT("gamepad_id", ev.gamepad_id);
            HID_SET_INT("button", ev.button);
            break;

        case FMRB_HID_EVENT_GAMEPAD_AXIS:
            HID_SET_TYPE("gamepad_axis");
            HID_SET_INT("gamepad_id", ev.gamepad_id);
            HID_SET_INT("axis", ev.axis);
            HID_SET_INT("value", ev.value);
            break;

        // Kana input mode changed (JP layout). Apps that show the mode read
        // this; the rest never asked for it and drop through their case.
        case FMRB_HID_EVENT_KANA_MODE:
            HID_SET_TYPE("kana_mode");
            HID_SET_INT("mode", ev.kana_mode);
            break;

        default:
            goto cleanup;
    }

#undef HID_SET_TYPE
#undef HID_SET_INT
#undef HID_SET

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

    // Guard: skip if VM context is invalid
    if (!mrb->c || !mrb->c->ci) {
        FMRB_LOGE(TAG, "mrb->c or mrb->c->ci is NULL, skip on_event");
        goto cleanup;
    }

    // Execute on_event directly.
    // Note: Object#method is not available in mruby (CRuby-only).
    // _dispatch_event runs the frame's handling (close button, reload) and
    // then the app's on_event hook, so apps no longer owe a super(ev).
    mrb_funcall(mrb, self, "_dispatch_event", 1, event_hash);

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

// Is something already queued for this app? Read without consuming it, so a
// GC step can tell whether it made a message wait.
static bool app_msg_waiting(fmrb_app_task_context_t* ctx)
{
    fmrb_msg_queue_stats_t stats;
    if (fmrb_msg_get_stats(ctx->app_id, &stats) != FMRB_OK) {
        return false;
    }
    return stats.current_waiting > 0;
}

// Spend the time an app is waiting in _spin on garbage collection.
//
// Only does anything when the app asked for it with GC.scheduler_driven =
// true (FmrbApp#idle_gc=): mrb_gc_scheduler_pending() is FALSE otherwise, so
// this costs one function call per spin for everyone else. In that mode the
// allocation path no longer collects, so this is where collection happens.
//
// Why it matters: with the allocation path driving, one collection runs to
// completion inside whatever call happened to allocate, which on the device
// stops the app for 100-205 ms and is audible as a stall in a playing song
// (doc/midi/report/p6.md 10). Stepping from here breaks the same work into
// pieces taken while the app had nothing to do.
//
// Three bounds keep the app responsive:
//  - a step is only started when the sleep left can absorb it, so the app is
//    never made later than it would have been without collecting at all;
//  - a margin of the sleep is left unspent;
//  - stepping stops as soon as a message is queued (that message waited for
//    the step, which is what mrb_gc_scheduler_jitter records).
#define GC_IDLE_SLACK_US 2000

static bool spin_idle_gc(mrb_state *mrb, fmrb_app_task_context_t* ctx,
                         fmrb_time_t deadline_us)
{
    bool stepped = false;

    if (ctx->app_id < 0 || ctx->app_id >= FMRB_MAX_APPS) {
        return false;
    }
    uint32_t* est = &s_gc_step_est_us[ctx->app_id];

    while (mrb_gc_scheduler_pending(mrb)) {
        fmrb_time_t now = fmrb_hal_time_get_us();
        if (now >= deadline_us) {
            break;
        }
        if ((deadline_us - now) < (fmrb_time_t)(*est) + GC_IDLE_SLACK_US) {
            break;
        }

        mrb_gc_step(mrb);
        uint32_t took = (uint32_t)(fmrb_hal_time_get_us() - now);
        if (took > *est) {
            *est = took;
        } else {
            *est -= *est / 8;
        }
        stepped = true;

        bool delayed = app_msg_waiting(ctx);
        mrb_gc_scheduler_jitter(mrb, delayed);
        if (delayed) {
            break; // deliver it now; the rest of the cycle can wait
        }
    }
    return stepped;
}

static mrb_value mrb_fmrb_app_spin(mrb_state *mrb, mrb_value self)
{

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }
    FMRB_LOGD(TAG, ">>>>>>>>> _spin(%s) START >>>>>>>>>>>>>",ctx->app_name);

    mrb_int timeout_ms;
    mrb_get_args(mrb, "i", &timeout_ms);

    // A callback dispatched below may ask to leave the wait early
    // (FmrbApp#request_early_update). Cleared here so only a request made
    // from inside THIS wait counts: one made from on_update is about to be
    // answered by the timeout this call was given.
    mrb_sym spin_break_sym = mrb_intern_lit(mrb, "@_spin_break");
    mrb_iv_set(mrb, self, spin_break_sym, mrb_false_value());

    // Record start time to ensure we wait for the full timeout period
    fmrb_tick_t start_tick = fmrb_task_get_tick_count();
    fmrb_tick_t target_tick = start_tick + FMRB_MS_TO_TICKS(timeout_ms);
    // The same deadline in microseconds, for the GC budget below: a tick is
    // too coarse to decide whether a step fits in what is left.
    fmrb_time_t deadline_us = fmrb_hal_time_get_us() + (fmrb_time_t)timeout_ms * 1000;

    // Spin Loop - process messages until timeout expires
    while(true){
        // Calculate remaining time
        fmrb_tick_t current_tick = fmrb_task_get_tick_count();
        if (current_tick >= target_tick) {
            // Timeout expired, exit spin loop
            break;
        }

        fmrb_tick_t remaining_ticks = target_tick - current_tick;

        // Collect while there is nothing else to do (no-op unless the app
        // turned on FmrbApp#idle_gc). Re-enter the loop afterwards: the clock
        // has moved and a message may have arrived.
        if (spin_idle_gc(mrb, ctx, deadline_us)) {
            continue;
        }

        // Try to receive message with remaining timeout.
        // fmrb_msg_receive takes milliseconds (it applies FMRB_MS_TO_TICKS
        // internally), so the tick deadline has to be converted back.
        fmrb_msg_t msg;
        fmrb_err_t ret = fmrb_msg_receive(ctx->app_id, &msg,
                                          FMRB_TICKS_TO_MS(remaining_ticks));

        if (ret == FMRB_OK) {
            // Dispatch message based on type
            if (msg.type == FMRB_MSG_TYPE_HID_EVENT) {
                bool bret = dispatch_hid_event_to_ruby(mrb, self, &msg);
                if(bret == false){
                    return mrb_nil_value();
                }
            } else if (msg.type == FMRB_MSG_TYPE_APP_CONTROL) {
                // Handle APP_CONTROL messages (like resize events)
                // Unpack msgpack data
                mrb_value data_str = mrb_str_new(mrb, (const char*)msg.data, msg.size);

                // Get MessagePack module and call unpack as module function
                struct RClass *msgpack_mod = mrb_module_get(mrb, "MessagePack");
                mrb_value data_hash = mrb_funcall(mrb, mrb_obj_value(msgpack_mod),
                                                  "unpack", 1, data_str);

                if (mrb_hash_p(data_hash)) {
                    // Check command type
                    mrb_value cmd_val = mrb_hash_get(mrb, data_hash, mrb_str_new_cstr(mrb, "cmd"));
                    if (mrb_string_p(cmd_val)) {
                        const char* cmd = mrb_str_to_cstr(mrb, cmd_val);

                        if (strcmp(cmd, "resize") == 0) {
                            // Handle resize: update instance variables first, then call callback
                            mrb_value width_val = mrb_hash_get(mrb, data_hash, mrb_str_new_cstr(mrb, "width"));
                            mrb_value height_val = mrb_hash_get(mrb, data_hash, mrb_str_new_cstr(mrb, "height"));

                            if (mrb_fixnum_p(width_val) && mrb_fixnum_p(height_val)) {
                                mrb_int new_width = mrb_fixnum(width_val);
                                mrb_int new_height = mrb_fixnum(height_val);

                                // A runtime window <-> fullscreen switch carries
                                // the new mode (fmrb_app_set_fullscreen). A plain
                                // resize (corner drag) omits it and is windowed.
                                mrb_value fs_val = mrb_hash_get(mrb, data_hash,
                                                                mrb_str_new_cstr(mrb, "fullscreen"));
                                int is_fullscreen = mrb_test(fs_val) ? 1 : 0;
                                if (!mrb_nil_p(fs_val)) {
                                    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@_fullscreen"),
                                               mrb_bool_value(is_fullscreen));
                                }

                                // Update Ruby instance variables
                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@window_width"), mrb_fixnum_value(new_width));
                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@window_height"), mrb_fixnum_value(new_height));

                                // Update user area dimensions. Fullscreen has no
                                // title bar and no border, so it owns the lot --
                                // same split as FmrbApp#_init.
                                // 11 = FmrbApp::TITLE_BAR_H
                                mrb_int user_area_x0 = is_fullscreen ? 0 : 1;
                                mrb_int user_area_y0 = is_fullscreen ? 0 : 11;
                                mrb_int user_area_width  = is_fullscreen ? new_width  : new_width - 2;
                                mrb_int user_area_height = is_fullscreen ? new_height : new_height - 12;
                                mrb_int user_area_x1 = is_fullscreen ? new_width  : new_width - 1;
                                mrb_int user_area_y1 = is_fullscreen ? new_height : new_height - 1;

                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@user_area_x0"), mrb_fixnum_value(user_area_x0));
                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@user_area_y0"), mrb_fixnum_value(user_area_y0));
                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@user_area_width"), mrb_fixnum_value(user_area_width));
                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@user_area_height"), mrb_fixnum_value(user_area_height));
                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@user_area_x1"), mrb_fixnum_value(user_area_x1));
                                mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@user_area_y1"), mrb_fixnum_value(user_area_y1));

                                // The backend drops the sprite clip on resize
                                // (it was sized for the old active area), so
                                // re-issue one for the new user area.
                                mrb_sym clip_sym = mrb_intern_lit(mrb, "_apply_user_area_sprite_clip");
                                if (mrb_respond_to(mrb, self, clip_sym)) {
                                    mrb_funcall(mrb, self, "_apply_user_area_sprite_clip", 0);
                                }

                                // Call on_resize callback if it exists
                                mrb_sym on_resize_sym = mrb_intern_lit(mrb, "on_resize");
                                if (mrb_respond_to(mrb, self, on_resize_sym)) {
                                    mrb_funcall(mrb, self, "on_resize", 2, width_val, height_val);
                                }
                            }
                        } else if (strcmp(cmd, "suspend") == 0 || strcmp(cmd, "resume") == 0 ||
                                   strcmp(cmd, "stop") == 0 || strcmp(cmd, "clear_and_stop") == 0 ||
                                   strcmp(cmd, "quit_request") == 0) {
                            // System suspend/resume: call _handle_system_control
                            mrb_sym sys_ctrl_sym = mrb_intern_lit(mrb, "_handle_system_control");
                            if (mrb_respond_to(mrb, self, sys_ctrl_sym)) {
                                mrb_funcall(mrb, self, "_handle_system_control", 1, data_hash);
                            }
                        } else {
                            // Other control commands: call on_control if exists
                            mrb_sym on_control_sym = mrb_intern_lit(mrb, "on_control");
                            if (mrb_respond_to(mrb, self, on_control_sym)) {
                                mrb_funcall(mrb, self, "on_control", 1, data_hash);
                            }
                        }
                    }
                }
            } else {
                FMRB_LOGI(TAG, "App %s message type %d not handled", ctx->app_name, msg.type);
            }

            // The callback learned the app has to act sooner than the timeout
            // it asked for, so stop waiting and let main_loop reach on_update.
            // Without it a deadline set from on_control (the service host's
            // wake_in) is not looked at until the current sleep runs out.
            if (mrb_test(mrb_iv_get(mrb, self, spin_break_sym))) {
                mrb_iv_set(mrb, self, spin_break_sym, mrb_false_value());
                break;
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

    // 案D: do NOT apply mrb_tick() here. Ticks accumulated by the signal source
    // during this C funcall are drained and applied by the scheduler loop
    // (mrb_task_run) -- the single, universal tick-application point for every
    // VM. Applying them here too would double-count tick_.

    FMRB_LOGD(TAG, "<<<<<<<<< _spin(%s) END <<<<<<<<<<<<<",ctx->app_name);
    return mrb_nil_value();
}

// FmrbApp#_mark_expected_stop() -> nil
// "I am ending because I meant to." Called from FmrbApp#stop, which is where
// every intended end goes through: the close button, Ctrl+Q, and a script that
// simply finished. An app that dies on an exception never reaches it, and that
// is the whole difference the kernel reads (fmrb_app.h expected_stop).
static mrb_value mrb_fmrb_app_mark_expected_stop(mrb_state *mrb, mrb_value self)
{
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (ctx) {
        ctx->expected_stop = true;
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

    // Releases the window, wallpaper and extra canvases and clears the context
    // fields, so the kernel's reap finds nothing left to delete.
    fmrb_app_canvas_release_all(ctx);

    // Delete message queue
    fmrb_err_t ret = fmrb_msg_delete_queue(ctx->app_id);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to delete message queue for app %s: %d",
                 ctx->app_name, ret);
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    // Release HW resources owned by this task (I2C buses, RMT channels, etc.)
    hw_proxy_release_resources((hw_proxy_task_handle_t)fmrb_task_get_current());
#endif

    return mrb_nil_value();
}

// C API: Cleanup VM resources (called from fmrb_app.c after VM execution finishes)
// This should be called AFTER the Ruby script has finished executing
void fmrb_app_vm_cleanup(mrb_state *mrb)
{
    if (!mrb) {
        return;
    }

    // Unregister VM from the tick manager.
    // (Renamed from hal_deinit -> mrb_hal_task_final: the tick manager moved to
    //  mruby-task's freertos HAL port as part of the case-D consolidation.)
    mrb_hal_task_final(mrb);
    FMRB_LOGI(TAG, "VM unregistered from HAL tick manager");
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

// FmrbApp#_create_canvas(w, h, z_offset, use_transparent, transparent_color) -> canvas_id
//
// Create an extra canvas owned by this app (e.g. a hardware-scrolled map
// layer used with FmrbGfx#set_viewport). The canvas is registered in the
// app context so kernel suspend/resume visibility control and the C-level
// cleanup on kill/crash cover it like the main canvas.
//
// Limitation: the window manager does not re-assign the z-order of extra
// canvases on focus changes; intended for fullscreen apps.
static mrb_value mrb_fmrb_app_create_canvas(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_int w, h, z_offset, use_transparent, transparent_color;
    mrb_get_args(mrb, "iiiii", &w, &h, &z_offset, &use_transparent, &transparent_color);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid canvas size");
    }

    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_err_t ret = fmrb_app_canvas_create_extra(
        ctx, (uint16_t)w, (uint16_t)h, (int16_t)z_offset, use_transparent != 0,
        (uint8_t)transparent_color, &canvas_id);
    if (ret == FMRB_ERR_NO_RESOURCE) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Extra canvas limit reached (%d)",
                   FMRB_APP_MAX_EXTRA_CANVAS);
    }
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to create canvas: %d", ret);
    }
    return mrb_fixnum_value(canvas_id);
}

// FmrbApp#_delete_canvas(canvas_id) -> nil
static mrb_value mrb_fmrb_app_delete_canvas(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_int canvas_id;
    mrb_get_args(mrb, "i", &canvas_id);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }

    if (fmrb_app_canvas_delete_extra(ctx, (fmrb_canvas_handle_t)canvas_id) != FMRB_OK) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Not an extra canvas of this app");
    }
    return mrb_nil_value();
}

// FmrbApp#_is_file_app -> true/false
static mrb_value mrb_fmrb_app_is_file_app(mrb_state *mrb, mrb_value self)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) return mrb_false_value();
    return mrb_bool_value(ctx->load_mode == FMRB_LOAD_MODE_FILE);
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


// FmrbApp#_send_audio_note(on, ch, freq, vol, duty, sweep) -> bool
// The note_on / note_off half of FmrbAudio, without the Hash.
// FmrbApp#_sync_file(src_path, dest_path) -> true/false
//
// The same transfer FmrbGfx#sync_file does, reachable without a canvas: a
// headless app (the service host) has no FmrbGfx at all, and play_wav has to
// put the file where the audio side can read it.
static mrb_value mrb_fmrb_app_sync_file(mrb_state *mrb, mrb_value self)
{
    const char *src;
    const char *dest;
    mrb_get_args(mrb, "zz", &src, &dest);

    return mrb_bool_value(fmrb_kernel_sync_file(src, dest) == FMRB_OK);
}

static mrb_value mrb_fmrb_app_send_audio_note(mrb_state *mrb, mrb_value self)
{
    mrb_bool on;
    mrb_int ch, freq, vol, duty, sweep;
    mrb_get_args(mrb, "biiiii", &on, &ch, &freq, &vol, &duty, &sweep);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context available");
    }

    int ret = fmrb_app_send_audio_note(ctx->app_id, on, ch, freq, vol,
                                       duty, sweep, 1000);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "App %s failed to send audio note: %d", ctx->app_name, ret);
        return mrb_false_value();
    }
    return mrb_true_value();
}

// FmrbApp#_audio_note_bytes(on, ch, freq, vol, duty, sweep) -> String
// The same message as a String, so a test can compare it against
// MessagePack.pack of the equivalent Hash. Not used in normal operation.
static mrb_value mrb_fmrb_app_audio_note_bytes(mrb_state *mrb, mrb_value self)
{
    mrb_bool on;
    mrb_int ch, freq, vol, duty, sweep;
    uint8_t buf[FMRB_APP_AUDIO_NOTE_MSG_MAX];
    mrb_get_args(mrb, "biiiii", &on, &ch, &freq, &vol, &duty, &sweep);

    size_t n = fmrb_app_build_audio_note_msg(buf, on, (int32_t)ch, (int32_t)freq,
                                             (int32_t)vol, (int32_t)duty,
                                             (int32_t)sweep);
    return mrb_str_new(mrb, (const char *)buf, n);
}

// FmrbApp.pid -> Integer (nil outside an app context)
// The caller's own process id, the same number ps reports as :id. An app that
// acts on the process list needs it to leave itself out (the shell's and the
// monitor's kill).
static mrb_value mrb_fmrb_app_s_pid(mrb_state *mrb, mrb_value self)
{
    (void)mrb; (void)self;
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        return mrb_nil_value();
    }
    return mrb_fixnum_value((mrb_int)ctx->app_id);
}

// FmrbApp.ps() -> Array[Hash]
// Get process list with memory statistics
// FmrbApp.ps_gen -> Integer. Process-set generation: bumped by the kernel
// on every state transition. A 1Hz UI poll compares it to its last value
// and calls the allocating FmrbApp.ps only when something actually changed.
static mrb_value mrb_fmrb_app_s_ps_gen(mrb_state *mrb, mrb_value self)
{
    (void)mrb; (void)self;
    return mrb_fixnum_value((mrb_int)fmrb_app_proc_generation());
}

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
        // No window and no canvas: the taskbar is a list of windows and skips
        // these, ps and the monitor list processes and keep them.
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "headless")),
                     mrb_bool_value(list[i].headless));

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

// FmrbApp._get_last_error -> Hash {name:, error:} or nil
// Read last error from shared static buffer (set by crashed app)
static mrb_value mrb_fmrb_app_s_get_last_error(mrb_state *mrb, mrb_value self)
{
    const char *name = fmrb_app_get_last_error_name();
    const char *msg = fmrb_app_get_last_error_msg();

    if (name[0] == '\0') {
        return mrb_nil_value();
    }

    mrb_value hash = mrb_hash_new_capa(mrb, 2);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "name")),
                 mrb_str_new_cstr(mrb, name));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "error")),
                 mrb_str_new_cstr(mrb, msg));
    return hash;
}

// FmrbApp.config(section_name) -> Array of Hash, or nil
// Read a section from system_conf.toml
// [[array-of-tables]] and [table] both return Array of Hash for consistency.
static mrb_value mrb_fmrb_app_s_config(mrb_state *mrb, mrb_value self)
{
    const char *section;
    mrb_get_args(mrb, "z", &section);

    #define CONFIG_MAX_TABLES 16
    // sizeof(fmrb_config_table_t) is ~2.5KB, so the full array is ~40KB. That
    // must NOT live on the stack: this runs in the system_desktop task whose
    // stack is only 12KB. The oversized frame overflowed into adjacent memory
    // and crashed once enough sections were written deep into the array
    // (e.g. a 5th [[shortcuts]] entry). Allocate on the heap instead.
    fmrb_config_table_t *tables =
        (fmrb_config_table_t *)mrb_malloc(mrb, sizeof(fmrb_config_table_t) * CONFIG_MAX_TABLES);

    int table_count = fmrb_kernel_get_config_section(section, tables, CONFIG_MAX_TABLES);

    if (table_count <= 0) {
        mrb_free(mrb, tables);
        return mrb_nil_value();
    }

    mrb_value result = mrb_ary_new_capa(mrb, table_count);
    for (int t = 0; t < table_count; t++) {
        mrb_value hash = mrb_hash_new_capa(mrb, tables[t].count);
        for (int k = 0; k < tables[t].count; k++) {
            mrb_hash_set(mrb, hash,
                         mrb_str_new_cstr(mrb, tables[t].kv[k].key),
                         mrb_str_new_cstr(mrb, tables[t].kv[k].value));
        }
        mrb_ary_push(mrb, result, hash);
    }

    mrb_free(mrb, tables);
    return result;
}

// FmrbApp.script_path -> String: the file this app was started from ("" for
// an app built into the firmware). FmrbApp#app_handle_name starts from it.
static mrb_value mrb_fmrb_app_s_script_path(mrb_state *mrb, mrb_value self)
{
    (void)self;
    fmrb_app_task_context_t *ctx = fmrb_current();
    return mrb_str_new_cstr(mrb, ctx ? ctx->filepath : "");
}

// FmrbApp.uptime_us() -> Integer
// Microseconds since boot, for profiling from Ruby. Log timestamps only have
// millisecond resolution and each log line blocks the console UART for several
// milliseconds, which is too coarse and too intrusive to time short sections.
static mrb_value mrb_fmrb_app_s_uptime_us(mrb_state *mrb, mrb_value self)
{
    (void)self;
    return mrb_int_value(mrb, (mrb_int)fmrb_hal_time_get_us());
}

// FmrbApp.wallclock() -> Hash {year:, month:, day:, hour:, minute:, second:} or nil
static mrb_value mrb_fmrb_app_s_wallclock(mrb_state *mrb, mrb_value self)
{
    fmrb_wallclock_t wc;
    if (fmrb_hal_time_get_wallclock(&wc) != FMRB_OK) {
        return mrb_nil_value();
    }

    mrb_value hash = mrb_hash_new_capa(mrb, 6);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "year")),
                 mrb_fixnum_value(wc.year));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "month")),
                 mrb_fixnum_value(wc.month));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "day")),
                 mrb_fixnum_value(wc.day));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "hour")),
                 mrb_fixnum_value(wc.hour));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "minute")),
                 mrb_fixnum_value(wc.minute));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "second")),
                 mrb_fixnum_value(wc.second));
    return hash;
}

// FmrbApp.set_wallclock(year, month, day, hour, minute, second)
//   -> Hash {year:, month:, day:, hour:, minute:, second:}
//
// Interprets the six arguments as LOCAL time fields, converts to a UTC
// epoch via mktime() (which respects the TZ env set by fmrb_kernel),
// and updates the system clock with clock_settime().
//
// The returned hash holds the UTC equivalent of the same instant, so
// the caller can write it to a UTC-storing RTC (RX8900) without any
// further timezone math. This keeps the on-screen display, the system
// clock, and the RTC self-consistent: RTC always stores UTC fields,
// the boot path (rx8900.rb sync_system_clock) reads them as UTC, and
// localtime_r() converts to local for display.
static mrb_value mrb_fmrb_app_s_set_wallclock(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_int year, month, day, hour, minute, second;
    mrb_get_args(mrb, "iiiiii", &year, &month, &day, &hour, &minute, &second);

    struct tm local_tm = {0};
    local_tm.tm_year  = (int)year - 1900;
    local_tm.tm_mon   = (int)month - 1;
    local_tm.tm_mday  = (int)day;
    local_tm.tm_hour  = (int)hour;
    local_tm.tm_min   = (int)minute;
    local_tm.tm_sec   = (int)second;
    local_tm.tm_isdst = -1;  // Let mktime decide DST from TZ rules

    time_t epoch = mktime(&local_tm);
    if (epoch == (time_t)-1) {
        return mrb_nil_value();
    }

    struct timespec ts = { .tv_sec = epoch, .tv_nsec = 0 };
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        return mrb_nil_value();
    }

    struct tm utc_tm;
    gmtime_r(&epoch, &utc_tm);

    mrb_value hash = mrb_hash_new_capa(mrb, 6);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "year")),
                 mrb_fixnum_value(utc_tm.tm_year + 1900));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "month")),
                 mrb_fixnum_value(utc_tm.tm_mon + 1));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "day")),
                 mrb_fixnum_value(utc_tm.tm_mday));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "hour")),
                 mrb_fixnum_value(utc_tm.tm_hour));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "minute")),
                 mrb_fixnum_value(utc_tm.tm_min));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "second")),
                 mrb_fixnum_value(utc_tm.tm_sec));
    return hash;
}

// FmrbApp.gfx_stats() -> Hash { cmds: uint32, presents: uint32 }
// Returns cumulative counters from the Host Task. Counters wrap modulo 2^32;
// callers should compute rate from deltas between successive samples.
static mrb_value mrb_fmrb_app_s_gfx_stats(mrb_state *mrb, mrb_value self)
{
    (void)self;
    uint32_t cmds = 0, presents = 0;
    fmrb_host_get_gfx_counters(&cmds, &presents);

    mrb_value hash = mrb_hash_new_capa(mrb, 2);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "cmds")),
                 mrb_fixnum_value((mrb_int)cmds));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "presents")),
                 mrb_fixnum_value((mrb_int)presents));
    return hash;
}

// FmrbApp.sys_pool_info() -> Hash
// Get system pool (fmrb_sys_malloc) information (TLSF allocator)
static mrb_value mrb_fmrb_app_s_sys_pool_info(mrb_state *mrb, mrb_value self)
{
    fmrb_pool_stats_t stats;
    mrb_value hash = mrb_hash_new_capa(mrb, 5);

    int ret = fmrb_sys_mem_get_stats(&stats);
    static int log_count = 0;
    if (log_count < 3) {
        FMRB_LOGI(TAG, "sys_pool_info: ret=%d, total=%zu, used=%zu, free=%zu",
                  ret, stats.total_size, stats.used_size, stats.free_size);
        log_count++;
    }
    if (ret == 0) {
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
// Defined in picoruby-mruby's alloc.c (same extern pattern as fmrb_app.c).
extern int mrb_get_estalloc_stats(void* est_ptr, size_t* total, size_t* used, size_t* free_out, int32_t* frag);

// FmrbApp.pool_usage -> Integer 0-100 (percent of this VM's estalloc pool
// in use), or -1 when unavailable. Allocation-free (a single fixnum), so
// the desktop can poll it for its GC watermark without adding to the very
// garbage it is trying to bound.
static mrb_value mrb_fmrb_app_s_pool_usage(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (ctx == NULL || ctx->est == NULL) {
        return mrb_fixnum_value(-1);
    }
    size_t total = 0, used = 0, free_bytes = 0;
    int32_t frag = 0;
    if (mrb_get_estalloc_stats(ctx->est, &total, &used, &free_bytes, &frag) != 0 || total == 0) {
        return mrb_fixnum_value(-1);
    }
    return mrb_fixnum_value((mrb_int)((used * 100) / total));
}

// FmrbApp.pool_used -> Integer, bytes of this VM's estalloc pool in use, or
// -1 when unavailable. pool_usage's percent is too coarse to see where a
// burst of garbage comes from: on the desktop's 1536 KB pool one percent is
// 15 KB, and a rescan that produced ~280 KB of garbage had to be attributed
// across a dozen phases. Same cost as pool_usage (one fixnum, no allocation).
static mrb_value mrb_fmrb_app_s_pool_used(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (ctx == NULL || ctx->est == NULL) {
        return mrb_fixnum_value(-1);
    }
    size_t total = 0, used = 0, free_bytes = 0;
    int32_t frag = 0;
    if (mrb_get_estalloc_stats(ctx->est, &total, &used, &free_bytes, &frag) != 0) {
        return mrb_fixnum_value(-1);
    }
    return mrb_fixnum_value((mrb_int)used);
}

static mrb_value mrb_fmrb_app_s_heap_info(mrb_state *mrb, mrb_value self)
{
    mrb_value hash = mrb_hash_new_capa(mrb, 4);

#ifndef CONFIG_IDF_TARGET_LINUX
    // ESP32: Use ESP-IDF heap API
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    // IRAM: internal SRAM free / total (matches fmrb_task status log).
    size_t iram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t iram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free")),
                 mrb_fixnum_value(free_heap));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "total")),
                 mrb_fixnum_value(total_heap));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "min_free")),
                 mrb_fixnum_value(min_free_heap));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "largest_block")),
                 mrb_fixnum_value(largest_free_block));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "iram_free")),
                 mrb_fixnum_value(iram_free));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "iram_total")),
                 mrb_fixnum_value(iram_total));
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
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "iram_free")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "iram_total")),
                     mrb_fixnum_value(0));
    } else {
        // If sysinfo fails, return zeros
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "free")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "total")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "iram_free")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "iram_total")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "min_free")),
                     mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "largest_block")),
                     mrb_fixnum_value(0));
    }
#endif

    return hash;
}

// FmrbApp.reboot -> never returns (on the native program: the program ends
// shortly after)
// Restarts the system. On ESP32 esp_restart() (no power-cycle reset reason
// on next boot). In the simulation the host process exits so the SDL harness
// comes back via the launcher script. The dropdown only exposes the menu
// entry on ESP32 (FmrbConst::PLATFORM == "esp32"), but the API itself is
// safe to call on any platform.
static mrb_value mrb_fmrb_app_s_reboot(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    FMRB_LOGI(TAG, "Reboot requested by user");
    fmrb_host_audio_flush_save();  // a mute / volume change still waiting to be saved
    fmrb_task_delay_ms(100);
    // The OS services' reset (fmrb_platform_sys.h): esp_restart on a device,
    // the process ends in the simulation, the native program ends.
    fmrb_platform_sys_power(FMRB_SYS_POWER_RESET);
    return mrb_nil_value();
}

// FmrbApp.power_actions -> Array of Symbols: what the desktop's power menu
// offers on this machine (fmrb_platform_sys.h). [:reset] on a device, [] in
// the simulation and the browser, [:exit] or [:power_off, :restart,
// :suspend] on the native program by its machine definition, and :lock on
// the PC's compositor with a lock program (L13).
static const struct {
    uint32_t bit;
    const char *name;
} s_power_names[] = {
    { FMRB_SYS_POWER_RESET, "reset" },
    { FMRB_SYS_POWER_EXIT, "exit" },
    { FMRB_SYS_POWER_OFF, "power_off" },
    { FMRB_SYS_POWER_RESTART, "restart" },
    { FMRB_SYS_POWER_SUSPEND, "suspend" },
    { FMRB_SYS_POWER_LOCK, "lock" },
};
#define POWER_NAMES_N (sizeof(s_power_names) / sizeof(s_power_names[0]))

static mrb_value mrb_fmrb_app_s_power_actions(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    uint32_t bits = fmrb_platform_sys_power_actions();
    mrb_value ary = mrb_ary_new(mrb);
    for (size_t i = 0; i < POWER_NAMES_N; i++) {
        if (bits & s_power_names[i].bit) {
            mrb_ary_push(mrb, ary, mrb_symbol_value(mrb_intern_cstr(mrb, s_power_names[i].name)));
        }
    }
    return ary;
}

// FmrbApp.power(action) -> false when it did not happen. action is one of
// power_actions' symbols. :exit ends the program (it returns true and the
// program stops shortly after); the others go to the OS.
static mrb_value mrb_fmrb_app_s_power(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_sym sym;
    mrb_get_args(mrb, "n", &sym);
    const char *name = mrb_sym_name(mrb, sym);
    for (size_t i = 0; i < POWER_NAMES_N; i++) {
        if (strcmp(name, s_power_names[i].name) != 0) continue;
        if (!(fmrb_platform_sys_power_actions() & s_power_names[i].bit)) break;
        FMRB_LOGI(TAG, "power: %s requested", name);
        fmrb_host_audio_flush_save();
        return mrb_bool_value(fmrb_platform_sys_power((fmrb_sys_power_t)s_power_names[i].bit) == FMRB_OK);
    }
    FMRB_LOGW(TAG, "power: %s is not offered here", name);
    return mrb_false_value();
}

// FmrbApp.battery_level -> Integer 0-100, or nil without a battery value.
// FmrbApp.battery_charging? -> bool. Allocation free, for the 1 Hz status.
static mrb_value mrb_fmrb_app_s_battery_level(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    fmrb_sys_battery_t b;
    if (!fmrb_platform_sys_battery(&b)) return mrb_nil_value();
    return mrb_fixnum_value(b.percent);
}

static mrb_value mrb_fmrb_app_s_battery_charging_p(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    fmrb_sys_battery_t b;
    return mrb_bool_value(fmrb_platform_sys_battery(&b) && b.charging);
}

// ---- Linux programs (doc/linux_native L10, fmrb_platform_sys.h) ----
//
// FmrbApp.linux_available? -> bool. false on the devices, the simulation and
//   the browser; the rest below then raise or answer "ended".
// FmrbApp.linux_start(command, cwd, cols, rows) -> Integer handle. command
//   nil starts the interactive shell. cwd is a Family mruby path. Raises
//   RuntimeError with the reason when it cannot start.
// FmrbApp.linux_read(h) -> String of what it printed ("" nothing just now),
//   nil when no more output will come.
// FmrbApp.linux_write(h, str) -> Integer bytes taken (-1: it takes no more).
// FmrbApp.linux_signal(h, sym) -> bool. :int, :term, :kill, :hup.
// FmrbApp.linux_exit_code(h) -> nil while it runs, else Integer.
// FmrbApp.linux_resize(h, cols, rows) -> bool.
// FmrbApp.linux_close(h) -> nil. Ends it if it still runs (no wait).
//
// Each program belongs to the calling app's task and ends with it, however
// the app ends. None of these waits.
#define LINUX_READ_CHUNK 4096

static mrb_value mrb_fmrb_app_s_linux_available_p(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_bool_value(fmrb_platform_sys_proc_available());
}

static mrb_value mrb_fmrb_app_s_linux_start(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_value cmd;
    const char *cwd;
    mrb_int cols, rows;
    mrb_get_args(mrb, "ozii", &cmd, &cwd, &cols, &rows);
    if (!fmrb_platform_sys_proc_available()) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Linux commands are not available on this machine");
    }
    if (cols < 1) cols = 1;
    if (cols > 1000) cols = 1000;
    if (rows < 1) rows = 1;
    if (rows > 1000) rows = 1000;
    fmrb_sys_proc_spec_t spec = {
        .command = mrb_nil_p(cmd) ? NULL : mrb_str_to_cstr(mrb, cmd),
        .cwd = cwd,
        .cols = (uint16_t)cols,
        .rows = (uint16_t)rows,
    };
    char why[160] = "";
    int32_t id = 0;
    fmrb_err_t err = fmrb_platform_sys_proc_start(&spec, &id, why, sizeof(why));
    if (err != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "%s", why[0] ? why : "cannot start");
    }
    return mrb_fixnum_value(id);
}

static mrb_value mrb_fmrb_app_s_linux_read(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    if (!fmrb_platform_sys_proc_available()) return mrb_nil_value();
    mrb_value str = mrb_str_new_capa(mrb, LINUX_READ_CHUNK);
    int32_t n = fmrb_platform_sys_proc_read((int32_t)id, RSTRING_PTR(str), LINUX_READ_CHUNK);
    if (n < 0) return mrb_nil_value();
    mrb_str_resize(mrb, str, n);
    return str;
}

static mrb_value mrb_fmrb_app_s_linux_write(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int id;
    mrb_value str;
    mrb_get_args(mrb, "iS", &id, &str);
    int32_t n = fmrb_platform_sys_proc_write((int32_t)id, RSTRING_PTR(str), (size_t)RSTRING_LEN(str));
    return mrb_fixnum_value(n);
}

static mrb_value mrb_fmrb_app_s_linux_signal(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int id;
    mrb_sym sym;
    mrb_get_args(mrb, "in", &id, &sym);
    const char *name = mrb_sym_name(mrb, sym);
    fmrb_sys_signal_t sig;
    if (strcmp(name, "int") == 0) sig = FMRB_SYS_SIG_INT;
    else if (strcmp(name, "term") == 0) sig = FMRB_SYS_SIG_TERM;
    else if (strcmp(name, "kill") == 0) sig = FMRB_SYS_SIG_KILL;
    else if (strcmp(name, "hup") == 0) sig = FMRB_SYS_SIG_HUP;
    else return mrb_false_value();
    return mrb_bool_value(fmrb_platform_sys_proc_signal((int32_t)id, sig) == FMRB_OK);
}

static mrb_value mrb_fmrb_app_s_linux_exit_code(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    int code = 0;
    if (!fmrb_platform_sys_proc_exited((int32_t)id, &code)) return mrb_nil_value();
    return mrb_fixnum_value(code);
}

static mrb_value mrb_fmrb_app_s_linux_resize(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int id, cols, rows;
    mrb_get_args(mrb, "iii", &id, &cols, &rows);
    if (cols < 1 || rows < 1 || cols > 1000 || rows > 1000) return mrb_false_value();
    return mrb_bool_value(fmrb_platform_sys_proc_resize((int32_t)id, (uint16_t)cols,
                                                        (uint16_t)rows) == FMRB_OK);
}

static mrb_value mrb_fmrb_app_s_linux_close(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    fmrb_platform_sys_proc_close((int32_t)id);
    return mrb_nil_value();
}

// ---- The Linux PC's applications (doc/linux_native L9b, fmrb_platform_sys.h) ----
//
// FmrbApp.linux_apps(mode, lang) -> String. "" on every machine without them
//   (the devices, the simulation, the browser, a native machine that is not
//   the compositor). Else the launcher's list as text: a header line
//   ("ready\t<group icon>" or "loading\t<group icon>"), then for mode 0 one
//   "name\tid\ticon" line per entry. mode 1 reads the entries again, mode 2
//   gives the header only. lang picks the names (Name[ja] for "ja").
// FmrbApp.linux_launch(id) -> Integer. 0 asked to start, -1 no such entry or
//   not available. Never waits.
static mrb_value mrb_fmrb_app_s_linux_apps(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int mode;
    const char *lang;
    mrb_get_args(mrb, "iz", &mode, &lang);
    int32_t need = fmrb_platform_sys_apps_list((int)mode, lang, NULL, 0);
    if (need < 0) return mrb_str_new_lit(mrb, "");
    // The list may grow between the two calls: ask again with the new size.
    for (int tries = 0; tries < 4; tries++) {
        mrb_value str = mrb_str_new_capa(mrb, (size_t)need + 1);
        int32_t got = fmrb_platform_sys_apps_list(mode == 1 ? 2 : (int)mode, lang, RSTRING_PTR(str),
                                                  (size_t)need + 1);
        if (got < 0) return mrb_str_new_lit(mrb, "");
        if (got <= need + 1) {
            mrb_str_resize(mrb, str, got);
            return str;
        }
        need = got;
    }
    return mrb_str_new_lit(mrb, "");
}

static mrb_value mrb_fmrb_app_s_linux_launch(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *id;
    mrb_get_args(mrb, "z", &id);
    return mrb_fixnum_value(fmrb_platform_sys_apps_launch(id) == FMRB_OK ? 0 : -1);
}

// ---- Opening a file in a Linux application (doc/linux_native L11) ----
//
// FmrbApp.linux_handlers(path) -> String. "" on every machine without Linux
//   applications, and for a file with no place on Linux. Else
//   "type\t<MIME type, or empty>\n", then "name\tid\n" for each application
//   that opens it, the default first. path is a Family mruby path.
// FmrbApp.linux_open(id, path) -> String. The application's name when it was
//   asked to start (id "" is the default for the file's type), "" when not
//   (the reason is in the log). Never waits for the application.
static mrb_value mrb_fmrb_app_s_linux_handlers(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *path;
    mrb_get_args(mrb, "z", &path);
    size_t cap = 2048;
    for (int tries = 0; tries < 3; tries++) {
        mrb_value str = mrb_str_new_capa(mrb, cap);
        int32_t got = fmrb_platform_sys_apps_handlers(path, RSTRING_PTR(str), cap);
        if (got < 0) return mrb_str_new_lit(mrb, "");
        if ((size_t)got <= cap) {
            mrb_str_resize(mrb, str, got);
            return str;
        }
        cap = (size_t)got + 256;
    }
    return mrb_str_new_lit(mrb, "");
}

static mrb_value mrb_fmrb_app_s_linux_open(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *id;
    const char *path;
    mrb_get_args(mrb, "zz", &id, &path);
    char name[128];
    name[0] = '\0';
    if (fmrb_platform_sys_apps_open(id, path, name, sizeof(name), NULL, 0) != FMRB_OK) {
        return mrb_str_new_lit(mrb, "");
    }
    return name[0] ? mrb_str_new_cstr(mrb, name) : mrb_str_new_lit(mrb, "?");
}

// ---- The machine's clipboard (doc/linux_native L12a, fmrb_clipboard.h) ----
//
// FmrbApp.clipboard(limit = 0) -> String. The text every app shares ("" when
//   empty); with a limit > 0, at most that many bytes, cut before a character
//   that would not fit whole. On the Linux PC and in the browser it is what
//   was last copied there or here.
// FmrbApp.set_clipboard(str) -> true, or false when str is over the maximum
//   (64 KB; the clipboard is then unchanged).
// FmrbApp.clipboard_gen -> Integer: how many times the text has changed, to
//   tell a change without reading the text.
/* The first n bytes of b without a last character that is not all there. */
static size_t clip_utf8_whole(const unsigned char *b, size_t n)
{
    size_t i = n;
    for (int k = 0; i > 0 && k < 4; k++) {
        i--;
        if ((b[i] & 0xC0) != 0x80) break;
    }
    if (i >= n) return n;
    unsigned char c = b[i];
    size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    return (n - i >= need) ? n : i;
}

static mrb_value mrb_fmrb_app_s_clipboard(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int limit = 0;
    mrb_get_args(mrb, "|i", &limit);
    for (int tries = 0; tries < 4; tries++) {
        uint32_t gen0 = fmrb_clipboard_generation();
        size_t len = fmrb_clipboard_length();
        size_t want = (limit > 0 && (size_t)limit < len) ? (size_t)limit : len;
        if (want == 0) return mrb_str_new_lit(mrb, "");
        mrb_value str = mrb_str_new_capa(mrb, want);
        uint32_t gen1 = 0;
        size_t got = fmrb_clipboard_read(0, RSTRING_PTR(str), want, &gen1);
        if (gen1 != gen0 || got != want) continue;   /* changed meanwhile */
        if (want < len) got = clip_utf8_whole((const unsigned char *)RSTRING_PTR(str), got);
        mrb_str_resize(mrb, str, (mrb_int)got);
        return str;
    }
    return mrb_str_new_lit(mrb, "");
}

static mrb_value mrb_fmrb_app_s_set_clipboard(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_value str;
    mrb_get_args(mrb, "S", &str);
    return mrb_bool_value(fmrb_clipboard_set(RSTRING_PTR(str), (size_t)RSTRING_LEN(str)) == FMRB_OK);
}

static mrb_value mrb_fmrb_app_s_clipboard_gen(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_fixnum_value((mrb_int)fmrb_clipboard_generation());
}

// ---- The PC's settings and notices (doc/linux_native L14, fmrb_platform_sys.h) ----
//
// Every device, the simulation and the browser answer "not available":
// "" for the texts, false, -1 for notices_gen.
//
// FmrbApp.wifi_networks(scan = false) -> String. The Wi-Fi state and the
//   networks in reach: "wifi\t<status>\t<active>\t<busy>\t<code>\t<arg>"
//   then "ap\t<ssid>\t<strength>\t<secured>\t<active>\t<saved>" per
//   network (native/sys/linux_net.h). scan true asks for a scan too.
// FmrbApp.wifi_connect(ssid, password = "") -> bool (queued). The password
//   goes to the OS (NetworkManager) and is not kept here.
// FmrbApp.wifi_disconnect -> bool.
// FmrbApp.bt_devices -> String. "bt\t<status>\t<powered>\t<busy>\t<code>\t<arg>"
//   then "dev\t<address>\t<name>\t<connected>\t<icon>" per known device.
// FmrbApp.bt_power(on) -> bool. FmrbApp.bt_connect(address, on) -> bool.
// FmrbApp.notices_gen -> Integer, changes when the notices do (-1: none
//   are shown on this machine). Allocation free, for every update.
// FmrbApp.notices -> String, newest first: "id\tkind\tapp\tsummary\tbody"
//   per line (a line break in the body is 0x1F).
// FmrbApp.notice_dismiss(id) -> nil.
// FmrbApp.notify(summary, body = "") -> bool. A notice from the calling app
//   (its name above the text), for a few seconds; false where there are
//   none (only the PC shows them).
typedef int32_t (*sys_text_fn)(const void *ud, char *buf, size_t cap);

static mrb_value sys_text(mrb_state *mrb, sys_text_fn fn, const void *ud)
{
    int32_t need = fn(ud, NULL, 0);
    if (need < 0) return mrb_str_new_lit(mrb, "");
    for (int tries = 0; tries < 4; tries++) {
        mrb_value str = mrb_str_new_capa(mrb, (size_t)need + 1);
        int32_t got = fn(ud, RSTRING_PTR(str), (size_t)need + 1);
        if (got < 0) return mrb_str_new_lit(mrb, "");
        if (got <= need) {
            mrb_str_resize(mrb, str, got);
            return str;
        }
        need = got + 256;
    }
    return mrb_str_new_lit(mrb, "");
}

static int32_t wifi_text(const void *ud, char *buf, size_t cap)
{
    bool scan = ud != NULL && buf == NULL;   /* ask for the scan once, on the measuring call */
    return fmrb_platform_sys_wifi_list(scan, buf, cap);
}

static int32_t bt_text(const void *ud, char *buf, size_t cap)
{
    (void)ud;
    return fmrb_platform_sys_bt_list(buf, cap);
}

static int32_t notice_text(const void *ud, char *buf, size_t cap)
{
    (void)ud;
    return fmrb_platform_sys_notice_list(buf, cap);
}

static mrb_value mrb_fmrb_app_s_wifi_networks(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_bool scan = false;
    mrb_get_args(mrb, "|b", &scan);
    return sys_text(mrb, wifi_text, scan ? (const void *)1 : NULL);
}

static mrb_value mrb_fmrb_app_s_wifi_connect(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *ssid;
    mrb_value pw = mrb_nil_value();
    mrb_get_args(mrb, "z|S", &ssid, &pw);
    const char *p = "";
    if (mrb_string_p(pw)) p = mrb_str_to_cstr(mrb, pw);
    return mrb_bool_value(fmrb_platform_sys_wifi_connect(ssid, p) == FMRB_OK);
}

static mrb_value mrb_fmrb_app_s_wifi_disconnect(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_bool_value(fmrb_platform_sys_wifi_disconnect() == FMRB_OK);
}

static mrb_value mrb_fmrb_app_s_bt_devices(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    return sys_text(mrb, bt_text, NULL);
}

static mrb_value mrb_fmrb_app_s_bt_power(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_bool on;
    mrb_get_args(mrb, "b", &on);
    return mrb_bool_value(fmrb_platform_sys_bt_power(on) == FMRB_OK);
}

static mrb_value mrb_fmrb_app_s_bt_connect(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *addr;
    mrb_bool on;
    mrb_get_args(mrb, "zb", &addr, &on);
    return mrb_bool_value(fmrb_platform_sys_bt_connect(addr, on) == FMRB_OK);
}

static mrb_value mrb_fmrb_app_s_notices_gen(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_fixnum_value(fmrb_platform_sys_notice_gen());
}

static mrb_value mrb_fmrb_app_s_notices(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    return sys_text(mrb, notice_text, NULL);
}

static mrb_value mrb_fmrb_app_s_notice_dismiss(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    if (id > 0) fmrb_platform_sys_notice_dismiss((uint32_t)id);
    return mrb_nil_value();
}

static mrb_value mrb_fmrb_app_s_notify(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *summary;
    const char *body = "";
    mrb_get_args(mrb, "z|z", &summary, &body);
    fmrb_app_task_context_t *ctx = fmrb_current();
    const char *app = ctx ? ctx->app_name : "";
    return mrb_bool_value(fmrb_platform_sys_notice_post(app, summary, body, -1) == FMRB_OK);
}

#if defined(FMRB_PLATFORM_NATIVE)
// ---- Linux windows (doc/linux_native L9a, native/wlr/wl_bridge.h) ----
//
// For the stand-in app the compositor's bridge starts for each Linux window
// (main/prebuild_scripts/kernel/linux_window.app.rb). The native program only:
// no other build defines these methods.
//
// FmrbApp.wl_window -> String (the window's title) while the Linux window is
//   there, false once it has gone, nil when the caller is no stand-in (yet).
// FmrbApp.wl_close -> bool. Ask the Linux window to close (the program may ask
//   about unsaved work first, or refuse).
// FmrbApp.wl_key -> Integer: the colour key of the caller's canvas (RGB332),
//   which the stand-in paints outside its rounded corners (L9d).
extern int native_wl_window_state(int pid, char *title, size_t cap);
extern bool native_wl_window_close(int pid);
extern uint8_t native_wl_canvas_key(const fmrb_app_task_context_t *ctx, uint8_t key);

static mrb_value mrb_fmrb_app_s_wl_window(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) return mrb_nil_value();
    char title[64];
    int st = native_wl_window_state((int)ctx->app_id, title, sizeof(title));
    if (st < 0) return mrb_nil_value();
    if (st == 0) return mrb_false_value();
    return mrb_str_new_cstr(mrb, title);
}

static mrb_value mrb_fmrb_app_s_wl_close(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) return mrb_false_value();
    return mrb_bool_value(native_wl_window_close((int)ctx->app_id));
}

// FmrbApp.linux_keyboard -> "jp" or "us": the layout Linux is set to, what
//   keyboard_layout = "auto" follows (L9d). Native only, like the calls above.
extern bool native_linux_keyboard_jp(char *how, size_t how_len);

static mrb_value mrb_fmrb_app_s_linux_keyboard(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    return mrb_str_new_cstr(mrb, native_linux_keyboard_jp(NULL, 0) ? "jp" : "us");
}

// FmrbApp.linux_settings(page) -> bool: open the PC's settings window (L14,
//   native/backend/native_settings.c) on the "wifi" or "bluetooth" page, or
//   bring it in front there. FmrbApp.settings_page -> Integer: the page
//   asked for last in the low bit (0 Wi-Fi, 1 Bluetooth), the count of the
//   asks above it (the window turns to it when it changes). Native only.
extern bool native_settings_open(const char *page);
extern int native_settings_page(void);

static mrb_value mrb_fmrb_app_s_linux_settings(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *page;
    mrb_get_args(mrb, "z", &page);
    return mrb_bool_value(native_settings_open(page));
}

static mrb_value mrb_fmrb_app_s_settings_page(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_fixnum_value(native_settings_page());
}

static mrb_value mrb_fmrb_app_s_wl_key(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) return mrb_fixnum_value(0x01);
    return mrb_fixnum_value(native_wl_canvas_key(ctx, 0x01));
}
#endif

// FmrbApp.ble_start -> true/false
// Manual BLE start for the ble_auto_start=false configuration (desktop menu).
// Retro (built-in radio) only: Modern's C6 radio path manages itself, and
// Linux has no BLE -- both return false. Idempotent: ble_service_start is a
// logged no-op when BLE is already up or starting.
#if !defined(CONFIG_IDF_TARGET_LINUX) && !defined(CONFIG_IDF_TARGET_ESP32P4)
extern int ble_service_start(void);   // main/drivers/ble/ble_task.h (fmrb_err_t; 0 == OK)
#endif

// FmrbApp.ble_state -> Integer: 0 = BLE off, 1 = enabled and waiting for a
// central, 2 = connected. Allocation-free fixnum; feeds the menu-bar
// indicator. The Linux sim has no BLE and always reports 0 (hidden).
#if !defined(CONFIG_IDF_TARGET_LINUX)
extern int ble_ui_state(void);        // main/drivers/ble/ble_task.h
#endif
static mrb_value mrb_fmrb_app_s_ble_state(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
#if !defined(CONFIG_IDF_TARGET_LINUX)
    return mrb_fixnum_value(ble_ui_state());
#else
    return mrb_fixnum_value(0);
#endif
}

// FmrbApp.ble_settings_window(seconds) -> Integer: opens the settings window
// for that many seconds (0 closes it) and returns the seconds left, 0 when it
// did not open (BLE not running, or no BLE in this build). While it is open
// the BLE console may write the WiFi and Asterism settings
// (doc/ruby_asterism/report/cf2.md). FmrbApp.ble_settings_left -> Integer,
// allocation-free, for the desktop's 1Hz indicator. Linux has no BLE: 0.
#if !defined(CONFIG_IDF_TARGET_LINUX)
extern fmrb_err_t ble_settings_window_set(uint32_t seconds);   // main/drivers/ble/ble_task.h
extern uint32_t ble_settings_window_left(void);
#endif
static mrb_value mrb_fmrb_app_s_ble_settings_window(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int seconds;
    mrb_get_args(mrb, "i", &seconds);
    if (seconds < 0) seconds = 0;
#if !defined(CONFIG_IDF_TARGET_LINUX)
    ble_settings_window_set((uint32_t)seconds);
    return mrb_fixnum_value((mrb_int)ble_settings_window_left());
#else
    return mrb_fixnum_value(0);
#endif
}

static mrb_value mrb_fmrb_app_s_ble_settings_left(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
#if !defined(CONFIG_IDF_TARGET_LINUX)
    return mrb_fixnum_value((mrb_int)ble_settings_window_left());
#else
    return mrb_fixnum_value(0);
#endif
}

// FmrbApp.set_kana_mode(n) -> nil. 0 = off, 1 = hiragana, 2 = katakana.
// Behind the clickable mode indicators: the keyboard is not the only way to
// reach kana input, and on a touch machine it is not even the usual one.
static mrb_value mrb_fmrb_app_s_set_kana_mode(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int mode;
    mrb_get_args(mrb, "i", &mode);
    if (mode < 0) mode = 0;
    fmrb_host_set_kana_mode((uint8_t)mode);
    return mrb_nil_value();
}

// FmrbApp.set_text_cursor(x, y, h) -> nil. Where this app's text cursor is
// (canvas coordinates, h the line's height; 0 says it takes no text now), so
// a PC's IME shows the text being composed and its candidates there
// (doc/linux_native L12b). Nothing happens on the devices.
static mrb_value mrb_fmrb_app_s_set_text_cursor(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int x, y, h;
    mrb_get_args(mrb, "iii", &x, &y, &h);
    fmrb_host_set_text_cursor((int)x, (int)y, (int)h);
    return mrb_nil_value();
}

// FmrbApp.audio_muted? -> bool. The machine-wide mute (doc/reference/audio_output.md).
// Allocation-free: the desktop's speaker cell reads it every second, which is
// how a change made remotely shows up there.
static mrb_value mrb_fmrb_app_s_audio_muted_p(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_bool_value(fmrb_host_audio_muted());
}

// FmrbApp.path_read_only?(path) -> bool. Whether the entry at a virtual path
// cannot be deleted or renamed (fmrb_hal_file_entry_is_read_only): the native
// build's distribution tree and /mnt/root. Always false on the devices, the
// simulation and the browser. The File Manager asks before it offers a delete.
static mrb_value mrb_fmrb_app_s_path_read_only_p(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *path;
    mrb_get_args(mrb, "z", &path);
    return mrb_bool_value(fmrb_hal_file_entry_is_read_only(path));
}

// FmrbApp.set_audio_mute(on) -> nil. Mutes or unmutes the whole machine at
// the output stage and saves it to system_conf; see fmrb_host_set_audio_mute.
static mrb_value mrb_fmrb_app_s_set_audio_mute(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_bool on;
    mrb_get_args(mrb, "b", &on);
    fmrb_host_set_audio_mute(on ? true : false);
    return mrb_nil_value();
}

// FmrbApp.audio_volume -> Integer 0-10, and FmrbApp.set_audio_volume(n).
// The machine's volume (doc/reference/audio_output.md); see fmrb_host_set_audio_volume.
static mrb_value mrb_fmrb_app_s_audio_volume(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_fixnum_value(fmrb_host_audio_volume());
}

static mrb_value mrb_fmrb_app_s_set_audio_volume(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int step;
    mrb_get_args(mrb, "i", &step);
    fmrb_host_set_audio_volume((int)step);
    return mrb_nil_value();
}

// FmrbApp.preview_audio_volume(n) -> nil: apply without saving (Config's
// live preview). FmrbApp.audio_conf_saved -> nil: the caller wrote the mute
// and volume into system_conf itself (Config's Save).
static mrb_value mrb_fmrb_app_s_preview_audio_volume(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    mrb_int step;
    mrb_get_args(mrb, "i", &step);
    fmrb_host_preview_audio_volume((int)step);
    return mrb_nil_value();
}

static mrb_value mrb_fmrb_app_s_audio_conf_saved(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    fmrb_host_audio_conf_written();
    return mrb_nil_value();
}

static mrb_value mrb_fmrb_app_s_ble_start(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
#if !defined(CONFIG_IDF_TARGET_LINUX) && !defined(CONFIG_IDF_TARGET_ESP32P4)
    return mrb_bool_value(ble_service_start() == 0);
#else
    FMRB_LOGI(TAG, "FmrbApp.ble_start: not supported on this target");
    return mrb_false_value();
#endif
}

// FmrbApp.rd_stream_state -> 0 none / 1 MJPEG / 2 H.264. Allocation-free,
// polled at 1Hz by the desktop so the wifi icon can switch to a
// screen-transfer glyph while remote desktop video is going out.
#if defined(FMRB_HAS_WIFI) && defined(CONFIG_IDF_TARGET_ESP32P4)
extern int rd_http_stream_state(void);
#endif
static mrb_value mrb_fmrb_app_s_rd_stream_state(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
#if defined(FMRB_HAS_WIFI) && defined(CONFIG_IDF_TARGET_ESP32P4)
    return mrb_fixnum_value(rd_http_stream_state());
#else
    return mrb_fixnum_value(0);
#endif
}

// FmrbApp.wifi_connected? -> bool. Allocation-free subset of wifi_info for
// the 1Hz status-bar icon, which only needs the connection state.
static mrb_value mrb_fmrb_app_s_wifi_connected_p(mrb_state *mrb, mrb_value klass)
{
    (void)mrb; (void)klass;
    return mrb_bool_value(fmrb_platform_sys_net_connected());
}

// FmrbApp.wifi_info() -> Hash {connected:, ip:, ssid:, hostname:[, signal:]}
// or nil, from the OS services (fmrb_platform_sys.h): the WiFi STA on the
// boards that have one, the host network on the Linux builds, the OS's link
// on the native program (with the signal 0-100 when it is wireless). nil
// where there is no network, and the desktop greys the icon.
static mrb_value mrb_fmrb_app_s_wifi_info(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    fmrb_sys_net_t n;
    if (!fmrb_platform_sys_net_info(&n)) return mrb_nil_value();
    mrb_value hash = mrb_hash_new_capa(mrb, 5);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "connected")),
                 mrb_bool_value(n.connected));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "ip")),
                 mrb_str_new_cstr(mrb, n.ip));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "ssid")),
                 mrb_str_new_cstr(mrb, n.ssid));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "hostname")),
                 mrb_str_new_cstr(mrb, n.hostname));
    // Only where the OS tells it (the native program on a wireless link).
    if (n.signal >= 0) {
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "signal")),
                     mrb_fixnum_value(n.signal));
    }
    return hash;
}

// FmrbApp._clear_cache(path) -> Hash {ok:, deleted:, status:}
// Sends FILE_CMD_RMDIR via host_task. WROVER enforces that path resolves
// inside its cache root, so callers cannot target arbitrary directories. The
// returned :deleted reports how many entries (files + directories) were
// removed; :status mirrors the remote status byte (0=ok, 1=rejected, 2=fs
// error).
static mrb_value mrb_fmrb_app_s_clear_cache(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    char *path;
    mrb_get_args(mrb, "z", &path);

    size_t path_len = strlen(path);
    if (path_len == 0 || path_len >= 120) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid path length");
    }

    file_cmd_result_t result;
    result.done_sem = fmrb_semaphore_create_binary();
    if (!result.done_sem) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to create semaphore");
    }
    result.result = -1;
    memset(&result.data, 0, sizeof(result.data));

    fmrb_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = FMRB_MSG_TYPE_FILE_TRANSFER;
    msg.size = sizeof(file_cmd_t);

    file_cmd_t *cmd = (file_cmd_t *)msg.data;
    cmd->cmd_type = FILE_CMD_RMDIR;
    cmd->result = &result;
    cmd->path_len = (uint16_t)path_len;
    memcpy(cmd->path, path, path_len);

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
    if (ret != FMRB_OK) {
        fmrb_semaphore_delete(result.done_sem);
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to enqueue clear_cache request");
    }

    // Wait long enough for the WROVER walk to finish on a large cache.
        // The reply context is on this stack; hold off a forced delete.
    fmrb_app_sync_io_begin();
    fmrb_base_type_t wait_ret = fmrb_semaphore_take(result.done_sem,
                                                    FMRB_MS_TO_TICKS(20000));
    fmrb_app_sync_io_end();
    fmrb_semaphore_delete(result.done_sem);

    mrb_value hash = mrb_hash_new(mrb);
    mrb_value k_ok      = mrb_symbol_value(mrb_intern_lit(mrb, "ok"));
    mrb_value k_deleted = mrb_symbol_value(mrb_intern_lit(mrb, "deleted"));
    mrb_value k_status  = mrb_symbol_value(mrb_intern_lit(mrb, "status"));

    if (wait_ret != FMRB_PASS) {
        mrb_hash_set(mrb, hash, k_ok, mrb_false_value());
        mrb_hash_set(mrb, hash, k_deleted, mrb_fixnum_value(0));
        mrb_hash_set(mrb, hash, k_status, mrb_fixnum_value(-1));
        return hash;
    }

    bool ok = (result.result == 0);
    mrb_hash_set(mrb, hash, k_ok, ok ? mrb_true_value() : mrb_false_value());
    mrb_hash_set(mrb, hash, k_deleted,
                 mrb_fixnum_value((mrb_int)result.data.rmdir.deleted_count));
    mrb_hash_set(mrb, hash, k_status,
                 mrb_fixnum_value((mrb_int)result.data.rmdir.remote_status));
    return hash;
}

// FmrbApp.enable_cursor -> nil
// Allow the OS cursor to appear on the next mouse event. system_desktop calls
// this once its boot animation is complete so the cursor stays hidden during
// the boot logo reveal.
static mrb_value mrb_fmrb_app_s_enable_cursor(mrb_state *mrb, mrb_value klass)
{
    fmrb_host_enable_cursor();
    return mrb_nil_value();
}

// FmrbApp.set_cursor_visible(visible) -> nil
// Show or hide the OS cursor immediately. Fullscreen apps use this to
// suppress the cursor while running and restore it on exit.
static mrb_value mrb_fmrb_app_s_set_cursor_visible(mrb_state *mrb, mrb_value klass)
{
    mrb_bool visible;
    mrb_get_args(mrb, "b", &visible);
    fmrb_host_set_cursor_visible(visible ? true : false);
    return mrb_nil_value();
}

// FmrbApp.usb_devices -> Array of Hash {type:, vid:, pid:, addr:}
// Snapshot of currently connected USB HID devices. `type` is a short
// string ("KBD"/"MOUSE"/"GAMEPAD"/"OTHER") suitable for one-line display.
// Returns an empty array on Linux (no HID enumeration there) or when the
// USB task is not yet ready.
static mrb_value mrb_fmrb_app_s_usb_devices(mrb_state *mrb, mrb_value klass)
{
    fmrb_usb_device_info_t devs[USB_TASK_MAX_DEVICES];
    int count = usb_task_get_device_info(devs, USB_TASK_MAX_DEVICES);

    mrb_value result = mrb_ary_new_capa(mrb, count);
    for (int i = 0; i < count; i++) {
        const char *type_str;
        switch (devs[i].type) {
            case FMRB_USB_DEV_TYPE_KEYBOARD: type_str = "KBD";     break;
            case FMRB_USB_DEV_TYPE_MOUSE:    type_str = "MOUSE";   break;
            case FMRB_USB_DEV_TYPE_GAMEPAD:  type_str = "GAMEPAD"; break;
            default:                         type_str = "OTHER";   break;
        }
        mrb_value hash = mrb_hash_new_capa(mrb, 4);
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")),
                     mrb_str_new_cstr(mrb, type_str));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "vid")),
                     mrb_fixnum_value(devs[i].vid));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "pid")),
                     mrb_fixnum_value(devs[i].pid));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "addr")),
                     mrb_fixnum_value(devs[i].dev_addr));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "slot")),
                     mrb_fixnum_value(devs[i].slot));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "report_len")),
                     mrb_fixnum_value(devs[i].report_byte_len));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "layout_valid")),
                     devs[i].layout_valid ? mrb_true_value() : mrb_false_value());
        mrb_ary_push(mrb, result, hash);
    }
    return result;
}

// FmrbApp.hid_raw_subscribe(slot) -> bool
// Ask the USB task to forward raw HID reports from `slot` to this app
// (delivered via on_control with cmd "hid_raw"). Used by the HID Inspector.
static mrb_value mrb_fmrb_app_s_hid_raw_subscribe(mrb_state *mrb, mrb_value klass)
{
    mrb_int slot;
    mrb_get_args(mrb, "i", &slot);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        return mrb_false_value();
    }
    fmrb_err_t ret = usb_task_subscribe_raw_reports((int8_t)slot, (uint16_t)ctx->app_id);
    return (ret == FMRB_OK) ? mrb_true_value() : mrb_false_value();
}

// FmrbApp.hid_raw_unsubscribe(slot) -> bool
static mrb_value mrb_fmrb_app_s_hid_raw_unsubscribe(mrb_state *mrb, mrb_value klass)
{
    mrb_int slot;
    mrb_get_args(mrb, "i", &slot);
    fmrb_err_t ret = usb_task_unsubscribe_raw_reports((int8_t)slot);
    return (ret == FMRB_OK) ? mrb_true_value() : mrb_false_value();
}

/* ---- FmrbMic: the microphone, read straight from the app task -------------
 *
 * No message round trip: the samples are produced in this firmware (the
 * ES7210 hangs off the speaker's I2S port), so an app reads them the way it
 * would read from any C function. FmrbAudio#mic_* are one-line wrappers over
 * these (mrblib/fmrb-audio.rb).
 *
 * Samples cross as a byte String of little-endian int16 -- the same shape
 * Fmrb::Fft takes, so `fft.forward(mic.read(512))` needs no conversion.
 */

static mrb_value mrb_fmrb_mic_available(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    return mrb_bool_value(audio_p4_mic_available());
#else
    return mrb_false_value();
#endif
}

static mrb_value mrb_fmrb_mic_rate(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    return mrb_fixnum_value(audio_p4_mic_sample_rate());
#else
    return mrb_fixnum_value(0);
#endif
}

/* FmrbMic.enable(on) -> true / false */
static mrb_value mrb_fmrb_mic_enable(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_bool on = TRUE;
    mrb_get_args(mrb, "|b", &on);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    return mrb_bool_value(audio_p4_mic_enable(on ? true : false) == FMRB_OK);
#else
    (void)on;
    return mrb_false_value();
#endif
}

/* FmrbMic.read(count) -> String of count int16 samples, or nil.
 *
 * Blocks until the samples are there (the driver hands them over about 256 at
 * a time) or the timeout runs out. nil rather than a short String: a spectrum
 * of half a window is not something a caller can use by accident. */
static mrb_value mrb_fmrb_mic_read(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_int count;
    mrb_int timeout_ms = 200;
    mrb_get_args(mrb, "i|i", &count, &timeout_ms);
    if (count < 1 || count > 4096) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "mic read count must be 1..4096");
    }
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    int16_t *buf = (int16_t *)mrb_malloc(mrb, sizeof(int16_t) * (size_t)count);
    int have = 0;
    while (have < count) {
        int n = audio_p4_mic_read(buf + have, (int)count - have, (int)timeout_ms);
        if (n <= 0) {
            mrb_free(mrb, buf);
            return mrb_nil_value();
        }
        have += n;
    }
    mrb_value str = mrb_str_new(mrb, (const char *)buf, sizeof(int16_t) * (size_t)count);
    mrb_free(mrb, buf);
    return str;
#else
    (void)timeout_ms;
    return mrb_nil_value();
#endif
}

void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb)
{
    struct RClass *mic_module = mrb_define_module(mrb, "FmrbMic");
    mrb_define_module_function(mrb, mic_module, "available?", mrb_fmrb_mic_available, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mic_module, "rate", mrb_fmrb_mic_rate, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mic_module, "enable", mrb_fmrb_mic_enable, MRB_ARGS_OPT(1));
    mrb_define_module_function(mrb, mic_module, "read", mrb_fmrb_mic_read, MRB_ARGS_ARG(1, 1));

    // Define FmrbApp class
    struct RClass *app_class = mrb_define_class(mrb, "FmrbApp", mrb->object_class);

    // Instance methods (called from Ruby instances)
    mrb_define_method(mrb, app_class, "_init", mrb_fmrb_app_init, MRB_ARGS_NONE());
    mrb_define_method(mrb, app_class, "_spin", mrb_fmrb_app_spin, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, app_class, "_cleanup", mrb_fmrb_app_cleanup, MRB_ARGS_NONE());
    mrb_define_method(mrb, app_class, "_mark_expected_stop", mrb_fmrb_app_mark_expected_stop, MRB_ARGS_NONE());
    mrb_define_method(mrb, app_class, "_send_message", mrb_fmrb_app_send_message, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, app_class, "_sync_file", mrb_fmrb_app_sync_file, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, app_class, "_send_audio_note", mrb_fmrb_app_send_audio_note, MRB_ARGS_REQ(6));
    mrb_define_method(mrb, app_class, "_audio_note_bytes", mrb_fmrb_app_audio_note_bytes, MRB_ARGS_REQ(6));
    mrb_define_method(mrb, app_class, "_set_window_param", mrb_fmrb_app_set_window_param, MRB_ARGS_REQ(2));

    mrb_define_method(mrb, app_class, "_is_file_app", mrb_fmrb_app_is_file_app, MRB_ARGS_NONE());
    mrb_define_method(mrb, app_class, "_create_canvas", mrb_fmrb_app_create_canvas, MRB_ARGS_REQ(5));
    mrb_define_method(mrb, app_class, "_delete_canvas", mrb_fmrb_app_delete_canvas, MRB_ARGS_REQ(1));

    // Class methods
    mrb_define_class_method(mrb, app_class, "pid", mrb_fmrb_app_s_pid, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "ps", mrb_fmrb_app_s_ps, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "ps_gen", mrb_fmrb_app_s_ps_gen, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "heap_info", mrb_fmrb_app_s_heap_info, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "pool_usage", mrb_fmrb_app_s_pool_usage, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "pool_used", mrb_fmrb_app_s_pool_used, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "sys_pool_info", mrb_fmrb_app_s_sys_pool_info, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "_get_last_error", mrb_fmrb_app_s_get_last_error, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "config", mrb_fmrb_app_s_config, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "uptime_us", mrb_fmrb_app_s_uptime_us, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "script_path", mrb_fmrb_app_s_script_path, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "wallclock", mrb_fmrb_app_s_wallclock, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "set_wallclock", mrb_fmrb_app_s_set_wallclock, MRB_ARGS_REQ(6));
    mrb_define_class_method(mrb, app_class, "gfx_stats", mrb_fmrb_app_s_gfx_stats, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "enable_cursor", mrb_fmrb_app_s_enable_cursor, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "set_cursor_visible", mrb_fmrb_app_s_set_cursor_visible, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "reboot", mrb_fmrb_app_s_reboot, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "power_actions", mrb_fmrb_app_s_power_actions, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "power", mrb_fmrb_app_s_power, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "battery_level", mrb_fmrb_app_s_battery_level, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "battery_charging?", mrb_fmrb_app_s_battery_charging_p, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "linux_available?", mrb_fmrb_app_s_linux_available_p, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "linux_apps", mrb_fmrb_app_s_linux_apps, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, app_class, "linux_launch", mrb_fmrb_app_s_linux_launch, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "linux_handlers", mrb_fmrb_app_s_linux_handlers, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "linux_open", mrb_fmrb_app_s_linux_open, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, app_class, "linux_start", mrb_fmrb_app_s_linux_start, MRB_ARGS_REQ(4));
    mrb_define_class_method(mrb, app_class, "linux_read", mrb_fmrb_app_s_linux_read, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "linux_write", mrb_fmrb_app_s_linux_write, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, app_class, "linux_signal", mrb_fmrb_app_s_linux_signal, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, app_class, "linux_exit_code", mrb_fmrb_app_s_linux_exit_code, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "linux_resize", mrb_fmrb_app_s_linux_resize, MRB_ARGS_REQ(3));
    mrb_define_class_method(mrb, app_class, "linux_close", mrb_fmrb_app_s_linux_close, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "clipboard", mrb_fmrb_app_s_clipboard, MRB_ARGS_OPT(1));
    mrb_define_class_method(mrb, app_class, "set_clipboard", mrb_fmrb_app_s_set_clipboard, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "clipboard_gen", mrb_fmrb_app_s_clipboard_gen, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "wifi_networks", mrb_fmrb_app_s_wifi_networks, MRB_ARGS_OPT(1));
    mrb_define_class_method(mrb, app_class, "wifi_connect", mrb_fmrb_app_s_wifi_connect, MRB_ARGS_ARG(1, 1));
    mrb_define_class_method(mrb, app_class, "wifi_disconnect", mrb_fmrb_app_s_wifi_disconnect, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "bt_devices", mrb_fmrb_app_s_bt_devices, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "bt_power", mrb_fmrb_app_s_bt_power, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "bt_connect", mrb_fmrb_app_s_bt_connect, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, app_class, "notices_gen", mrb_fmrb_app_s_notices_gen, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "notices", mrb_fmrb_app_s_notices, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "notice_dismiss", mrb_fmrb_app_s_notice_dismiss, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "notify", mrb_fmrb_app_s_notify, MRB_ARGS_ARG(1, 1));
#if defined(FMRB_PLATFORM_NATIVE)
    mrb_define_class_method(mrb, app_class, "wl_window", mrb_fmrb_app_s_wl_window, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "wl_close", mrb_fmrb_app_s_wl_close, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "wl_key", mrb_fmrb_app_s_wl_key, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "linux_keyboard", mrb_fmrb_app_s_linux_keyboard, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "linux_settings", mrb_fmrb_app_s_linux_settings, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "settings_page", mrb_fmrb_app_s_settings_page, MRB_ARGS_NONE());
#endif
    mrb_define_class_method(mrb, app_class, "ble_start", mrb_fmrb_app_s_ble_start, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "ble_state", mrb_fmrb_app_s_ble_state, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "ble_settings_window", mrb_fmrb_app_s_ble_settings_window, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "ble_settings_left", mrb_fmrb_app_s_ble_settings_left, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "set_kana_mode", mrb_fmrb_app_s_set_kana_mode, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "set_text_cursor", mrb_fmrb_app_s_set_text_cursor, MRB_ARGS_REQ(3));
    mrb_define_class_method(mrb, app_class, "audio_muted?", mrb_fmrb_app_s_audio_muted_p, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "path_read_only?", mrb_fmrb_app_s_path_read_only_p, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "set_audio_mute", mrb_fmrb_app_s_set_audio_mute, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "audio_volume", mrb_fmrb_app_s_audio_volume, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "set_audio_volume", mrb_fmrb_app_s_set_audio_volume, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "preview_audio_volume", mrb_fmrb_app_s_preview_audio_volume, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "audio_conf_saved", mrb_fmrb_app_s_audio_conf_saved, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "rd_stream_state", mrb_fmrb_app_s_rd_stream_state, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "wifi_info", mrb_fmrb_app_s_wifi_info, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "wifi_connected?", mrb_fmrb_app_s_wifi_connected_p, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "_clear_cache", mrb_fmrb_app_s_clear_cache, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "usb_devices", mrb_fmrb_app_s_usb_devices, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "hid_raw_subscribe", mrb_fmrb_app_s_hid_raw_subscribe, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app_class, "hid_raw_unsubscribe", mrb_fmrb_app_s_hid_raw_unsubscribe, MRB_ARGS_REQ(1));

    // Note: Constants now defined in FmrbConst module (picoruby-fmrb-const gem)

    // Initialize graphics subsystem
    mrb_fmrb_gfx_init(mrb);
    mrb_fmrb_gfx_block_init(mrb);

    // FmrbToml (toml.c): the firmware's TOML reader for Ruby
    mrb_fmrb_toml_init(mrb);

    // Audio subsystem will be initialized when needed
    //mrb_fmrb_audio_init(mrb);
}

void mrb_picoruby_fmrb_app_final_impl(mrb_state *mrb)
{
    mrb_fmrb_gfx_final(mrb);
    //mrb_fmrb_audio_final(mrb);
}
