#pragma once

#include <stdint.h>

// ============================================================
// System Limits
// ============================================================

// FMRB_MAX_APPS and the counts derived from it live in fmrb_limits.h, which
// fmrb_mem_config.h includes too (one pool per app slot).
#include "fmrb_limits.h"

#include "fmrb_rtos.h"

// ============================================================
// Task Configuration
// ============================================================
// All task stack sizes, priorities, and creation flags are
// defined here for centralized management.
//
// FMRB_TASK_FLAG_PSRAM:    stack allocated on PSRAM (saves internal RAM)
// FMRB_TASK_FLAG_PINNED_0: pinned to core 0
// FMRB_TASK_FLAG_PINNED_1: pinned to core 1
// FMRB_TASK_FLAG_NONE:     internal RAM stack, any core
//
// WARNING: Tasks with PSRAM stacks must NOT pass stack-local
// buffers to SPI flash DMA (LittleFS, FAT, etc.).
// Use heap_caps_malloc(MALLOC_CAP_INTERNAL) for DMA buffers.
// ============================================================

// --- Core Assignment Policy ---
// Core 0: HW-facing tasks (USB, SPI, GPIO, RTC, LED, FS proxy, host transport)
// Core 1: mruby VM / app tasks (kernel, system_desktop, system_overlay, shell, user apps)
//
// Note: PSRAM tasks use xTaskCreateWithCaps which does not support core pinning.
// PSRAM tasks run on any core (FreeRTOS scheduler decides).
// Internal RAM tasks can be pinned to a specific core.

// --- mruby App tasks (Core 1, PSRAM stack) ---

// Kernel task (mruby VM for OS kernel)
// TEMPORARY: pinned to core 1 with INTERNAL DRAM stack to test the
// hypothesis that PSRAM-stack tasks are involved in the BSS guard
// corruption near _bt_bss_start. Revert to FMRB_TASK_FLAG_PSRAM if the
// corruption is unrelated.
// Sized from measured frames, not guesswork: compiling the Spinel-generated
// kernel with -fstack-usage (ILP32, -O2) puts its heaviest Ruby methods at
// handle_app_control 4.4KB and handle_hid_event 4.1KB, on top of the entry and
// main_loop frames and whatever the FFI shim and newlib add below them. 12KB
// left no useful margin over that chain.
// The mruby VM keeps its Ruby frames on the mrb stack inside the VM pool, so
// only the C interpreter loop lives here: measured high-water on P4 is 8.8KB,
// which 12KB covers with ~3.4KB to spare.
#ifdef FMRB_KERNEL_ENGINE_SPINEL
#define FMRB_KERNEL_TASK_STACK_SIZE     (16 * 1024)
#else
#define FMRB_KERNEL_TASK_STACK_SIZE     (12 * 1024)
#endif
#define FMRB_KERNEL_TASK_PRIORITY       (9)
#define FMRB_KERNEL_TASK_FLAGS          FMRB_TASK_FLAG_PINNED_1

// Host task (graphics/audio transport, SPI slave comm, heap alloc)
// Must be internal RAM (uses realloc via msgpack)
// History: 16KB was NOT enough -- heavy GFX flood (~80 cmds/s + msgpack encode
// + UART) left ~1KB free and corrupted NimBLE BSS via stack overflow
// (ble_hs_state_ctx overwritten, "Host not enabled. Dropping the packet!").
// It was bumped to 32KB then re-measured: the M-2 session (S3, 2026-08-02,
// games running + window drags + BLE transfer concurrently) peaked at 15.4KB,
// matching two earlier independent measurements (15.2KB, 15.4KB) -- the peak
// is a fixed chain, not load-proportional. 24KB keeps a 8.6KB (~56%) margin.
#define FMRB_HOST_TASK_STACK_SIZE       (24 * 1024)
#define FMRB_HOST_TASK_PRIORITY         (10)
#define FMRB_HOST_TASK_FLAGS            FMRB_TASK_FLAG_PINNED_0

// System App task (mruby VM for system GUI)
// TEMPORARY: pinned to core 1 with INTERNAL DRAM stack — see KERNEL note.
// 12KB overflowed: the Spinel desktop task died in vsnprintf during its first
// draw (stack protection fault on ESP32-P4). Measured frames explain it --
// system_desktop_entry alone is 6.2KB, FmrbApp#initialize 2.0KB, the draw_*
// methods 1.7-2.0KB each -- so one on_create chain spends ~11KB before any
// helper runs. The mruby desktop was no better off: it ran with 1020 bytes of
// its 12KB left, the same margin that once let the host task corrupt NimBLE's
// BSS (see FMRB_HOST_TASK_STACK_SIZE above). Hence 16KB, not 12KB, for mruby:
// its measured high-water is 11.3KB (launcher grid draw), leaving ~4.7KB.
#ifdef FMRB_APP_ENGINE_DESKTOP_SPINEL
#define FMRB_SYSTEM_APP_TASK_STACK_SIZE (24 * 1024)
#else
#define FMRB_SYSTEM_APP_TASK_STACK_SIZE (16 * 1024)
#endif
#define FMRB_SYSTEM_APP_TASK_PRIORITY   (8)
#define FMRB_SYSTEM_APP_TASK_FLAGS      FMRB_TASK_FLAG_PINNED_1

// Shell App task (mruby VM for shell)
#define FMRB_SHELL_APP_TASK_STACK_SIZE  (12 * 1024)
#define FMRB_SHELL_APP_PRIORITY         (2)
#define FMRB_SHELL_APP_TASK_FLAGS       FMRB_TASK_FLAG_PINNED_1

// Editor task. The editor shared the shell's 12KB until the type support
// (completion, hover, diagnostics, signature help) started parsing the
// document on this task: prism's parser and the engine's tree walks are
// recursive, and a Tab press with 2.9KB of stack left took the Tab5 down with
// a stack protection fault (doc/editor_ti/report/p5.md). The Linux simulator
// never showed it -- its tasks have far more room.
//
// Paid for by the same feature: moving the engine's 16KB arena to external
// RAM freed more internal RAM than this spends.
#define FMRB_EDITOR_APP_TASK_STACK_SIZE (24 * 1024)

// User App task (mruby/lua/basic VM for user apps)
// 16KB is a FLOOR, not a candidate: PicoRabbit peaked at 15.3KB in the M-2
// session (S3, 2026-08-02), and MicroPython derives its C recursion limit
// from the remaining stack at startup (mp_stack_set_limit), so shrinking this
// silently lowers what Python scripts can recurse to.
#define FMRB_USER_APP_TASK_STACK_SIZE   (16 * 1024)
// Per-app override via "task_stack_kb" in the app's .toml (fmrb_app_spawner).
// Clamped to [FMRB_USER_APP_TASK_STACK_SIZE, FMRB_USER_APP_TASK_STACK_MAX].
// Apps that eval Ruby at runtime need it: the mruby compiler (codegen.c)
// recurses on the task's C stack, and PicoRabbit overflowed 16KB on P4
// compiling a slide's fmrb block (stack protection fault, 2026-08-07).
#define FMRB_USER_APP_TASK_STACK_MAX    (64 * 1024)

// What a spawn is charged beyond its task stack: the message queue, the task
// control block and the semaphores. Measured on a Tab5, an app costs its stack
// plus 2-6 KB of internal RAM.
#define FMRB_APP_SPAWN_OVERHEAD         (8 * 1024)

// Internal RAM left for the rest of the machine after an app is started. A
// spawn that would eat into this is refused (app_internal_ram_available).
// 30 KB is a floor to keep drivers and the network stack able to allocate, not
// a comfort margin -- WiFi and the remote desktop alone move internal RAM by
// tens of KB while apps run.
#define FMRB_APP_SPAWN_MARGIN           (30 * 1024)
#define FMRB_USER_APP_PRIORITY          (2)
#define FMRB_USER_APP_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_1

// Service host task (doc/user_extension/services/plan.md). One VM holding
// every resident user service, so a machine with three of them pays for one
// task instead of three.
//
// One step BELOW a user app on purpose. Services are background work and must
// never make the app in front stutter: at the same priority the scheduler
// hands them slices round-robin, and a service handler would take them out of
// a game's frame. At 1 they run in the gaps an app leaves, and an app leaves
// one on every on_update (it sleeps in _spin). The other half of that bargain
// is that a foreground app spinning at 100% delays the services, which is the
// stated order of priorities; anything that cannot tolerate the delay (a MIDI
// router) belongs in an app of its own, not here.
//
// The stack is the user-app floor plus room for the compiler: the host reads
// its services with require at boot, and mruby's codegen recurses on the C
// stack (a 16 KB app has been taken down by compiling a file, see
// FMRB_USER_APP_TASK_STACK_MAX above). Tune this down from the measured
// high-water in the periodic fmrb_task: dump, not by guessing.
//
// Measured on a Tab5 with the three bundled services: high-water 10,128 B
// of 24 KB (doc/user_extension/services/report/s1.md), so 20 KB keeps
// ~10 KB of headroom for a user's deeper require. Internal RAM is the
// scarcest resource; raise this again only on a measured overflow
// (a Stack protection fault in the boot log names the task).
#define FMRB_SERVICE_APP_TASK_STACK_SIZE (20 * 1024)
#define FMRB_SERVICE_APP_PRIORITY       (1)
#define FMRB_SERVICE_APP_TASK_FLAGS     FMRB_TASK_FLAG_PINNED_1

// --- Infrastructure tasks (Core 0, internal RAM) ---

// RTC task (I2C, low priority)
#define FMRB_RTC_TASK_STACK_SIZE        (4096)
#define FMRB_RTC_TASK_PRIORITY          (3)
#define FMRB_RTC_TASK_FLAGS             FMRB_TASK_FLAG_PINNED_1

// Status LED task (GPIO). Sits in the control/liveness tier with the RTC and
// the debugger so it keeps beating while a guest spins: "blinking" has to mean
// "the OS is alive", which it cannot if a busy app can starve it.
#define FMRB_STATUS_LED_TASK_STACK_SIZE (4096)
#define FMRB_STATUS_LED_TASK_PRIORITY   (3)
#define FMRB_STATUS_LED_TASK_FLAGS      FMRB_TASK_FLAG_PINNED_1

// USB host library task (USB DMA needs internal RAM)
#define FMRB_USB_HOST_TASK_STACK_SIZE   (4096)
#define FMRB_USB_HOST_TASK_PRIORITY     (5)
#define FMRB_USB_HOST_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_0

// USB HID host task
// Increased 4KB -> 5KB: the M-2 session (S3, 2026-08-02) peaked at 2.9KB
// (device replug included), leaving only 1.1KB -- the thinnest margin of any
// task, on a path (enumeration) with third-party driver frames. Never shrink
// this below the measured peak + ~2KB.
#define FMRB_USB_HID_TASK_STACK_SIZE    (5 * 1024)
#define FMRB_USB_HID_TASK_PRIORITY      (5)
#define FMRB_USB_HID_TASK_FLAGS         FMRB_TASK_FLAG_PINNED_0

// SPI connection check task (SPI slave)
#define FMRB_SPI_CONN_TASK_STACK_SIZE   (4096)
#define FMRB_SPI_CONN_TASK_PRIORITY     (5)
#define FMRB_SPI_CONN_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_1

// HW proxy task (internal RAM, handles file I/O for PSRAM tasks)
// M-2 session (S3, 2026-08-02) peaked at 2.0KB with file traffic flowing;
// 6KB keeps a 2x margin over that. Note the proxied-file-I/O role is dormant
// while PSRAM task stacks stay banned -- re-measure if that ever returns.
#define FMRB_HW_PROXY_TASK_STACK_SIZE   (6 * 1024)
#define FMRB_HW_PROXY_TASK_PRIORITY     (6)
#define FMRB_HW_PROXY_TASK_FLAGS        FMRB_TASK_FLAG_PINNED_0

// BLE task (managed by NimBLE, config referenced in sdkconfig)
// NOTE: the "nimble_host" task itself is created by the NimBLE port at its
// default priority 4, pinned to core 1 by sdkconfig. This header cannot
// change it; treat 4 as an anchor when assigning tiers (doc/reference/task_priority.md).
#define FMRB_BLE_TASK_STACK_SIZE        (4096)
#define FMRB_BLE_TASK_PRIORITY          (4)

// BLE bootstrap task (runs the NimBLE start sequence once, then exits)
#define FMRB_BLE_START_TASK_STACK_SIZE  (6144)
#define FMRB_BLE_START_TASK_PRIORITY    (4)

// BLE file service processing task (file I/O needs internal RAM for flash DMA)
// Stays at 8KB: a real BLE file transfer in the M-2 session (S3, 2026-08-02)
// peaked at 5.3KB -- the idle figure (~2KB) that once suggested 6KB is off by
// more than the margin 6KB would leave. Do not shrink without re-measuring a
// transfer.
#define FMRB_BLE_FS_TASK_STACK_SIZE     (8 * 1024)
#define FMRB_BLE_FS_TASK_PRIORITY       (4)
#define FMRB_BLE_FS_TASK_FLAGS          FMRB_TASK_FLAG_PINNED_0

// Remote debugger daemon task (msgpack over BLE GATT / TCP on Linux)
// Not pinned: it is latency-tolerant and runs at a low priority, so either core
// may take it. The frame and log line buffers are static (see fmrb_debugd.c),
// so only the command dispatch chain lives here. The attached path is now
// measured (M-2 session, S3, 2026-08-02): an attach + web-console session
// dipped to 2,580 B free at the spawn-from-file call (handle_spawn ->
// fmrb_default_apps), i.e. a >=3.6KB peak. 6KB stands; 4-5KB would leave
// almost nothing on that path.
#define FMRB_DEBUGD_TASK_STACK_SIZE     (6 * 1024)
#define FMRB_DEBUGD_TASK_PRIORITY       (3)
#define FMRB_DEBUGD_TASK_FLAGS          FMRB_TASK_FLAG_NONE

// M5GFX receiver task (GFX commands via local Message Buffer)
#define FMRB_M5GFX_TASK_STACK_SIZE      (8 * 1024)
#define FMRB_M5GFX_TASK_PRIORITY        (6)
#define FMRB_M5GFX_TASK_FLAGS           FMRB_TASK_FLAG_PINNED_0

// I2C keyboard polling task (ATOM_DISPLAY mode)
#define FMRB_I2C_KBD_TASK_STACK_SIZE    (4096)
#define FMRB_I2C_KBD_TASK_PRIORITY      (5)
#define FMRB_I2C_KBD_TASK_FLAGS         FMRB_TASK_FLAG_PINNED_0

// --- Driver tasks created with raw xTaskCreatePinnedToCore ---
// These use explicit _CORE macros instead of FMRB_TASK_FLAG_* because their
// creation sites do not go through fmrb_task_create_ex.

// Tab5 display task (MIPI-DSI compositor, receives GFX commands via
// hal_link_local; owns PPA blend/SRM and the boot screen)
#define FMRB_DISPLAY_P4_TASK_STACK_SIZE (16 * 1024)
#define FMRB_DISPLAY_P4_TASK_PRIORITY   (5)
#define FMRB_DISPLAY_P4_TASK_CORE       (1)

// Motion-JPEG player (Tab5: reads the file, drives the JPEG decoder, hands
// finished frames to the display task). Below the display task on purpose --
// a late frame must never delay compositing. Core 0 keeps the file reads and
// the decoder wait off the core the compositor runs on.
#define FMRB_VIDEO_P4_TASK_STACK_SIZE   (4096)
#define FMRB_VIDEO_P4_TASK_PRIORITY     (4)
#define FMRB_VIDEO_P4_TASK_CORE         (0)

// Audio task (ESP32-P4 Modern: NTSC-timed APU emulation feed)
#define FMRB_AUDIO_P4_TASK_STACK_SIZE   (8192)
#define FMRB_AUDIO_P4_TASK_PRIORITY     (6)
#define FMRB_AUDIO_P4_TASK_CORE         (0)

// Touch input polling task (Tab5)
#define FMRB_TOUCH_TASK_STACK_SIZE      (4096)
#define FMRB_TOUCH_TASK_PRIORITY        (5)
#define FMRB_TOUCH_TASK_CORE            (1)

// Tab5 I2C keyboard polling task
#define FMRB_TAB5_KBD_TASK_STACK_SIZE   (4096)
#define FMRB_TAB5_KBD_TASK_PRIORITY     (5)
#define FMRB_TAB5_KBD_TASK_CORE         (0)

// Remote desktop (H.264 / MJPEG streaming and its bootstrap)
#define FMRB_RD_STREAM_TASK_STACK_SIZE  (8192)
#define FMRB_RD_STREAM_TASK_PRIORITY    (4)
#define FMRB_RD_STREAM_TASK_CORE        (1)
#define FMRB_RD_MJPEG_TASK_STACK_SIZE   (8192)
#define FMRB_RD_MJPEG_TASK_PRIORITY     (4)
#define FMRB_RD_MJPEG_TASK_CORE         (0)
#define FMRB_RD_START_TASK_STACK_SIZE   (4096)
#define FMRB_RD_START_TASK_PRIORITY     (4)
// HTTP server task is created by esp_http_server, not fmrb_task_create;
// these values are handed to httpd_config_t in rd_http.c.
#define FMRB_RD_HTTPD_TASK_STACK_SIZE   (8192)
#define FMRB_RD_HTTPD_TASK_PRIORITY     (5)
#define FMRB_RD_HTTPD_TASK_CORE         (0)

// --- Linux simulation driver tasks ---

// USB HID injection receiver (reads the fmrb_inject socket)
#define FMRB_USB_RX_TASK_STACK_SIZE     (4096)
#define FMRB_USB_RX_TASK_PRIORITY       (5)

// Socket link receiver (one per channel, graphics/audio transport)
#define FMRB_LINK_RX_TASK_STACK_SIZE    (4096)
#define FMRB_LINK_RX_TASK_PRIORITY      (5)

// ============================================================
// Message Queue Lengths
// ============================================================

// HOST task: High-frequency graphics commands, HID events from USB
#define FMRB_HOST_MSG_QUEUE_LEN (128)

// HOST task message queue management
#define FMRB_HOST_HID_RESERVED_SLOTS (32)
#define FMRB_HOST_GFX_AVAILABLE_SLOTS (FMRB_HOST_MSG_QUEUE_LEN - FMRB_HOST_HID_RESERVED_SLOTS)

// KERNEL task: HID events, window management, app lifecycle.
// 64 matches the app queues (P7.8): this queue also carries the MIDI note
// stream (hundreds of msgs/s) plus every forwarded HID event, and at 32 it
// was the first to jam when the desktop stalled in a GC storm -- HOST then
// blocked on it and input/audio froze together. ~180B per slot, so the
// extra 32 slots cost ~5.6KB of internal RAM.
#define FMRB_KERNEL_MSG_QUEUE_LEN (64)

// SYSTEM_APP task: System-level application messages
#define FMRB_SYSTEM_APP_MSG_QUEUE_LEN (64)

// USER_APP tasks: User application messages.
// 64 absorbs ~2s of the 30Hz mouse-move stream while an app is busy (a GC
// step or a long redraw); at 32 the queue filled in ~1s and the kernel's
// blocking HID forward stalled the whole input path (doc/midi/report/p7_6.md
// 6.2). Item storage is PSRAM (fmrb_msg.c), so this costs no internal RAM.
#define FMRB_USER_APP_MSG_QUEUE_LEN (64)

// Default queue length for unconfigured tasks
#define FMRB_DEFAULT_MSG_QUEUE_LEN (10)

// ============================================================
// Process IDs
// ============================================================
typedef enum FMRB_PROC_ID{
    PROC_ID_KERNEL = 0,
    PROC_ID_HOST,
    PROC_ID_SYSTEM_APP,
    PROC_ID_SYSTEM_OVERLAY,
    // User app slots. There is one memory pool per slot (POOL_ID_USER_APP0 and
    // up) and one context slot per id (FMRB_MAX_APPS); naming ids beyond that
    // is what let a spawn hand out a slot index past the end of the context
    // pool, so the assert in main/app/fmrb_app.c has to stay true.
    //
    // The first five are named because other code refers to them (the FmrbConst
    // exports in picoruby-fmrb-const); a build that raises FMRB_MAX_APPS gets
    // the rest as unnamed ids up to PROC_ID_MAX, which follows the ceiling.
    //
    // The count is a ceiling, not a promise: what actually limits concurrent
    // apps is memory and the `max_apps` setting, and a spawn is refused when
    // either says so (the internal RAM check in fmrb_app_spawn_app, and the
    // PSRAM the slot's pool needs).
    PROC_ID_USER_APP0,
    PROC_ID_USER_APP1,
    PROC_ID_USER_APP2,
    PROC_ID_USER_APP3,
    PROC_ID_USER_APP4,
    PROC_ID_MAX = FMRB_MAX_APPS
} fmrb_proc_id_t;

/**
 * @brief One past the last usable user app process id.
 *
 * Equals PROC_ID_MAX and FMRB_MAX_APPS while the three agree, which is the
 * point: a scan over user app slots says what it means and cannot outrun the
 * context pool. main/app/fmrb_app.c asserts the identity at the pool itself.
 */
#define PROC_ID_USER_APP_END (PROC_ID_USER_APP0 + FMRB_MAX_USER_APPS)
