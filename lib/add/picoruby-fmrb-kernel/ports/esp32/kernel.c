#include <mruby.h>
#include <mruby/value.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "hal.h"
#include "task.h"
#include "fmrb_kernel.h"
#include "fmrb_app.h"
#include "fmrb_rtos.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "fmrb_hid_msg.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"
#include "fmrb_transport.h"
#include "status_led.h"
#include "boot.h"
#include "hal.h"

static const char* TAG = "kernel";

// Kernel#_init() - Initialize kernel handler
// Sets @tick, @max_app_num instance variables. The message queue is created
// at task spawn time in fmrb_app.c (before the RUNNING state transition) so
// senders cannot race ahead of the queue's existence.
static mrb_value mrb_kernel_handler_init(mrb_state *mrb, mrb_value self)
{
    // Set @tick instance variable (default 33ms)
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@tick"),
               mrb_fixnum_value(33));

    // Set @max_app_num instance variable
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@max_app_num"),
               mrb_fixnum_value(FMRB_MAX_APPS));

    // Set @max_path_len instance variable
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@max_path_len"),
               mrb_fixnum_value(FMRB_MAX_PATH_LEN));

    FMRB_LOGI(TAG, "Kernel handler initialized: tick=%d, max_apps=%d",
             33, FMRB_MAX_APPS);

    return mrb_nil_value();
}

// Kernel#_poll_message(timeout_ms) -> {type:, src_pid:, data:} | nil
// Receive ONE message for the kernel proc, blocking up to timeout_ms. Returns a
// hash (symbol keys, matching the Spinel base layer) or nil on timeout/error.
// The Ruby main_loop drives dispatch (control inversion): it calls msg_handler
// itself instead of C funcalling back into Ruby, so the mruby and Spinel kernels
// share one main_loop source.
static mrb_value mrb_kernel_handler_poll_message(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_int timeout_ms;
    mrb_get_args(mrb, "i", &timeout_ms);

    // fmrb_msg_receive takes milliseconds (applies MS_TO_TICKS internally).
    fmrb_msg_t msg;
    fmrb_err_t ret = fmrb_msg_receive(PROC_ID_KERNEL, &msg,
                                      (uint32_t)(timeout_ms < 0 ? 0 : timeout_ms));
    if (ret != FMRB_OK) {
        // Timeout or receive error: no message this poll.
        if (ret != FMRB_ERR_TIMEOUT) {
            FMRB_LOGW(TAG, "Kernel message receive error: %d", ret);
        }
        return mrb_nil_value();
    }

    // Build Ruby hash: {type: int, src_pid: int, data: string}
    mrb_value hash = mrb_hash_new(mrb);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")),
                 mrb_fixnum_value(msg.type));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "src_pid")),
                 mrb_fixnum_value(msg.src_pid));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "data")),
                 mrb_str_new(mrb, (const char*)msg.data, msg.size));
    return hash;
}

// Kernel#_spawn_app_req(app_name) -> Integer (PID) or nil
// Spawn application by name, returns PID on success or nil on failure.
// On failure @last_spawn_err carries the fmrb_err.h code (0 on success) so the
// kernel can tell the user why instead of guessing; the Spinel port sets the
// same ivar from Ruby.
static mrb_value mrb_kernel_handler_spawn_app_req(mrb_state *mrb, mrb_value self)
{
    const char *app_name;
    mrb_get_args(mrb, "z", &app_name);

    FMRB_LOGI(TAG, "Spawning app: %s", app_name);

    int32_t new_pid = -1;
    fmrb_err_t result = fmrb_app_spawn_app(app_name, &new_pid);
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@last_spawn_err"),
               mrb_fixnum_value(result == FMRB_OK ? 0 : (mrb_int)result));

    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "App %s spawned successfully with PID %d", app_name, new_pid);
        return mrb_fixnum_value(new_pid);
    } else {
        FMRB_LOGE(TAG, "Failed to spawn app: %s (error=%d)", app_name, result);
        return mrb_nil_value();
    }
}

static mrb_value mrb_kernel_set_ready(mrb_state *mrb, mrb_value self)
{
    fmrb_kernel_set_ready();
    return mrb_nil_value();
}

// Kernel#check_protocol_version(timeout_ms = 5000) -> bool
// Check protocol version with host
static mrb_value mrb_kernel_check_protocol_version(mrb_state *mrb, mrb_value self)
{
    mrb_int timeout_ms = 5000;  // Default 5 seconds
    mrb_get_args(mrb, "|i", &timeout_ms);

    FMRB_LOGI(TAG, "Checking protocol version (timeout=%d ms)...", (int)timeout_ms);

    fmrb_err_t ret = fmrb_transport_check_version((uint32_t)timeout_ms);

    if (ret == FMRB_OK) {
        FMRB_LOGI(TAG, "Protocol version check succeeded");
        return mrb_true_value();
    } else {
        FMRB_LOGE(TAG, "Protocol version check failed: %d", ret);
        return mrb_false_value();
    }
}

// Kernel#check_ga_version(timeout_ms = 5000) -> bool
// Check Graphics-Audio firmware version with host
static mrb_value mrb_kernel_check_ga_version(mrb_state *mrb, mrb_value self)
{
    mrb_int timeout_ms = 5000;
    mrb_get_args(mrb, "|i", &timeout_ms);

    FMRB_LOGI(TAG, "Checking GA firmware version (timeout=%d ms)...", (int)timeout_ms);

    fmrb_err_t ret = fmrb_transport_check_ga_version((uint32_t)timeout_ms);

    if (ret == FMRB_OK) {
        FMRB_LOGI(TAG, "GA version check succeeded");
        return mrb_true_value();
    } else {
        FMRB_LOGE(TAG, "GA version check failed: %d", ret);
        return mrb_false_value();
    }
}

// Kernel#_set_error_led(level) -> nil
// Set the Status LED error pattern (see FmrbConst::LED_ERR_*).
static mrb_value mrb_kernel_set_error_led(mrb_state *mrb, mrb_value self)
{
    mrb_int level;
    mrb_get_args(mrb, "i", &level);
    status_led_set_error((int)level);
    return mrb_nil_value();
}

// FmrbKernel.boot_complete! -> nil
// Signal that the desktop boot animation has finished and the system is
// fully interactive. Switches the green status LED from the fast boot
// blink to the slow heartbeat. Callable from any mruby VM (class method)
// since the status_led state is process-global.
static mrb_value mrb_kernel_boot_complete(mrb_state *mrb, mrb_value self)
{
    status_led_set_boot_complete();
    return mrb_nil_value();
}

// FmrbKernel#_set_hid_target(pid) - Set HID event target app
static mrb_value mrb_kernel_set_hid_target(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);

    if (pid < 0 || pid > 255) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid PID");
    }

    fmrb_err_t ret = fmrb_kernel_set_hid_target((uint8_t)pid);
    if (ret != FMRB_OK) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to set HID target");
    }

    return mrb_nil_value();
}

// FmrbKernel#_set_focused_window(window_id) - Set focused window ID
static mrb_value mrb_kernel_set_focused_window(mrb_state *mrb, mrb_value self)
{
    mrb_int window_id;
    mrb_get_args(mrb, "i", &window_id);

    if (window_id < 0 || window_id > 255) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid window ID");
    }

    fmrb_err_t ret = fmrb_kernel_set_focused_window((uint8_t)window_id);
    if (ret != FMRB_OK) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to set focused window");
    }

    return mrb_nil_value();
}

// FmrbKernel#_get_window_list() -> Array of hashes
// Returns list of active windows with position and size info
static mrb_value mrb_kernel_get_window_list(mrb_state *mrb, mrb_value self)
{
    fmrb_window_info_t windows[FMRB_MAX_APPS];
    int32_t count = fmrb_app_get_window_list(windows, FMRB_MAX_APPS);

    // Create Ruby array
    mrb_value array = mrb_ary_new_capa(mrb, count);

    for (int32_t i = 0; i < count; i++) {
        // Create hash for each window: {pid:, app_name:, x:, y:, width:, height:, z_order:}
        mrb_value hash = mrb_hash_new(mrb);
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "pid")),
                     mrb_fixnum_value(windows[i].pid));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "app_name")),
                     mrb_str_new_cstr(mrb, windows[i].app_name));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "x")),
                     mrb_fixnum_value(windows[i].x));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "y")),
                     mrb_fixnum_value(windows[i].y));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "width")),
                     mrb_fixnum_value(windows[i].width));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "height")),
                     mrb_fixnum_value(windows[i].height));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "z_order")),
                     mrb_fixnum_value(windows[i].z_order));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "fullscreen")),
                     mrb_bool_value(windows[i].fullscreen));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "resizable")),
                     mrb_bool_value(windows[i].resizable));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "min_width")),
                     mrb_fixnum_value(windows[i].min_width));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "min_height")),
                     mrb_fixnum_value(windows[i].min_height));

        mrb_ary_push(mrb, array, hash);
    }

    return array;
}

// Internal helper shared by _send_raw_message / _try_send_raw_message.
// timeout_ms: ticks to wait for queue space. Use 0 for non-blocking send.
static mrb_value kernel_send_raw_message_impl(mrb_state *mrb, mrb_value self, uint32_t timeout_ms)
{
    mrb_int dest_pid, msg_type;
    mrb_value data_val;
    mrb_get_args(mrb, "iiS", &dest_pid, &msg_type, &data_val);

    // Get binary data
    const char* data_ptr = RSTRING_PTR(data_val);
    mrb_int data_len = RSTRING_LEN(data_val);

    if (data_len > FMRB_MAX_MSG_PAYLOAD_SIZE) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "Message data too large: %d bytes (max %d)",
                   data_len, FMRB_MAX_MSG_PAYLOAD_SIZE);
    }

    // Create message
    fmrb_msg_t msg = {
        .type = (uint8_t)msg_type,
        .src_pid = PROC_ID_KERNEL,
        .size = (uint16_t)data_len
    };
    memcpy(msg.data, data_ptr, data_len);

    // Send message
    fmrb_err_t ret = fmrb_msg_send((uint8_t)dest_pid, &msg, timeout_ms);

    return mrb_bool_value(ret == FMRB_OK);
}

// FmrbKernel#_send_raw_message(dest_pid, msg_type, data) -> bool
// Send raw binary message to another process (blocking up to 100 ms)
static mrb_value mrb_kernel_send_raw_message(mrb_state *mrb, mrb_value self)
{
    return kernel_send_raw_message_impl(mrb, self, 100);
}

// FmrbKernel#_try_send_raw_message(dest_pid, msg_type, data) -> bool
// Non-blocking variant: returns false immediately if the destination queue
// is full. Use for high-rate, drop-tolerant messages such as resize-preview
// updates, where blocking the kernel for 100 ms per send causes the router
// to fall behind the mouse cursor.
static mrb_value mrb_kernel_try_send_raw_message(mrb_state *mrb, mrb_value self)
{
    return kernel_send_raw_message_impl(mrb, self, 0);
}

// FmrbKernel#_bring_to_front(pid) -> bool
// Bring window to front
static mrb_value mrb_kernel_bring_to_front(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);

    if (pid < 0 || pid > 255) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid PID");
    }

    fmrb_err_t ret = fmrb_app_bring_to_front((uint8_t)pid);
    return mrb_bool_value(ret == FMRB_OK);
}

// FmrbKernel#_update_window_position(pid, x, y) -> bool
// Update window position for drag and drop
static mrb_value mrb_kernel_update_window_position(mrb_state *mrb, mrb_value self)
{
    mrb_int pid, x, y;
    mrb_get_args(mrb, "iii", &pid, &x, &y);

    if (pid < 0 || pid > 255) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid PID");
    }

    if (x < 0 || x > 65535 || y < 0 || y > 65535) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid position");
    }

    fmrb_err_t ret = fmrb_app_update_window_position((uint8_t)pid, (uint16_t)x, (uint16_t)y);
    return mrb_bool_value(ret == FMRB_OK);
}

// FmrbKernel#_get_sync_files() -> Array of Hashes [{src: "...", dest: "..."}, ...]
static mrb_value mrb_kernel_get_sync_files(mrb_state *mrb, mrb_value self)
{
    fmrb_sync_file_entry_t entries[8];
    int count = fmrb_kernel_get_sync_files(entries, 8);

    mrb_value array = mrb_ary_new_capa(mrb, count);
    for (int i = 0; i < count; i++) {
        mrb_value hash = mrb_hash_new(mrb);
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "src")),
                     mrb_str_new_cstr(mrb, entries[i].src));
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "dest")),
                     mrb_str_new_cstr(mrb, entries[i].dest));
        mrb_ary_push(mrb, array, hash);
    }
    return array;
}

// FmrbKernel#_sync_file(src, dest) -> true/false
static mrb_value mrb_kernel_sync_file(mrb_state *mrb, mrb_value self)
{
    const char *src, *dest;
    mrb_get_args(mrb, "zz", &src, &dest);

    fmrb_err_t ret = fmrb_kernel_sync_file(src, dest);
    return mrb_bool_value(ret == FMRB_OK);
}

// FmrbKernel#_update_window_size(pid, width, height) -> bool
// Update window size for resize operation
static mrb_value mrb_kernel_update_window_size(mrb_state *mrb, mrb_value self)
{
    mrb_int pid, width, height;
    mrb_get_args(mrb, "iii", &pid, &width, &height);

    if (pid < 0 || pid > 255) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid PID");
    }

    if (width < 0 || width > 65535 || height < 0 || height > 65535) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid size");
    }

    fmrb_err_t ret = fmrb_app_update_window_size((uint8_t)pid, (uint16_t)width, (uint16_t)height);
    return mrb_bool_value(ret == FMRB_OK);
}

// FmrbKernel#_get_app_info(pid) -> Hash or nil
// Returns { load_mode: Int, path: String, name: String }
static mrb_value mrb_kernel_get_app_info(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);

    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id((int32_t)pid);
    if (!ctx) {
        return mrb_nil_value();
    }

    mrb_value hash = mrb_hash_new_capa(mrb, 4);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "load_mode")),
                 mrb_fixnum_value(ctx->load_mode));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "name")),
                 mrb_str_new_cstr(mrb, ctx->app_name));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "fullscreen")),
                 mrb_bool_value(ctx->fullscreen));

    if (ctx->load_mode == FMRB_LOAD_MODE_FILE && ctx->load_data) {
        mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "path")),
                     mrb_str_new_cstr(mrb, (const char *)ctx->load_data));
    }

    // Expose VM type as a symbol so the kernel can route behavior per runtime
    const char *vm_sym = "native";
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY:       vm_sym = "mruby"; break;
        case FMRB_VM_TYPE_LUA:         vm_sym = "lua";   break;
        case FMRB_VM_TYPE_BASIC:       vm_sym = "basic"; break;
        case FMRB_VM_TYPE_MICROPYTHON: vm_sym = "micropython"; break;
        default: break;
    }
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "vm_type")),
                 mrb_symbol_value(mrb_intern_cstr(mrb, vm_sym)));

    return hash;
}


// FmrbKernel#_get_last_error -> Hash {name:, error:} or nil
static mrb_value mrb_kernel_get_last_error(mrb_state *mrb, mrb_value self)
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

// Kernel#_suspend_app(pid) -> true/false
static mrb_value mrb_kernel_suspend_app(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);
    return fmrb_app_suspend((int32_t)pid) ? mrb_true_value() : mrb_false_value();
}

// Kernel#_resume_app(pid) -> true/false
static mrb_value mrb_kernel_resume_app(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);
    return fmrb_app_resume((int32_t)pid) ? mrb_true_value() : mrb_false_value();
}

// Kernel#_reap_app(pid) -> true/false
// Delete the FreeRTOS task associated with pid after the app has self-cleaned
// and parked itself. Idempotent: true if already reaped.
static mrb_value mrb_kernel_reap_app(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);
    return fmrb_app_reap((int32_t)pid) ? mrb_true_value() : mrb_false_value();
}

// FmrbKernel#_sync_time_to_host() -> true/false
// Send current system time (and TZ) to graphics-audio side via CONTROL SET_TIME
static mrb_value mrb_kernel_sync_time_to_host(mrb_state *mrb, mrb_value self)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    fmrb_control_set_time_t cmd = {
        .tv_sec = (int64_t)tv.tv_sec,
        .tv_usec = (int32_t)tv.tv_usec
    };
    memset(cmd.tz, 0, sizeof(cmd.tz));
    const char *tz = getenv("TZ");
    if (tz) {
        strncpy(cmd.tz, tz, sizeof(cmd.tz) - 1);
    }

    fmrb_err_t ret = fmrb_transport_send(
        FMRB_LINK_TYPE_CONTROL,
        FMRB_LINK_CONTROL_SET_TIME,
        (const uint8_t*)&cmd,
        sizeof(cmd),
        FMRB_TRANSPORT_TIMEOUT_DEFAULT
    );

    return (ret == FMRB_OK) ? mrb_true_value() : mrb_false_value();
}

void mrb_fmrb_kernel_init(mrb_state *mrb)
{
    // Define FmrbKernel class
    struct RClass *handler_class = mrb_define_class(mrb, "FmrbKernel", mrb->object_class);
    mrb_define_method(mrb, handler_class, "_set_ready", mrb_kernel_set_ready, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_init", mrb_kernel_handler_init, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_poll_message", mrb_kernel_handler_poll_message, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_spawn_app_req", mrb_kernel_handler_spawn_app_req, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "check_protocol_version", mrb_kernel_check_protocol_version, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, handler_class, "check_ga_version", mrb_kernel_check_ga_version, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, handler_class, "_set_error_led", mrb_kernel_set_error_led, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, handler_class, "boot_complete!", mrb_kernel_boot_complete, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_get_window_list", mrb_kernel_get_window_list, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_set_hid_target", mrb_kernel_set_hid_target, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_set_focused_window", mrb_kernel_set_focused_window, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_send_raw_message", mrb_kernel_send_raw_message, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, handler_class, "_try_send_raw_message", mrb_kernel_try_send_raw_message, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, handler_class, "_bring_to_front", mrb_kernel_bring_to_front, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_update_window_position", mrb_kernel_update_window_position, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, handler_class, "_update_window_size", mrb_kernel_update_window_size, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, handler_class, "_get_sync_files", mrb_kernel_get_sync_files, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_sync_file", mrb_kernel_sync_file, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, handler_class, "_get_app_info", mrb_kernel_get_app_info, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_get_last_error", mrb_kernel_get_last_error, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_suspend_app", mrb_kernel_suspend_app, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_resume_app", mrb_kernel_resume_app, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_reap_app", mrb_kernel_reap_app, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_sync_time_to_host", mrb_kernel_sync_time_to_host, MRB_ARGS_NONE());

    // Note: Constants now defined in FmrbConst module (picoruby-fmrb-const gem)
}

void mrb_fmrb_kernel_final(mrb_state *mrb)
{
    // Cleanup if needed
}
