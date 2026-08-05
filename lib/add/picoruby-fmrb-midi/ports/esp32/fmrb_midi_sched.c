/*
 * Timed command queue for MIDI playback. See include/fmrb_midi_sched.h for
 * why this exists; this file is the ring, the timer and the two sinks.
 *
 * The wake-up source differs between the targets and nothing else does:
 *
 *   ESP32  esp_timer, one-shot, re-armed after every batch. Its task is
 *          already running (picoruby-machine uses it), so this costs no new
 *          stack; it sits at priority 22 on core 0, above every Ruby task
 *          and off the core the apps run on.
 *   Linux  esp_timer registers headers only for the linux target (its
 *          CMakeLists compiles no sources there), so the simulation uses a
 *          small task instead. A POSIX timer is not an option: the FreeRTOS
 *          simulator drives its own scheduler from SIGALRM.
 *
 * Both call fire_due(), so the queue, the ordering and the statistics are
 * the same code on both.
 */

#include "fmrb_midi_sched.h"
#include "fmrb_midi_serial.h"
#include "picoruby_fmrb_app.h"

#include "fmrb_hal_time.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_timer.h"
#endif

static const char *TAG = "midi_sched";

typedef struct {
    uint64_t due_us;
    uint8_t kind;
    uint8_t src_pid;
    uint8_t payload[FMRB_MIDI_SCHED_PAYLOAD];
} fmrb_midi_cmd_t;

static fmrb_midi_cmd_t s_ring[FMRB_MIDI_SCHED_CAPACITY];
static volatile int s_head; /* next to fire */
static volatile int s_tail; /* next free slot */
static fmrb_spinlock_t s_lock = FMRB_SPINLOCK_INITIALIZER;
static fmrb_midi_sched_stats_t s_stats;
static bool s_started;

/* Firing a command a hair early is better than re-arming the timer for the
 * last few microseconds of a wait, and no listener can tell. */
#define FIRE_SLACK_US 200

#ifndef CONFIG_IDF_TARGET_LINUX
static esp_timer_handle_t s_timer;
#else
static TaskHandle_t s_task;
static SemaphoreHandle_t s_wake;
/* The simulation has room for a stack; the device does not spend one (it
 * borrows the esp_timer task instead). */
#define SCHED_TASK_STACK 4096
#define SCHED_TASK_PRIO 20
#endif

static void arm_timer(void);

/* --- ring ------------------------------------------------------------- */

static int ring_depth_locked(void)
{
    int d = s_tail - s_head;
    return d < 0 ? d + FMRB_MIDI_SCHED_CAPACITY : d;
}

int fmrb_midi_sched_depth(void)
{
    fmrb_enter_critical(&s_lock);
    int d = ring_depth_locked();
    fmrb_exit_critical(&s_lock);
    return d;
}

int fmrb_midi_sched_free(void)
{
    /* One slot is always left empty so full and empty stay distinguishable. */
    return FMRB_MIDI_SCHED_CAPACITY - 1 - fmrb_midi_sched_depth();
}

/* --- sinks ------------------------------------------------------------ */
/*
 * Both are safe to call from the timer: neither enters mruby, and neither
 * blocks for long. The audio one hands a message to the kernel queue; the
 * serial one writes at most three bytes into a UART that has room for
 * hundreds.
 */
static bool send_command(const fmrb_midi_cmd_t *cmd)
{
    switch (cmd->kind) {
    case FMRB_MIDI_CMD_APU_NOTE: {
        int freq = cmd->payload[1] | (cmd->payload[2] << 8);
        return fmrb_app_send_audio_note(cmd->src_pid, true, cmd->payload[0],
                                        freq, cmd->payload[3], cmd->payload[4],
                                        cmd->payload[5], 0) == (int)FMRB_OK;
    }
    case FMRB_MIDI_CMD_APU_OFF:
        return fmrb_app_send_audio_note(cmd->src_pid, false, cmd->payload[0],
                                        0, 0, 0, 0, 0) == (int)FMRB_OK;
    case FMRB_MIDI_CMD_SERIAL: {
        size_t len = cmd->payload[0];
        if (len == 0 || len > 3) {
            return false;
        }
        return fmrb_midi_serial_write(&cmd->payload[1], len) == FMRB_OK;
    }
    default:
        return false;
    }
}

/* --- firing ----------------------------------------------------------- */

/* Send everything that has come due, then arm for the next one. Runs on the
 * timer, never on a Ruby task. */
static void fire_due(void)
{
    while (true) {
        fmrb_midi_cmd_t cmd;
        uint64_t now = fmrb_hal_time_get_us();

        fmrb_enter_critical(&s_lock);
        if (s_head == s_tail) {
            fmrb_exit_critical(&s_lock);
            break;
        }
        if (s_ring[s_head].due_us > now + FIRE_SLACK_US) {
            fmrb_exit_critical(&s_lock);
            break;
        }
        cmd = s_ring[s_head];
        s_head = (s_head + 1) % FMRB_MIDI_SCHED_CAPACITY;
        fmrb_exit_critical(&s_lock);

        uint32_t late = (now > cmd.due_us) ? (uint32_t)(now - cmd.due_us) : 0;
        if (!send_command(&cmd)) {
            s_stats.send_failed++;
        }
        s_stats.fired++;
        s_stats.late_sum_us += late;
        if (late > s_stats.late_max_us) {
            s_stats.late_max_us = late;
        }
    }

    arm_timer();
}

/* Microseconds until the next command, 0 if one is already due, and
 * UINT64_MAX when there is nothing to wait for. */
static uint64_t next_delay_us(void)
{
    uint64_t due;

    fmrb_enter_critical(&s_lock);
    if (s_head == s_tail) {
        fmrb_exit_critical(&s_lock);
        return UINT64_MAX;
    }
    due = s_ring[s_head].due_us;
    fmrb_exit_critical(&s_lock);

    uint64_t now = fmrb_hal_time_get_us();
    return (due <= now + FIRE_SLACK_US) ? 0 : (due - now);
}

#ifndef CONFIG_IDF_TARGET_LINUX

static void timer_cb(void *arg)
{
    (void)arg;
    fire_due();
}

static void arm_timer(void)
{
    if (s_timer == NULL) {
        return;
    }
    uint64_t delay = next_delay_us();
    esp_timer_stop(s_timer); /* harmless when it is not running */
    if (delay == UINT64_MAX) {
        return;
    }
    esp_timer_start_once(s_timer, delay == 0 ? 1 : delay);
}

fmrb_err_t fmrb_midi_sched_start(void)
{
    if (s_started) {
        return FMRB_OK;
    }
    const esp_timer_create_args_t args = {
        .callback = timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "midi_sched",
    };
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        FMRB_LOGE(TAG, "esp_timer_create failed");
        return FMRB_ERR_INVALID_STATE;
    }
    s_started = true;
    FMRB_LOGI(TAG, "MIDI scheduler ready (esp_timer, %d slots)",
              FMRB_MIDI_SCHED_CAPACITY);
    return FMRB_OK;
}

#else /* CONFIG_IDF_TARGET_LINUX */

static void sched_task(void *arg)
{
    (void)arg;
    while (true) {
        uint64_t delay = next_delay_us();
        if (delay > 0) {
            /* Wake early when a push lands in front of what we are waiting
             * for; otherwise sleep out the wait. A full second is only ever
             * reached when the queue is empty. */
            TickType_t ticks = (delay == UINT64_MAX)
                                   ? FMRB_MS_TO_TICKS(1000)
                                   : FMRB_MS_TO_TICKS((delay + 999) / 1000);
            if (ticks == 0) {
                ticks = 1;
            }
            xSemaphoreTake(s_wake, ticks);
        }
        fire_due();
    }
}

static void arm_timer(void)
{
    /* The task re-reads the head every round; a push wakes it (see push). */
}

fmrb_err_t fmrb_midi_sched_start(void)
{
    if (s_started) {
        return FMRB_OK;
    }
    s_wake = xSemaphoreCreateBinary();
    if (s_wake == NULL) {
        return FMRB_ERR_NO_MEMORY;
    }
    if (fmrb_task_create_ex(sched_task, "midi_sched", SCHED_TASK_STACK, NULL,
                            SCHED_TASK_PRIO, &s_task,
                            FMRB_TASK_FLAG_NONE) != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to start the scheduler task");
        return FMRB_ERR_INVALID_STATE;
    }
    s_started = true;
    FMRB_LOGI(TAG, "MIDI scheduler ready (task, %d slots)",
              FMRB_MIDI_SCHED_CAPACITY);
    return FMRB_OK;
}

#endif /* CONFIG_IDF_TARGET_LINUX */

/* --- producer --------------------------------------------------------- */

bool fmrb_midi_sched_push(uint64_t due_us, uint8_t kind, uint8_t src_pid,
                          const uint8_t *payload)
{
    if (fmrb_midi_sched_start() != FMRB_OK) {
        return false;
    }

    bool first;
    fmrb_enter_critical(&s_lock);
    int next = (s_tail + 1) % FMRB_MIDI_SCHED_CAPACITY;
    if (next == s_head) {
        fmrb_exit_critical(&s_lock);
        s_stats.dropped++;
        return false;
    }
    s_ring[s_tail].due_us = due_us;
    s_ring[s_tail].kind = kind;
    s_ring[s_tail].src_pid = src_pid;
    memcpy(s_ring[s_tail].payload, payload, FMRB_MIDI_SCHED_PAYLOAD);
    first = (s_head == s_tail);
    s_tail = next;
    int depth = ring_depth_locked();
    fmrb_exit_critical(&s_lock);

    s_stats.pushed++;
    if ((uint32_t)depth > s_stats.depth_max) {
        s_stats.depth_max = (uint32_t)depth;
    }

    /* Only the first command of a run needs the timer re-armed: the queue is
     * filled in time order, so anything after it is later than what the
     * timer is already waiting for. */
    if (first) {
#ifndef CONFIG_IDF_TARGET_LINUX
        arm_timer();
#else
        xSemaphoreGive(s_wake);
#endif
    }
    return true;
}

void fmrb_midi_sched_clear(void)
{
    fmrb_enter_critical(&s_lock);
    s_head = 0;
    s_tail = 0;
    fmrb_exit_critical(&s_lock);
}

void fmrb_midi_sched_clear_pid(uint8_t src_pid)
{
    bool had_apu = false;
    bool had_serial = false;

    /* Compacts in place: what belongs to other apps keeps its order and its
     * due times. */
    fmrb_enter_critical(&s_lock);
    int read = s_head;
    int write = s_head;
    while (read != s_tail) {
        if (s_ring[read].src_pid == src_pid) {
            if (s_ring[read].kind == FMRB_MIDI_CMD_SERIAL) {
                had_serial = true;
            } else {
                had_apu = true;
            }
        } else {
            if (write != read) {
                s_ring[write] = s_ring[read];
            }
            write = (write + 1) % FMRB_MIDI_SCHED_CAPACITY;
        }
        read = (read + 1) % FMRB_MIDI_SCHED_CAPACITY;
    }
    s_tail = write;
    fmrb_exit_critical(&s_lock);

    /* What was dropped includes the note offs that would have ended whatever
     * is sounding now, so this has to end it instead. Doing it here rather
     * than leaving it to the caller is what makes a killed app safe: the
     * kernel silences an app's voices when it sees the app go, and if that
     * happens before the last queued note on fires, the note sounds forever
     * (observed in the simulation, doc/midi/report/p7_6.md). Silence emitted
     * from here is always after the last command that could start a note. */
    if (had_apu) {
        for (int voice = 0; voice < 4; voice++) {
            fmrb_app_send_audio_note(src_pid, false, voice, 0, 0, 0, 0, 0);
        }
    }
    if (had_serial) {
        for (int channel = 0; channel < 16; channel++) {
            uint8_t off[3] = { (uint8_t)(0xB0 | channel), 123, 0 }; /* all notes off */
            fmrb_midi_serial_write(off, sizeof(off));
        }
    }
}

void fmrb_midi_sched_get_stats(fmrb_midi_sched_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_stats;
}

void fmrb_midi_sched_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

uint64_t fmrb_midi_sched_now_us(void)
{
    return fmrb_hal_time_get_us();
}

/* --- Ruby binding -----------------------------------------------------
 *
 * Positional arguments only, and integers only, so pushing a command
 * allocates nothing (doc/midi/report/p6.md is about exactly that). The
 * player pushes hundreds of these a second.
 */

#include <mruby.h>
#include <mruby/hash.h>
#include <mruby/presym.h>
#include <mruby/variable.h>

#include "fmrb_app.h"

static uint8_t current_pid(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (uint8_t)ctx->app_id : 0;
}

static mrb_value mrb_sched_push_apu_note(mrb_state *mrb, mrb_value self)
{
    mrb_int due, ch, freq, vol, duty, sweep;
    mrb_get_args(mrb, "iiiiii", &due, &ch, &freq, &vol, &duty, &sweep);

    uint8_t p[FMRB_MIDI_SCHED_PAYLOAD];
    p[0] = (uint8_t)ch;
    p[1] = (uint8_t)(freq & 0xFF);
    p[2] = (uint8_t)((freq >> 8) & 0xFF);
    p[3] = (uint8_t)vol;
    p[4] = (uint8_t)duty;
    p[5] = (uint8_t)sweep;
    return mrb_bool_value(fmrb_midi_sched_push((uint64_t)due,
                                               FMRB_MIDI_CMD_APU_NOTE,
                                               current_pid(), p));
}

static mrb_value mrb_sched_push_apu_off(mrb_state *mrb, mrb_value self)
{
    mrb_int due, ch;
    mrb_get_args(mrb, "ii", &due, &ch);

    uint8_t p[FMRB_MIDI_SCHED_PAYLOAD] = { (uint8_t)ch, 0, 0, 0, 0, 0 };
    return mrb_bool_value(fmrb_midi_sched_push((uint64_t)due,
                                               FMRB_MIDI_CMD_APU_OFF,
                                               current_pid(), p));
}

static mrb_value mrb_sched_push_serial(mrb_state *mrb, mrb_value self)
{
    mrb_int due, len, b1, b2, b3;
    mrb_get_args(mrb, "iiiii", &due, &len, &b1, &b2, &b3);

    uint8_t p[FMRB_MIDI_SCHED_PAYLOAD];
    p[0] = (uint8_t)len;
    p[1] = (uint8_t)b1;
    p[2] = (uint8_t)b2;
    p[3] = (uint8_t)b3;
    p[4] = 0;
    p[5] = 0;
    return mrb_bool_value(fmrb_midi_sched_push((uint64_t)due,
                                               FMRB_MIDI_CMD_SERIAL,
                                               current_pid(), p));
}

/* Only this app's commands: another app playing at the same time keeps its
 * own future. */
static mrb_value mrb_sched_clear(mrb_state *mrb, mrb_value self)
{
    fmrb_midi_sched_clear_pid(current_pid());
    return mrb_nil_value();
}

static mrb_value mrb_sched_depth(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(fmrb_midi_sched_depth());
}

static mrb_value mrb_sched_free(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(fmrb_midi_sched_free());
}

static mrb_value mrb_sched_now_us(mrb_state *mrb, mrb_value self)
{
    return mrb_int_value(mrb, (mrb_int)fmrb_midi_sched_now_us());
}

static mrb_value mrb_sched_reset_stats(mrb_state *mrb, mrb_value self)
{
    fmrb_midi_sched_reset_stats();
    return mrb_nil_value();
}

/* Called once every few seconds for a log line, so a Hash is fine here. */
static mrb_value mrb_sched_stats(mrb_state *mrb, mrb_value self)
{
    fmrb_midi_sched_stats_t st;
    fmrb_midi_sched_get_stats(&st);

    mrb_value h = mrb_hash_new_capa(mrb, 8);
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "fired")),
                 mrb_int_value(mrb, (mrb_int)st.fired));
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "pushed")),
                 mrb_int_value(mrb, (mrb_int)st.pushed));
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "dropped")),
                 mrb_int_value(mrb, (mrb_int)st.dropped));
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "send_failed")),
                 mrb_int_value(mrb, (mrb_int)st.send_failed));
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "late_sum_us")),
                 mrb_int_value(mrb, (mrb_int)st.late_sum_us));
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "late_max_us")),
                 mrb_int_value(mrb, (mrb_int)st.late_max_us));
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "depth_max")),
                 mrb_int_value(mrb, (mrb_int)st.depth_max));
    return h;
}

void mrb_fmrb_midi_sched_init(mrb_state *mrb)
{
    /* Start the timer here rather than on the first push: creating it takes
     * long enough that the first notes of a song would go out late (measured
     * at 92 ms in the simulation, which is the very thing this queue exists
     * to remove). It is global and idempotent, so the first VM to load the
     * gem pays for it and the rest find it running. */
    fmrb_midi_sched_start();

    struct RClass *module = mrb_define_module(mrb, "FmrbMidi");
    struct RClass *klass = mrb_define_class_under(mrb, module, "Sched",
                                                  mrb->object_class);

    mrb_define_class_method(mrb, klass, "_push_apu_note", mrb_sched_push_apu_note, MRB_ARGS_REQ(6));
    mrb_define_class_method(mrb, klass, "_push_apu_off", mrb_sched_push_apu_off, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, klass, "_push_serial", mrb_sched_push_serial, MRB_ARGS_REQ(5));
    mrb_define_class_method(mrb, klass, "_clear", mrb_sched_clear, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, klass, "_depth", mrb_sched_depth, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, klass, "_free", mrb_sched_free, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, klass, "_now_us", mrb_sched_now_us, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, klass, "_stats", mrb_sched_stats, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, klass, "_reset_stats", mrb_sched_reset_stats, MRB_ARGS_NONE());
}

/* A dying app must not keep playing: drop what it queued when its VM goes.
 * Called from the gem finalizer, which runs on the app's own task. */
void mrb_fmrb_midi_sched_final(mrb_state *mrb)
{
    (void)mrb;
    fmrb_midi_sched_clear_pid(current_pid());
}
