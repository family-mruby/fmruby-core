/*
 * Timed command queue for MIDI playback.
 *
 * Ruby decides what should sound and when; this fires it. A player hands in
 * commands a few hundred milliseconds ahead of time, each stamped with the
 * microsecond it is due, and a timer sends them at that microsecond without
 * entering the VM at all.
 *
 * Why: before this, a note went out when the app's Ruby task happened to
 * wake up, so the beat carried every hesitation of the app scheduler - the
 * device measured 17-20 ms of average lateness with 71 ms peaks, well past
 * where a listener hears the rhythm wobble (doc/midi/report/p7_6.md). The
 * FMSQ path never had the problem because the consumer owns the clock.
 * This gives live MIDI the same property.
 *
 * What it deliberately does not do: any musical thinking. Voice assignment,
 * chord resolution and the note tables stay in Ruby, and what arrives here
 * is already the final register write. This is a dumb device that can tell
 * the time.
 *
 * Threading: one producer (the app's Ruby task) and one consumer (the timer),
 * with a spinlock over the ring indices. Nothing here touches mruby, so the
 * consumer can run at a priority above every Ruby task.
 */

#ifndef FMRB_MIDI_SCHED_H
#define FMRB_MIDI_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 128 entries of 16 bytes. A few hundred milliseconds of dense playing is
 * tens of commands, so this is roomy; the producer checks the free space
 * before decoding more, so a full queue costs a short wait, not a note. */
#define FMRB_MIDI_SCHED_CAPACITY 128

/* Payload bytes per command, enough for the widest one (an APU note:
 * channel, frequency as two bytes, volume, duty, sweep). */
#define FMRB_MIDI_SCHED_PAYLOAD 6

typedef enum {
    FMRB_MIDI_CMD_NONE = 0,
    FMRB_MIDI_CMD_APU_NOTE,  /* ch, freq_lo, freq_hi, vol, duty, sweep */
    FMRB_MIDI_CMD_APU_OFF,   /* ch */
    FMRB_MIDI_CMD_SERIAL     /* len, b1, b2, b3 */
} fmrb_midi_cmd_kind_t;

typedef struct {
    uint32_t pushed;        /* commands accepted */
    uint32_t fired;         /* commands sent */
    uint32_t dropped;       /* rejected because the queue was full */
    uint32_t send_failed;   /* the sink refused it (queue full, UART error) */
    uint64_t late_sum_us;   /* how late the sends were, against the due time */
    uint32_t late_max_us;
    uint32_t depth_max;     /* high water of the queue itself */
} fmrb_midi_sched_stats_t;

/* Start the timer. Idempotent; called from the first push. */
fmrb_err_t fmrb_midi_sched_start(void);

/* Queue one command. Returns false when the queue is full (nothing is
 * dropped silently: the caller is expected to wait and try again). */
bool fmrb_midi_sched_push(uint64_t due_us, uint8_t kind, uint8_t src_pid,
                          const uint8_t *payload);

/* Drop everything still queued. Does NOT silence anything - the caller owns
 * that, and does it by sending note offs on the immediate path, which is
 * also what happens when an app stops a song. */
void fmrb_midi_sched_clear(void);

/* Drop what one app queued. Called when its VM closes, so a killed app
 * cannot keep playing. */
void fmrb_midi_sched_clear_pid(uint8_t src_pid);

int fmrb_midi_sched_depth(void);
int fmrb_midi_sched_free(void);

void fmrb_midi_sched_get_stats(fmrb_midi_sched_stats_t *out);
void fmrb_midi_sched_reset_stats(void);

/* The clock the due times are on. */
uint64_t fmrb_midi_sched_now_us(void);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_MIDI_SCHED_H */
