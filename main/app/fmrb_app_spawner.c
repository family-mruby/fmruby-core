#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <picoruby.h>
#include "fmrb_hal.h"
#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_msg.h"
#include "fmrb_task_config.h"
#include "fmrb_kernel.h"
#include "fmrb_toml.h"
#include "fmrb_app_meta.h"
#ifdef FMRB_HAVE_WASM
#include "fmrb_wasm.h"
#endif

static const char *TAG = "fmrb_default_apps";

// External irep declarations (compiled by picorbc)
extern const uint8_t system_desktop_irep[];
extern const uint8_t shell_irep[];
#ifndef FMRB_NO_EDITOR
// Not in a build with FMRB_EDITOR=0 (main/CMakeLists.txt): the table below
// has no editor entries then, and fmrb_app_spawn_app refuses their names.
extern const uint8_t editor_irep[];
#endif
extern const uint8_t logviewer_irep[];
extern const uint8_t monitor_irep[];
extern const uint8_t inspector_irep[];
extern const uint8_t appstore_irep[];
extern const uint8_t wifi_irep[];
#ifndef FMRB_NO_TRANSFER_APP
extern const uint8_t transfer_irep[];
#endif
#if defined(FMRB_PLATFORM_NATIVE)
// The keyboard-only desktop (doc/linux_native, L4). Built for the hosted
// programs only (main/CMakeLists.txt leaves it out of the device and
// simulation firmware); the native program starts it in place of the mouse
// desktop when the machine definition says desktop.variant = "keys".
extern const uint8_t system_desktop_keys_irep[];
#endif
#ifdef FMRB_HW_FAMILY_MODERN
// Service host: Modern only, so Retro does not compile its bytecode
// (main/CMakeLists.txt drops it from the glob) and has no symbol to link.
extern const uint8_t services_irep[];
#endif

#if defined(FMRB_KERNEL_ENGINE_SPINEL) || defined(FMRB_APP_ENGINE_EDITOR_SPINEL) || \
    defined(FMRB_APP_ENGINE_DESKTOP_SPINEL)
#include "fmrb_spinel_host.h"

/* The body of a NATIVE task that runs a Spinel-compiled VM (kernel, desktop,
   editor): the Spinel runtime is backed by this task's estalloc pool, so every
   allocation the program makes comes from the slot's own pool, isolated from
   the other Spinel instances, and `ps` reports it through ctx->est.

   Each VM is a Spinel ext program with no entries, and its init runs the Ruby
   top level -- `SomeApp.new.start` -> main_loop -- so the call only returns
   when the program ends; app_task_main then performs the normal task cleanup.

   GC / string-heap thresholds are pool/32 for all three (sp_instance_config
   contract: threshold < pool). Collecting early keeps a burst of allocation --
   a mouse drag's event stream in the kernel, the desktop's app scan reading
   many .toml files -- from reaching the top of the pool between collections,
   which is fatal (exhaustion goes through sp_oom_die), and it claims the GC's
   own mark stack while the pool is still unfragmented. Raising it to delay
   the first collection is not an option either: the string heap gets the same
   number, and a quarter of the pool each ran the editor out of memory while
   typing into a 200KB file (its document lives in POOL_ID_EDITOR_DOC, so its
   own pool only carries the UI state). */
/* Kept out of line: the log calls take enough arguments to need outgoing
   stack slots, and inlined into the runner they grew its frame -- which sits
   under the whole VM on the kernel task's stack -- from 32 to 48 bytes. */
static void __attribute__((noinline, cold))
spinel_vm_fail(const fmrb_app_task_context_t *ctx, const char *why, void *pool, size_t pool_size)
{
    FMRB_LOGE(TAG, "%s: %s (pool %d at %p, %zu bytes)",
              ctx->app_name, why, ctx->mempool_id, pool, pool_size);
}

void fmrb_app_run_spinel_vm(fmrb_app_task_context_t *ctx, void (*init)(void),
                            void (*after)(void))
{
    void  *pool = fmrb_get_mempool_ptr(ctx->mempool_id);
    size_t pool_size = fmrb_get_mempool_size(ctx->mempool_id);
    if (!pool || pool_size == 0) {
        spinel_vm_fail(ctx, "mempool unavailable", pool, pool_size);
        return;
    }
    // One instance per program: see fmrb_app_spinel_claim. The refusal has
    // already been reported, so the task just ends.
    if (fmrb_app_spinel_claim(ctx, (const void *)init) != FMRB_OK) {
        return;
    }
    size_t threshold = pool_size / 32;
    void *est = fmrb_spinel_instance_begin(pool, pool_size, threshold, threshold);
    if (!est) {
        spinel_vm_fail(ctx, "failed to create the Spinel instance", pool, pool_size);
        fmrb_app_spinel_release(ctx);
        return;
    }
    ctx->est = est;

    init();

    fmrb_spinel_instance_end(est);
    ctx->est = NULL;
    // Still holding the claim, so no second instance of the program has
    // started yet when `after` runs.
    if (after) {
        after();
    }
    fmrb_app_spinel_release(ctx);
}
#endif

#ifdef FMRB_APP_ENGINE_EDITOR_SPINEL
/* Spinel engine for the editor (P5): default/editor and default/editor_fs run
   the Spinel-compiled program instead of editor_irep, as a NATIVE task backed
   by this app slot's pool, so the window attributes are unchanged. */
extern void Init_editor(void);   /* the Spinel ext program's init: its top level */
extern void fmrb_spx_ec_release_docs(void);   /* fmrb_spx_editor.c */

/* The editor program's init as fmrb_app_run_spinel_vm sees it (its address
   is also what the one-instance claim records). It runs once the program is
   claimed, so the documents it gives back before Init_editor can only be ones
   a killed editor left (fmrb_spx_editor.c), never a running editor's: a refused
   second editor never gets here. */
static void spinel_editor_init(void)
{
    fmrb_spx_ec_release_docs();
    Init_editor();
}

static void spinel_editor_native(void *arg)
{
    // The program never closes its document slots (fmrb_spx_editor.c says
    // why), so they are given back once the program has ended.
    fmrb_app_run_spinel_vm((fmrb_app_task_context_t *)arg, spinel_editor_init,
                           fmrb_spx_ec_release_docs);
}
#endif /* FMRB_APP_ENGINE_EDITOR_SPINEL */

#ifdef FMRB_APP_ENGINE_DESKTOP_SPINEL
/* Spinel engine (Phase 4 T4-3): system_desktop runs as the Spinel-compiled
   combined desktop program instead of mruby bytecode, as a NATIVE task so it
   gets the same PROC_ID_SYSTEM_APP context, canvas(es), message queue and
   lifecycle the mruby desktop does. */
extern void Init_system_desktop(void);   /* the Spinel ext program's init: its top level */

static void spinel_desktop_native(void *arg)
{
    fmrb_app_run_spinel_vm((fmrb_app_task_context_t *)arg, Init_system_desktop, NULL);
}
#endif /* FMRB_APP_ENGINE_DESKTOP_SPINEL */

// Built-in app configuration table
typedef struct {
    const char*       lookup_name;   // Name used in fmrb_app_spawn_app()
    fmrb_spawn_attr_t attr;
} builtin_app_entry_t;

static const builtin_app_entry_t builtin_app_table[] = {
    { "system/desktop", {
        .app_id = PROC_ID_SYSTEM_APP,
        .type = APP_TYPE_SYSTEM_APP,
        .name = "system_desktop",
#ifdef FMRB_APP_ENGINE_DESKTOP_SPINEL
        .vm_type = FMRB_VM_TYPE_NATIVE,
        .native_func = spinel_desktop_native,
#else
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = system_desktop_irep,
#endif
        .stack_words = FMRB_SYSTEM_APP_TASK_STACK_SIZE,
        .priority = FMRB_SYSTEM_APP_TASK_PRIORITY,
        .flags = FMRB_SYSTEM_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .has_background_canvas = true,
        .window_width = 0,
        .window_height = 0,
        .window_pos_x = 0,
        .window_pos_y = 0,
        .rounded_corners = true
    }},
    { "default/shell", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "FM-Shell",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = shell_irep,
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .resizable = true,
        .window_width = 300,
        .window_height = 200,
        .window_pos_x = 5,
        .window_pos_y = 15,
        // The prompt plus a couple of dozen characters, and enough height for
        // the logo band and a few rows of history.
        .min_window_width = 160,
        .min_window_height = 80,
        .rounded_corners = true
    }},
#ifndef FMRB_NO_EDITOR
    { "default/editor", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "FM-Editor",
#ifdef FMRB_APP_ENGINE_EDITOR_SPINEL
        .vm_type = FMRB_VM_TYPE_NATIVE,
        .native_func = spinel_editor_native,
#else
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = editor_irep,
#endif
        .stack_words = FMRB_EDITOR_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .resizable = true,
        .window_width = 240,
        .window_height = 200,
        .window_pos_x = 5,
        .window_pos_y = 15,
        // Menu bar needs ~218px; one edit row + menu + status fits in ~80px.
        .min_window_width = 220,
        .min_window_height = 80,
        // F11 takes it to the high-resolution fullscreen where the build can
        // show one (doc/fullscreen_hires/); elsewhere this is ignored. It lays
        // itself out at any size (recompute_layout): the whole of a wider
        // screen (doc/linux_native L9e).
        .fullscreen_flags = FMRB_FULLSCREEN_HIRES | FMRB_FULLSCREEN_ANY_SIZE,
        .rounded_corners = true
    }},
    // Fullscreen editor ("serious mode", doc/editor_serious_mode/plan.md).
    // Same irep as default/editor: the editor derives its layout from the user
    // area (recompute_layout), so the whole screen needs no editor-side change.
    // Fullscreen also suspends the other apps, which is part of the point.
    // Window size is filled in from the display config by spawn_builtin_app.
    // No large_memory: the large pools are few (two) and the
    // editor fits the ordinary one.
    { "default/editor_fs", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        // Same name as the windowed editor so the shell / desktop file-open
        // relay (which looks the editor up by name) works for both.
        .name = "FM-Editor",
#ifdef FMRB_APP_ENGINE_EDITOR_SPINEL
        .vm_type = FMRB_VM_TYPE_NATIVE,
        .native_func = spinel_editor_native,
#else
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = editor_irep,
#endif
        .stack_words = FMRB_EDITOR_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .fullscreen = true,
        // Ctrl+Tab may park it: the editor keeps its buffer while parked and
        // repaints on resume (EditorApp#on_resume).
        .fullscreen_switchable = true,
        // High-resolution fullscreen where available (doc/fullscreen_hires/),
        // any size (see default/editor).
        .fullscreen_flags = FMRB_FULLSCREEN_HIRES | FMRB_FULLSCREEN_ANY_SIZE,
        // Same window behaviour as default/editor once F11 makes it a window:
        // without these the corner drag is refused and the window is stuck at
        // whatever size it fell back to.
        .resizable = true,
        .min_window_width = 220,
        .min_window_height = 80,
        .window_width = 0,   // filled from the fullscreen size (fullscreen_window_size)
        .window_height = 0,
        .window_pos_x = 0,
        .window_pos_y = 0,
        .rounded_corners = false  // fullscreen: no frame, no shaped window
    }},
#endif /* FMRB_NO_EDITOR */
    { "default/logviewer", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "LogViewer",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = logviewer_irep,
        .stack_words = FMRB_TOOL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .resizable = true,
        .window_width = 300,
        .window_height = 200,
        .window_pos_x = 5,
        .window_pos_y = 15,
        // The toolbar is the floor for the width (level button, wrap button
        // and the AUTO/HOLD flag side by side); height is the toolbar plus a
        // few rows.
        .min_window_width = 200,
        .min_window_height = 70,
        .rounded_corners = true
    }},
    { "default/monitor", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "Monitor",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = monitor_irep,
        .stack_words = FMRB_TOOL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        // 180x120 fitted eight rows, and the task page now lists the services
        // inside the host as well as the tasks -- nine tasks and six services
        // is more than that, and the surplus fell off the bottom. This fits
        // the whole of both lists, and the width squares the columns off
        // (id / name / state / button).
        .window_width = 240,
        .window_height = 200,
        .window_pos_x = 5,
        .window_pos_y = 15,
        .rounded_corners = true
    }},
#ifdef FMRB_HW_FAMILY_MODERN
    // The service host (doc/user_extension/services/plan.md). Headless: it has
    // no window and never draws, so it is not announced, gets no canvas and is
    // not given the keyboard. Spawned by the kernel at boot when there is a
    // service list to run, and endable like any user app with kill <pid>.
    { "default/services", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "Services",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = services_irep,
        .stack_words = FMRB_SERVICE_APP_TASK_STACK_SIZE,
        // One below a user app: services must not take slices from the app in
        // front (see FMRB_SERVICE_APP_PRIORITY).
        .priority = FMRB_SERVICE_APP_PRIORITY,
        .flags = FMRB_SERVICE_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = true,
        .window_width = 0,
        .window_height = 0,
        .window_pos_x = 0,
        .window_pos_y = 0,
        .rounded_corners = false
    }},
#endif  /* FMRB_HW_FAMILY_MODERN */
    { "default/inspector", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "HID Inspector",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = inspector_irep,
        .stack_words = FMRB_TOOL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .window_width = 280,
        .window_height = 200,
        .window_pos_x = 5,
        .window_pos_y = 15,
        .rounded_corners = true
    }},
    /* The app store used to live in /app/tool as ordinary Ruby, which meant
     * 855 lines were compiled at every launch -- the codegen working set is
     * what made it ask for a larger VM pool than any other tool, and on a
     * Retro it ran out anyway. As bytecode it costs 19 KB of the app
     * partition and no compiler at all.
     *
     * Window figures are the ones its .app.toml carried. */
    { "default/appstore", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "App Store",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = appstore_irep,
        .stack_words = FMRB_APPSTORE_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .window_width = 280,
        .window_height = 190,
        .window_pos_x = 14,
        .window_pos_y = 26,
        .resizable = true,
        .min_window_width = 240,
        .min_window_height = 150,
        .rounded_corners = true
    }},
    /* The Wi-Fi window (doc/wifi_settings): the PC settings window's Wi-Fi
     * page, opened from the desktop's Network dialog on the devices. The
     * PC's own window is the same size (native/backend/native_settings.c),
     * and it fits a Retro screen under the menu bar. */
    { "default/wifi", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "Wi-Fi",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = wifi_irep,
        .stack_words = FMRB_WIFI_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .window_width = 280,
        .window_height = 200,
        .window_pos_x = 20,
        .window_pos_y = 20,
        .rounded_corners = true
    }},
#ifndef FMRB_NO_TRANSFER_APP
    /* The transfer app (doc/release_3_1 R3): files and apps between
     * machines over Asterism, and the remote shell. It was an app in the
     * storage that read some 220 KB of Ruby with require at every start;
     * the build now puts its parts together (main/ruby/default_app/
     * transfer.app.rb and transfer/) and nothing is compiled here. The
     * attributes are the ones its .app.toml carried: headless (background),
     * the large pool (with Asterism's gems a VM on the S3 holds about 300 KB
     * before it loads anything), one at a time, and movable -- not to be
     * moved (an Asterism session cannot be carried), but so that its VM
     * keeps the table of this build's method names that the receiving side
     * of a move answers from (doc/checkpoint F5). builtin_path is the name
     * it goes by as a path: what ps shows, what single_instance compares and
     * what the kernel adopts (fmrb_kernel.rb TRANSFER_APP). The stack,
     * priority and flags are an ordinary user app's. */
    { "default/transfer", {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "Transfer",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = transfer_irep,
        .builtin_path = "default/transfer",
        .stack_words = FMRB_USER_APP_TASK_STACK_SIZE,
        .priority = FMRB_USER_APP_PRIORITY,
        .flags = FMRB_USER_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = true,
        .large_memory = true,
        .single_instance = true,
#ifdef FMRB_HAVE_CHECKPOINT
        .movable = true,
#endif
        .window_width = 0,
        .window_height = 0,
        .window_pos_x = 0,
        .window_pos_y = 0,
        .rounded_corners = false
    }},
#endif /* FMRB_NO_TRANSFER_APP */
};

#define BUILTIN_APP_COUNT (sizeof(builtin_app_table) / sizeof(builtin_app_table[0]))

static void notify_kernel_app_spawned(int32_t pid);

// The window a fullscreen app starts with: the display size, or the screen
// the display switches to for it (fmrb_app_screen_mode_size: 640x360 for a
// fullscreen_hires app on the standard screen, 426x240 for a plain one on the
// 640x360 screen; fmrb_app_fullscreen_size_for has the native compositor's
// wider screen). Starting at that size means the canvas is created large
// enough and the kernel's switch needs no resize.
static void fullscreen_window_size(uint8_t flags, uint16_t* w, uint16_t* h)
{
    fmrb_app_fullscreen_size_for(flags, w, h);
}

/*
 * The editor runs once (doc/editor_multi_file, plan 4.4): it holds several
 * files, and the Spinel one cannot run twice. The kernel hands a second
 * request to the running editor itself before it gets here; this covers the
 * callers that reach the spawner directly (the debug daemon, the remote
 * control's launch), which used to get a second editor -- refused with an
 * error dialog on Spinel, running beside the first on mruby. They get the
 * running editor's pid instead, and the kernel brings it to the front.
 * Answers that pid, or -1 when no editor is up (one that is stopping does not
 * count: a new one may start once it has gone).
 */
static int32_t running_builtin_id(const char* name)
{
    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        const fmrb_app_task_context_t* ctx = fmrb_app_get_context_by_id(i);
        if (!ctx || ctx->state == PROC_STATE_STOPPING) continue;
        if (strncmp(ctx->app_name, name, FMRB_MAX_APP_NAME) == 0) return ctx->app_id;
    }
    return -1;
}

/* {"cmd":"focus_app","pid":N} to the kernel, as the taskbar sends it. */
static void notify_kernel_focus(int32_t pid)
{
    if (pid < 0 || pid > 0x7F) {
        return;
    }
    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_CONTROL,
        .src_pid = (fmrb_proc_id_t)pid,
    };
    uint8_t* d = msg.data;
    size_t p = 0;
    d[p++] = 0x82;  // fixmap 2
    d[p++] = 0xA3; memcpy(&d[p], "cmd", 3); p += 3;
    d[p++] = 0xA9; memcpy(&d[p], "focus_app", 9); p += 9;
    d[p++] = 0xA3; memcpy(&d[p], "pid", 3); p += 3;
    d[p++] = (uint8_t)pid;
    msg.size = p;
    fmrb_msg_send(PROC_ID_KERNEL, &msg, 10);
}

static fmrb_err_t spawn_builtin_app(const builtin_app_entry_t* entry, int32_t* out_pid)
{
    FMRB_LOGI(TAG, "Spawning built-in app: %s", entry->lookup_name);

    if (strncmp(entry->lookup_name, "default/editor", 14) == 0) {
        int32_t running = running_builtin_id(entry->attr.name);
        if (running >= 0) {
            FMRB_LOGI(TAG, "%s already running as pid %d: brought to the front",
                      entry->attr.name, (int)running);
            if (out_pid) {
                *out_pid = running;
            }
            notify_kernel_focus(running);
            return FMRB_OK;
        }
    }

    fmrb_spawn_attr_t attr = entry->attr;
#if defined(FMRB_PLATFORM_NATIVE)
    // The same "system_desktop" to the rest of the machine (the kernel and
    // the window list know it by that name), the keys program inside.
    if (strcmp(entry->lookup_name, "system/desktop") == 0 &&
        fmrb_kernel_native_machine()->keys_desktop) {
        attr.vm_type = FMRB_VM_TYPE_MRUBY;
        attr.load_mode = FMRB_LOAD_MODE_BYTECODE;
        attr.bytecode = system_desktop_keys_irep;
        FMRB_LOGI(TAG, "Desktop: the keyboard-only variant (system_desktop_keys)");
    }
#endif
    // Fullscreen built-ins leave the window size at 0 and take the fullscreen
    // size here, the same way the .app.toml path does for a fullscreen app
    // (fmrb_app_init would otherwise fall back to default_user_app_*).
    if (attr.fullscreen && attr.window_width == 0 && attr.window_height == 0) {
        fullscreen_window_size(attr.fullscreen_flags, &attr.window_width, &attr.window_height);
        attr.window_pos_x = 0;
        attr.window_pos_y = 0;
    }
    int32_t app_id;
    fmrb_err_t result = fmrb_app_spawn(&attr, &app_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "Built-in app spawned: id=%d, name=%s", app_id, entry->lookup_name);
        if (out_pid) {
            *out_pid = app_id;
        }
        // Same reason as for file based apps: a spawn that did not come through
        // the kernel's own handler still needs the post-spawn work, or the app
        // starts without the keyboard. System apps are excluded -- the desktop
        // and overlays are not what a user just asked to run, and the kernel
        // assigns their focus itself at boot.
        if (entry->attr.type != APP_TYPE_SYSTEM_APP) {
            notify_kernel_app_spawned(app_id);
        }
    } else {
        FMRB_LOGE(TAG, "Failed to spawn built-in app: %s (error=%d)", entry->lookup_name, result);
    }
    return result;
}

/**
 * Report a freshly spawned user app to the kernel.
 *
 * The kernel does the post-spawn work -- window list refresh, fullscreen entry,
 * keyboard routing -- in its own spawn handler, but this function is also
 * reached without going through it: the debug daemon calls the C API directly.
 * Those apps used to start without the keyboard, so the user had to click the
 * canvas first. The kernel ignores the notification when it already handled
 * that pid, so sending it unconditionally is safe.
 *
 * Payload is msgpack {"cmd":"spawned","pid":N}; pid is a small slot index, so
 * it always fits a positive fixint.
 */
static void notify_kernel_app_spawned(int32_t pid)
{
    if (pid < 0 || pid > 0x7F) {
        return;
    }
    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_CONTROL,
        .src_pid = (fmrb_proc_id_t)pid,
    };
    uint8_t* d = msg.data;
    size_t p = 0;
    d[p++] = 0x82;  // fixmap 2
    d[p++] = 0xA3; memcpy(&d[p], "cmd", 3); p += 3;
    d[p++] = 0xA7; memcpy(&d[p], "spawned", 7); p += 7;
    d[p++] = 0xA3; memcpy(&d[p], "pid", 3); p += 3;
    d[p++] = (uint8_t)pid;
    msg.size = p;
    fmrb_msg_send(PROC_ID_KERNEL, &msg, 10);
}

static fmrb_err_t spawn_user_app(const char* app_name, int32_t* out_pid)
{
    if (!app_name) {
        FMRB_LOGE(TAG, "app_name is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    FMRB_LOGI(TAG, "Creating user app from file: %s (stack free: %u bytes)",
              app_name, (unsigned)(fmrb_task_get_stack_high_water_mark(0) * sizeof(StackType_t)));

    fmrb_err_t result = FMRB_ERR_FAILED;

    FMRB_LOGI(TAG, "[spawn] 2 file_open");
    // Validate file exists before spawning
    fmrb_file_t file = NULL;
    fmrb_err_t ret = fmrb_hal_file_open(app_name, FMRB_O_RDONLY, &file);
    FMRB_LOGI(TAG, "[spawn] 3 file_open ret=%d", ret);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "File not found or cannot open: %s", app_name);
        result = FMRB_ERR_NOT_FOUND;
        goto cleanup_buffers;
    }
    fmrb_hal_file_close(file);

    FMRB_LOGI(TAG, "[spawn] 4 vm_type");
    // Determine VM type from file extension
    fmrb_vm_type_t vm_type = FMRB_VM_TYPE_MRUBY;  // Default to mruby
    const char* ext = strrchr(app_name, '.');
    if (ext) {
        if (strcmp(ext, ".lua") == 0) {
            vm_type = FMRB_VM_TYPE_LUA;
            FMRB_LOGI(TAG, "Detected Lua script: %s", app_name);
        } else if (strcmp(ext, ".rb") == 0) {
            vm_type = FMRB_VM_TYPE_MRUBY;
            FMRB_LOGI(TAG, "Detected mruby script: %s", app_name);
        } else if (strcmp(ext, ".bas") == 0) {
            vm_type = FMRB_VM_TYPE_BASIC;
            FMRB_LOGI(TAG, "Detected BASIC script: %s", app_name);
        } else if (strcmp(ext, ".py") == 0) {
            vm_type = FMRB_VM_TYPE_MICROPYTHON;
            FMRB_LOGI(TAG, "Detected Python script: %s", app_name);
        } else if (strcmp(ext, ".wasm") == 0) {
#ifdef FMRB_HAVE_WASM
            vm_type = FMRB_VM_TYPE_WASM;
            FMRB_LOGI(TAG, "Detected wasm app: %s", app_name);
#else
            FMRB_LOGE(TAG, "wasm apps are not in this build: %s", app_name);
            result = FMRB_ERR_NOT_SUPPORTED;
            goto cleanup_buffers;
#endif
        }
    }
#ifdef FMRB_HAVE_CHECKPOINT
    // A checkpoint image of an mruby app (doc/checkpoint): restored, not
    // compiled. Its attributes come from the .toml written beside it
    // (fmrb_app_checkpoint.c), read below like any app's.
    fmrb_load_mode_t load_mode = FMRB_LOAD_MODE_FILE;
    if (ext && strcmp(ext, ".ckpt") == 0) {
        vm_type = FMRB_VM_TYPE_MRUBY;
        load_mode = FMRB_LOAD_MODE_CHECKPOINT;
        FMRB_LOGI(TAG, "Detected mruby checkpoint image: %s", app_name);
    }
#endif

    FMRB_LOGI(TAG, "[spawn] 5 name check");
    if (!strrchr(app_name, '.')) {
        FMRB_LOGE(TAG, "Invalid app name (no extension): %s", app_name);
        result = FMRB_ERR_INVALID_PARAM;
        goto cleanup_buffers;
    }

    // Default values for spawn attributes
    const char* app_screen_name = NULL;
    const char* toml_screen_name = NULL;
    const char* toml_window_mode = NULL;
    bool headless = false;
    // A .bas app is a Family BASIC screen (28x24 characters), so it runs full
    // screen unless its .app.toml asks for something else (compat_plan
    // sec 4.2, phase_b2.md T2-5).
    bool fullscreen = (vm_type == FMRB_VM_TYPE_BASIC);
    int window_width = 100;
    int window_height = 100;
    int window_pos_x = 50;
    int window_pos_y = 50;
    bool resizable = false;
    bool large_memory = false;
    bool single_instance = false;
    bool movable = false;
    const char* exclusive_group = NULL;
    int min_window_width = 0;
    int min_window_height = 0;
    bool rounded_corners = true;
    bool frameless = false;
    int window_drag = -1;  // -1: the default for the frame (resolved below)
    bool fullscreen_switchable = false;
    uint8_t fullscreen_flags = 0;
    int stack_size = FMRB_USER_APP_TASK_STACK_SIZE;
    int32_t wasm_memory_pages = 0;  // 0: the default
#ifdef FMRB_HAVE_WASM
    // The interpreter's frames and the boundary functions it calls share the
    // app task's stack (its own default, doc/wasm_apps decision 27).
    if (vm_type == FMRB_VM_TYPE_WASM) {
        stack_size = FMRB_WASM_APP_TASK_STACK_SIZE;
    }
#endif

    FMRB_LOGI(TAG, "[spawn] 6 meta_load '%s'", app_name);
    // The .app.toml and the "#---fmrb" comment fence, merged key by key with
    // the sidecar winning (components/fmrb_toml/fmrb_app_meta.h). M(key) is
    // the table that answers key, NULL (the getters' default) when neither does.
    fmrb_app_meta_t meta;
    bool have_meta = fmrb_app_meta_load(app_name, &meta);
#define M(key) fmrb_app_meta_table(&meta, (key))
    if (have_meta) {
        FMRB_LOGI(TAG, "Loaded app attributes for %s (sidecar=%d comment=%d)",
                  app_name, meta.sidecar != NULL, meta.comment != NULL);

        toml_screen_name = fmrb_toml_get_string(M("app_screen_name"), "app_screen_name", NULL);
        if (toml_screen_name) {
            app_screen_name = toml_screen_name;
        }

        // Parse default_window_mode
        toml_window_mode = fmrb_toml_get_string(M("default_window_mode"), "default_window_mode", NULL);
        if (toml_window_mode) {
            if (strcmp(toml_window_mode, "background") == 0) {
                headless = true;
            } else if (strcmp(toml_window_mode, "fullscreen") == 0) {
                fullscreen = true;
            } else if (strcmp(toml_window_mode, "window") == 0 ||
                       strcmp(toml_window_mode, "fullwindow") == 0) {
                headless = false;
                fullscreen = false;
            } else {
                headless = false;
            }
        }

        // Parse fullscreen_hires: the app is laid out for 640x360 and may be
        // shown on the high-resolution screen while it is fullscreen
        // (doc/fullscreen_hires/). fullscreen_any_size: it lays itself out at
        // whatever size it is given, so a 640x360 screen is not switched to
        // 426x240 for it (doc/modern_resolution/). Each written as `true`, or
        // as 1 like the older flags; ignored where they mean nothing.
        if (fmrb_toml_get_bool(M("fullscreen_hires"), "fullscreen_hires", false) ||
            fmrb_toml_get_int(M("fullscreen_hires"), "fullscreen_hires", 0) != 0) {
            fullscreen_flags |= FMRB_FULLSCREEN_HIRES;
        }
        if (fmrb_toml_get_bool(M("fullscreen_any_size"), "fullscreen_any_size", false) ||
            fmrb_toml_get_int(M("fullscreen_any_size"), "fullscreen_any_size", 0) != 0) {
            fullscreen_flags |= FMRB_FULLSCREEN_ANY_SIZE;
        }

        // Parse window dimensions and position
        if (fullscreen) {
            uint16_t fw, fh;
            fullscreen_window_size(fullscreen_flags, &fw, &fh);
            window_width = fw;
            window_height = fh;
            window_pos_x = 0;
            window_pos_y = 0;
        } else {
            window_width = (int)fmrb_toml_get_int(M("default_window_width"), "default_window_width", 100);
            window_height = (int)fmrb_toml_get_int(M("default_window_height"), "default_window_height", 100);
            window_pos_x = (int)fmrb_toml_get_int(M("default_window_pos_x"), "default_window_pos_x", 50);
            window_pos_y = (int)fmrb_toml_get_int(M("default_window_pos_y"), "default_window_pos_y", 50);
        }

        // Parse resizable flag (default: false)
        resizable = (bool)fmrb_toml_get_int(M("resizable"), "resizable", 0);

        // Parse per-app minimum window size (0 = use global default 64)
        min_window_width  = (int)fmrb_toml_get_int(M("min_window_width"), "min_window_width",  0);
        min_window_height = (int)fmrb_toml_get_int(M("min_window_height"), "min_window_height", 0);

        // Parse large_memory flag (default: false)
        large_memory = (bool)fmrb_toml_get_int(M("large_memory"), "large_memory", 0);

        // Parse single_instance / exclusive_group: who the app may not run
        // beside (launch_claim in fmrb_app.c). single_instance refuses a
        // second copy of this file; exclusive_group refuses the launch while
        // any app declaring the same group runs. Written like
        // fullscreen_hires: `true` or 1.
        single_instance = fmrb_toml_get_bool(M("single_instance"), "single_instance", false) ||
                          fmrb_toml_get_int(M("single_instance"), "single_instance", 0) != 0;
#ifdef FMRB_HAVE_CHECKPOINT
        // movable: the app may be written to a checkpoint image and go on
        // elsewhere (doc/checkpoint, D6). Only an mruby app can be; the
        // builds without mruby-checkpoint do not read it.
        movable = (fmrb_toml_get_bool(M("movable"), "movable", false) ||
                   fmrb_toml_get_int(M("movable"), "movable", 0) != 0) &&
                  vm_type == FMRB_VM_TYPE_MRUBY;
#endif
        exclusive_group = fmrb_toml_get_string(M("exclusive_group"), "exclusive_group", NULL);
        if (exclusive_group && exclusive_group[0] == '\0') {
            fmrb_sys_free((void*)exclusive_group);
            exclusive_group = NULL;
        } else if (exclusive_group && strlen(exclusive_group) >= FMRB_EXCLUSIVE_GROUP_MAX) {
            FMRB_LOGW(TAG, "exclusive_group \"%s\" is longer than %d bytes; only the "
                           "first %d are compared", exclusive_group,
                      FMRB_EXCLUSIVE_GROUP_MAX - 1, FMRB_EXCLUSIVE_GROUP_MAX - 1);
        }

        // Parse rounded_corners flag (default: true).
        // When false, the window canvas is created opaque (no transparent compositing),
        // which is faster but disables the rounded corner / shaped window look.
        rounded_corners = (bool)fmrb_toml_get_int(M("rounded_corners"), "rounded_corners", 1);

        // Parse window_frame / window_drag (doc/frameless_window/).
        // window_frame = "none" leaves the window without a frame, title bar
        // or close button; anything else (default "normal") keeps them.
        // window_drag = "title" | "anywhere" | "none"; the default follows
        // the frame: a framed window is dragged by its title bar, a
        // frameless one from anywhere.
        const char* frame_str = fmrb_toml_get_string(M("window_frame"), "window_frame", NULL);
        if (frame_str) {
            if (strcmp(frame_str, "none") == 0) {
                frameless = true;
            } else if (strcmp(frame_str, "normal") != 0) {
                FMRB_LOGW(TAG, "window_frame \"%s\" is not \"normal\" or \"none\"; using \"normal\"",
                          frame_str);
            }
            fmrb_sys_free((void*)frame_str);
        }
        const char* drag_str = fmrb_toml_get_string(M("window_drag"), "window_drag", NULL);
        if (drag_str) {
            if (strcmp(drag_str, "title") == 0) {
                window_drag = FMRB_WINDOW_DRAG_TITLE;
            } else if (strcmp(drag_str, "anywhere") == 0) {
                window_drag = FMRB_WINDOW_DRAG_ANYWHERE;
            } else if (strcmp(drag_str, "none") == 0) {
                window_drag = FMRB_WINDOW_DRAG_NONE;
            } else {
                FMRB_LOGW(TAG, "window_drag \"%s\" is not \"title\", \"anywhere\" or \"none\"; "
                               "using the default", drag_str);
            }
            fmrb_sys_free((void*)drag_str);
        }

        // Parse fullscreen_switchable: the app declares its on_suspend hides
        // the canvas and on_resume redraws, so the kernel may park it on
        // Ctrl+Tab and bring it back later instead of refusing to leave.
        fullscreen_switchable = (bool)fmrb_toml_get_int(M("fullscreen_switchable"), "fullscreen_switchable", 0);

        // Parse task_stack_kb: per-app task C stack size in KB. Costs internal
        // RAM only while the app runs; the default is a floor, not a knob to
        // lower (see FMRB_USER_APP_TASK_STACK_SIZE in fmrb_task_config.h).
        int stack_kb = (int)fmrb_toml_get_int(M("task_stack_kb"), "task_stack_kb", 0);
        if (stack_kb > 0) {
            stack_size = stack_kb * 1024;
            if (stack_size < FMRB_USER_APP_TASK_STACK_SIZE) {
                FMRB_LOGW(TAG, "task_stack_kb=%d below the %d KB floor, clamped",
                          stack_kb, FMRB_USER_APP_TASK_STACK_SIZE / 1024);
                stack_size = FMRB_USER_APP_TASK_STACK_SIZE;
            } else if (stack_size > FMRB_USER_APP_TASK_STACK_MAX) {
                FMRB_LOGW(TAG, "task_stack_kb=%d above the %d KB cap, clamped",
                          stack_kb, FMRB_USER_APP_TASK_STACK_MAX / 1024);
                stack_size = FMRB_USER_APP_TASK_STACK_MAX;
            }
            FMRB_LOGI(TAG, "Per-app task stack: %d KB", stack_size / 1024);
        }

        // Parse wasm_memory_pages: the most a wasm app's linear memory may
        // grow to, in 64 KB pages. Kept as written; the wasm runtime checks
        // it against the app's frame and refuses a bad value with the reason
        // (a value that is not a whole number of 1 or more is passed as -1).
        if (vm_type == FMRB_VM_TYPE_WASM && M("wasm_memory_pages")) {
            int64_t pages = fmrb_toml_get_int(M("wasm_memory_pages"), "wasm_memory_pages", -1);
            wasm_memory_pages = pages < 1 ? -1 : (pages > 65536 ? 65536 : (int32_t)pages);
        }
    } else {
        FMRB_LOGW(TAG, "No .app.toml and no comment toml for %s", app_name);
    }

    // The metadata is how an app under /app gets its name and window; a file the
    // user just wrote and wants to run has none. Fall back to the file name so
    // "save, then run it" works (B4: the editor RUN and BASIC SAVE both produce
    // such files). Everything else already has a default.
    char derived_name[24];
    if (!app_screen_name) {
        const char* base = app_name;
        for (const char* p = app_name; *p != '\0'; p++) {
            if (*p == '/') {
                base = p + 1;
            }
        }
        size_t n = 0;
        while (base[n] != '\0' && base[n] != '.' && n < sizeof(derived_name) - 1) {
            derived_name[n] = base[n];
            n++;
        }
        derived_name[n] = '\0';
        if (n == 0) {
            FMRB_LOGE(TAG, "Cannot launch %s: no name in the path", app_name);
            result = FMRB_ERR_INVALID_PARAM;
            goto cleanup_toml;
        }
        app_screen_name = derived_name;
        FMRB_LOGI(TAG, "No app_screen_name for %s: using '%s'", app_name, app_screen_name);
    }

#ifdef FMRB_HAVE_CHECKPOINT
    // An image written on another machine carries the window as it was there
    // (doc/checkpoint F4): Retro's screen is narrower than Modern's. A window
    // that does not fit this screen is made to: as large as the screen at
    // most, then moved in. The app sees the new size as a resize
    // (FmrbApp#_checkpoint_restored calls on_resize). A fullscreen app takes
    // this screen's fullscreen size above, like any app.
    if (load_mode == FMRB_LOAD_MODE_CHECKPOINT && !fullscreen && !headless) {
        uint16_t sw = 0, sh = 0;
        fmrb_kernel_fullscreen_size(&sw, &sh);
        if (sw > 0 && sh > 0) {
            int w0 = window_width, h0 = window_height, x0 = window_pos_x, y0 = window_pos_y;
            if (window_width > sw) window_width = sw;
            if (window_height > sh) window_height = sh;
            if (window_pos_x + window_width > sw) window_pos_x = sw - window_width;
            if (window_pos_y + window_height > sh) window_pos_y = sh - window_height;
            if (window_pos_x < 0) window_pos_x = 0;
            if (window_pos_y < 0) window_pos_y = 0;
            if (min_window_width > window_width) min_window_width = window_width;
            if (min_window_height > window_height) min_window_height = window_height;
            if (w0 != window_width || h0 != window_height || x0 != window_pos_x || y0 != window_pos_y) {
                FMRB_LOGI(TAG, "The image's window %dx%d at %d,%d is made to fit this screen (%ux%u): "
                          "%dx%d at %d,%d", w0, h0, x0, y0, (unsigned)sw, (unsigned)sh,
                          window_width, window_height, window_pos_x, window_pos_y);
            }
        }
    }
#endif

    FMRB_LOGI(TAG, "[spawn] 7 fmrb_app_spawn vm=%d", vm_type);
    // Set spawn attributes
    fmrb_spawn_attr_t attr = {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = app_screen_name,
        .vm_type = vm_type,
#ifdef FMRB_HAVE_CHECKPOINT
        .load_mode = load_mode,
#else
        .load_mode = FMRB_LOAD_MODE_FILE,
#endif
        .filepath = app_name,
        .stack_words = stack_size,
        .priority = FMRB_USER_APP_PRIORITY,
        .flags = FMRB_USER_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = headless,
        .fullscreen = fullscreen,
        .fullscreen_switchable = fullscreen_switchable,
        .fullscreen_flags = fullscreen_flags,
        .resizable = resizable,
        .large_memory = large_memory,
        .wasm_memory_pages = wasm_memory_pages,
        .single_instance = single_instance,
        .movable = movable,
        .exclusive_group = exclusive_group,
        .window_width = window_width,
        .window_height = window_height,
        .window_pos_x = window_pos_x,
        .window_pos_y = window_pos_y,
        .min_window_width  = (uint16_t)min_window_width,
        .min_window_height = (uint16_t)min_window_height,
        .rounded_corners = rounded_corners,
        .frameless = frameless,
        .window_drag = (uint8_t)(window_drag >= 0 ? window_drag
                                 : (frameless ? FMRB_WINDOW_DRAG_ANYWHERE : FMRB_WINDOW_DRAG_TITLE)),
    };

    // Spawn the app
    int32_t app_id;
    result = fmrb_app_spawn(&attr, &app_id);
    FMRB_LOGI(TAG, "[spawn] 8 fmrb_app_spawn ret=%d", result);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "User app spawned: id=%d, name=%s, file=%s",
                  app_id, app_screen_name, app_name);
        if (out_pid) {
            *out_pid = app_id;
        }
        notify_kernel_app_spawned(app_id);
    } else {
        FMRB_LOGE(TAG, "Failed to spawn user app: %s (error=%d)", app_name, result);
    }

cleanup_toml:
    // Free TOML config and allocated strings
    if (toml_screen_name) {
        fmrb_sys_free((void*)toml_screen_name);
    }
    if (toml_window_mode) {
        fmrb_sys_free((void*)toml_window_mode);
    }
    if (exclusive_group) {
        fmrb_sys_free((void*)exclusive_group);
    }
    fmrb_app_meta_free(&meta);
#undef M

cleanup_buffers:
    return result;
}

fmrb_err_t fmrb_app_spawn_app(const char* app_name, int32_t* out_pid)
{
    if (app_name == NULL) {
        FMRB_LOGE(TAG, "app_name is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    // Search built-in app table
    for (size_t i = 0; i < BUILTIN_APP_COUNT; i++) {
        if (strcmp(app_name, builtin_app_table[i].lookup_name) == 0) {
            return spawn_builtin_app(&builtin_app_table[i], out_pid);
        }
    }

#ifdef FMRB_NO_EDITOR
    // A build without the editor (FMRB_EDITOR=0, the AtomS3R and StickS3)
    // says so, rather than calling the name unknown: the request is for
    // something other machines have, this one was built without.
    if (strcmp(app_name, "default/editor") == 0 || strcmp(app_name, "default/editor_fs") == 0) {
        FMRB_LOGW(TAG, "%s: the editor is not in this build (FMRB_EDITOR=0)", app_name);
        return FMRB_ERR_NOT_SUPPORTED;
    }
#endif

    // Reject unknown built-in app names
    if (strncmp(app_name, "system/", 7) == 0 || strncmp(app_name, "default/", 8) == 0) {
        FMRB_LOGE(TAG, "Unknown built-in app name: %s", app_name);
        return FMRB_ERR_NOT_FOUND;
    }

    // User App from filesystem
    fmrb_err_t ret = spawn_user_app(app_name, out_pid);
#ifdef FMRB_HAVE_CHECKPOINT
    // An image asked to be started (a move) hears it could not be.
    if (ret != FMRB_OK) fmrb_app_checkpoint_spawn_failed(app_name, ret);
#endif
    return ret;
}