/**
 * @file fmrb_basic.cpp
 * @brief Adapter between the fmruby app task and the BASIC interpreter core
 *
 * Everything fmruby specific lives here: the per task memory pool, the console
 * output callback, the app exit signal and the graphics extension. The core
 * itself (core/) stays free of IDF and fmruby headers.
 */

#include "fmrb_basic.h"

#include "basic_core.hpp"
#include "basic_charset.hpp"

// fmrb_app.h has no extern "C" guard of its own (it is a C only header).
extern "C" {
#include "fmrb_app.h"
#include "fmrb_hid_msg.h"
}

#include "fmrb_msg.h"

#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"

#include <cstring>
#include <new>

static const char* TAG = "fmrb_basic";

// Output reaches the console one line at a time (basic_output_cb_t takes a
// string), so put_char() accumulates until a newline.
#define FMRB_BASIC_OUT_LINE_MAX 160

struct basic_state {
    fmrb_mem_handle_t mem_handle;
    fmrb_basic::interpreter* core;
    void* core_storage;

    basic_output_cb_t output_cb;
    void* output_user_data;
    basic_input_cb_t input_cb;
    void* input_user_data;
    basic_gfx_ops_t gfx;
    basic_screen_ops_t screen;
    fmrb_basic::interpreter* core_for_keys;

    char out_line[FMRB_BASIC_OUT_LINE_MAX];
    size_t out_len;
};

namespace {

void flush_output(basic_state* state, bool with_newline) {
    if (state->out_len == 0 && !with_newline) {
        return;
    }
    if (with_newline && state->out_len < FMRB_BASIC_OUT_LINE_MAX - 1) {
        state->out_line[state->out_len++] = '\n';
    }
    state->out_line[state->out_len] = '\0';
    if (state->output_cb) {
        state->output_cb(state->output_user_data, state->out_line);
    } else if (state->out_len > 1) {
        // No console attached (the screen renderer is the real output): keep
        // the text in the log, which is what the headless harness reads.
        state->out_line[state->out_len - 1] = '\0';
        FMRB_LOGI(TAG, "PRINT: %s", state->out_line);
    }
    state->out_len = 0;
}

void* host_alloc(void* user, size_t size) {
    basic_state* state = static_cast<basic_state*>(user);
    return fmrb_malloc(state->mem_handle, size);
}

void host_dealloc(void* user, void* ptr) {
    basic_state* state = static_cast<basic_state*>(user);
    if (ptr) {
        fmrb_free(state->mem_handle, ptr);
    }
}

void host_put_char(void* user, char c) {
    basic_state* state = static_cast<basic_state*>(user);
    if (c == '\n') {
        flush_output(state, true);
        return;
    }
    if (state->out_len >= FMRB_BASIC_OUT_LINE_MAX - 2) {
        flush_output(state, true);
    }
    state->out_line[state->out_len++] = c;
}

int host_read_line(void* user, char* buf, size_t buf_size) {
    basic_state* state = static_cast<basic_state*>(user);
    flush_output(state, false);  // show the prompt before blocking
    if (!state->input_cb) {
        return -1;
    }
    return state->input_cb(state->input_user_data, buf, buf_size);
}

uint32_t host_ticks_ms(void* user) {
    (void)user;
    return static_cast<uint32_t>(fmrb_task_get_tick_count()) * portTICK_PERIOD_MS;
}

void host_sleep_ms(void* user, uint32_t ms) {
    (void)user;
    fmrb_task_delay_ms(ms);
}

bool host_on_tick(void* user) {
    basic_state* state = static_cast<basic_state*>(user);

    // Drain HID events so INKEY$ sees key presses while a program is running.
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (ctx && state->core_for_keys) {
        fmrb_msg_t msg;
        while (fmrb_msg_receive(static_cast<fmrb_proc_id_t>(ctx->app_id), &msg, 0) ==
               FMRB_OK) {
            if (msg.type != FMRB_MSG_TYPE_HID_EVENT) {
                continue;
            }
            const fmrb_hid_key_event_t* key =
                reinterpret_cast<const fmrb_hid_key_event_t*>(msg.data);
            if (key->subtype == HID_MSG_KEY_DOWN && key->character != 0) {
                state->core_for_keys->push_key(
                    fmrb_basic::unicode_to_fbcode(static_cast<uint8_t>(key->character)));
            }
        }
    }

    // Screen updates are batched: present once per tick instead of per cell.
    if (state->screen.present) {
        state->screen.present(state->screen.user_data);
    }

    // Cooperative exit: the kernel asks the app to stop by raising this flag.
    return !fmrb_app_poll_exit_signal(ctx);
}

void host_on_error(void* user, fmrb_basic::error_code code, int32_t line) {
    (void)user;
    FMRB_LOGE(TAG, "BASIC error %s (%s) at line %ld", fmrb_basic::error_mnemonic(code),
              fmrb_basic::error_message(code), static_cast<long>(line));
}

bool host_ext_statement(void* user, uint8_t tk, const fmrb_basic::basic_arg* args,
                        uint8_t argc) {
    basic_state* state = static_cast<basic_state*>(user);
    const fmrb_basic::token statement = static_cast<fmrb_basic::token>(tk);

    // fmruby specific graphics statements. Family BASIC screen, sprite and
    // sound statements land here too and are answered with "unsupported" until
    // the phase that implements them.
    switch (statement) {
        case fmrb_basic::token::cls:
            if (state->gfx.cls) {
                flush_output(state, false);
                state->gfx.cls(state->gfx.user_data);
                return true;
            }
            return false;
        case fmrb_basic::token::circle: {
            if (!state->gfx.circle || argc < 3) {
                return false;
            }
            const int16_t x = args[0].num;
            const int16_t y = args[1].num;
            const int16_t r = args[2].num;
            const uint8_t color = (argc > 3) ? static_cast<uint8_t>(args[3].num) : 0xFF;
            const bool filled = (argc > 4) ? (args[4].num != 0) : false;
            state->gfx.circle(state->gfx.user_data, x, y, r, color, filled);
            return true;
        }
        case fmrb_basic::token::present:
            if (state->gfx.present) {
                flush_output(state, false);
                state->gfx.present(state->gfx.user_data);
                return true;
            }
            return false;
        default:
            break;
    }
    return false;
}

void host_screen_cell(void* user, uint8_t x, uint8_t y, uint8_t code, uint8_t attr) {
    basic_state* state = static_cast<basic_state*>(user);
    if (state->screen.cell) {
        state->screen.cell(state->screen.user_data, x, y, code, attr);
    }
}

void host_screen_present(void* user) {
    basic_state* state = static_cast<basic_state*>(user);
    if (state->screen.present) {
        state->screen.present(state->screen.user_data);
    }
}

void host_screen_palette(void* user, uint8_t attr, uint8_t backdrop, uint8_t c1,
                         uint8_t c2, uint8_t c3) {
    basic_state* state = static_cast<basic_state*>(user);
    if (state->screen.palette) {
        state->screen.palette(state->screen.user_data, attr, backdrop, c1, c2, c3);
    }
}

void host_debug_line(void* user, const char* text) {
    // Screen dumps go to the log with their own prefix, so the headless
    // harness can pull them out with a line filter (phase_b0_report sec 3.3).
    (void)user;
    FMRB_LOGI(TAG, "%s", text);
}

fmrb_basic::basic_host_t make_host(basic_state* state) {
    fmrb_basic::basic_host_t host = {};
    host.alloc = host_alloc;
    host.dealloc = host_dealloc;
    host.put_char = host_put_char;
    host.read_line = host_read_line;
    host.ticks_ms = host_ticks_ms;
    host.sleep_ms = host_sleep_ms;
    host.on_tick = host_on_tick;
    host.on_error = host_on_error;
    host.ext_statement = host_ext_statement;
    host.screen_cell = host_screen_cell;
    host.screen_present = host_screen_present;
    host.screen_palette = host_screen_palette;
    host.debug_line = host_debug_line;
    host.user = state;
    return host;
}

}  // namespace

extern "C" {

fmrb_err_t fmrb_basic_init(void) {
    FMRB_LOGI(TAG, "BASIC subsystem initialized");
    return FMRB_OK;
}

basic_state_t* fmrb_basic_newstate(fmrb_app_task_context_t* ctx) {
    if (!ctx) {
        FMRB_LOGE(TAG, "Context is NULL");
        return nullptr;
    }

    basic_state* state =
        static_cast<basic_state*>(fmrb_malloc(ctx->mem_handle, sizeof(basic_state)));
    if (!state) {
        FMRB_LOGE(TAG, "Failed to allocate BASIC state for task %s", ctx->app_name);
        return nullptr;
    }
    memset(state, 0, sizeof(basic_state));
    state->mem_handle = ctx->mem_handle;

    // The core is placement new'd into pool memory: no global operator new.
    state->core_storage = fmrb_malloc(ctx->mem_handle, sizeof(fmrb_basic::interpreter));
    if (!state->core_storage) {
        FMRB_LOGE(TAG, "Failed to allocate interpreter for task %s", ctx->app_name);
        fmrb_free(ctx->mem_handle, state);
        return nullptr;
    }

    const fmrb_basic::basic_host_t host = make_host(state);
    state->core = new (state->core_storage) fmrb_basic::interpreter(host);
    state->core_for_keys = state->core;
    if (!state->core->init()) {
        FMRB_LOGE(TAG, "Failed to size BASIC storage for task %s", ctx->app_name);
        state->core->~interpreter();
        fmrb_free(ctx->mem_handle, state->core_storage);
        fmrb_free(ctx->mem_handle, state);
        return nullptr;
    }

    FMRB_LOGI(TAG, "BASIC state created for task %s (pool=%d)", ctx->app_name,
              ctx->mempool_id);
    return reinterpret_cast<basic_state_t*>(state);
}

void fmrb_basic_close(basic_state_t* handle) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (!state) {
        return;
    }
    if (state->core) {
        state->core->~interpreter();
    }
    if (state->core_storage) {
        fmrb_free(state->mem_handle, state->core_storage);
    }
    fmrb_free(state->mem_handle, state);
    FMRB_LOGI(TAG, "BASIC state closed");
}

fmrb_err_t fmrb_basic_load(basic_state_t* handle, const char* program) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (!state || !program) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (!state->core->load(program)) {
        flush_output(state, false);
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "Loaded %u program lines", (unsigned)state->core->line_count());
    return FMRB_OK;
}

fmrb_err_t fmrb_basic_run(basic_state_t* handle) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (!state) {
        return FMRB_ERR_INVALID_PARAM;
    }
    const bool ok = state->core->run();
    flush_output(state, false);

    // 00_common memory design rule 4: every phase records what the interpreter
    // actually costs, so the task stack is sized from measurements.
    fmrb_pool_stats_t stats;
    if (fmrb_mem_get_stats(state->mem_handle, &stats) == 0) {
        FMRB_LOGI(TAG,
                  "BASIC usage: pool used=%u free=%u of %u bytes, "
                  "stack headroom=%u bytes",
                  (unsigned)stats.used_size, (unsigned)stats.free_size,
                  (unsigned)stats.total_size,
                  (unsigned)(fmrb_task_get_stack_high_water_mark(NULL) *
                             sizeof(StackType_t)));
    }
    return ok ? FMRB_OK : FMRB_ERR_FAILED;
}

fmrb_err_t fmrb_basic_exec(basic_state_t* handle, const char* statement) {
    (void)handle;
    (void)statement;
    // Direct mode is decided in Phase B4 (phase_b0_report sec 9.1).
    return FMRB_ERR_NOT_SUPPORTED;
}

void fmrb_basic_clear(basic_state_t* handle) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (state) {
        state->core->clear_program();
        FMRB_LOGI(TAG, "Program cleared");
    }
}

fmrb_err_t fmrb_basic_list(basic_state_t* handle) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (!state) {
        return FMRB_ERR_INVALID_PARAM;
    }
    char text[FMRB_BASIC_OUT_LINE_MAX];
    for (uint16_t i = 0; i < state->core->line_count(); ++i) {
        state->core->decrunch_line(i, text, sizeof(text));
        if (state->output_cb) {
            state->output_cb(state->output_user_data, text);
        }
    }
    return FMRB_OK;
}

void fmrb_basic_set_output_cb(basic_state_t* handle, basic_output_cb_t callback,
                              void* user_data) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (state) {
        state->output_cb = callback;
        state->output_user_data = user_data;
    }
}

void fmrb_basic_set_input_cb(basic_state_t* handle, basic_input_cb_t callback,
                             void* user_data) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (state) {
        state->input_cb = callback;
        state->input_user_data = user_data;
    }
}

void fmrb_basic_set_gfx_ops(basic_state_t* handle, const basic_gfx_ops_t* ops) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (state && ops) {
        state->gfx = *ops;
    }
}

void fmrb_basic_set_screen_ops(basic_state_t* handle, const basic_screen_ops_t* ops) {
    basic_state* state = reinterpret_cast<basic_state*>(handle);
    if (!state || !ops) {
        return;
    }
    state->screen = *ops;
    // Push the current palette and repaint so the renderer starts in sync.
    state->core->screen_send_palette();
    state->core->screen_refresh();
}

}  // extern "C"
