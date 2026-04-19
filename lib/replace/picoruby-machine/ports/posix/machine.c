#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

#ifdef __APPLE__
#include <unistd.h>
#include <uuid/uuid.h>
#endif

#include "../../include/machine.h"

int
hal_getchar(void)
{
  if (sigint_status == MACHINE_SIGINT_RECEIVED) {
    sigint_status = MACHINE_SIG_NONE;
    return 3; // Ctrl-C
  } else if (sigint_status == MACHINE_SIGTSTP_RECEIVED) {
    sigint_status = MACHINE_SIG_NONE;
    return 26; // Ctrl-Z
  }
  int c = getchar();
  if (c == EOF) {
    return -1;
  } else {
    return c;
  }
}

int
hal_read_available(void)
{
  int c = fgetc(stdin);
  return ungetc(c, stdin) == EOF ? 0 : 1;
}

void
Machine_tud_task(void)
{
}

bool
Machine_tud_mounted_q(void)
{
  return true;
}

void
Machine_delay_ms(uint32_t ms)
{
  if (ms == 0) return;

  /*
   * Why the chunked wall-clock loop?
   *
   * FreeRTOS-posix (used for the Linux simulation target) emulates task ticks
   * by installing an ITIMER_REAL at ~1 ms. That delivers SIGALRM to whichever
   * thread happens to be running, and any in-flight nanosleep() returns early
   * with EINTR at the very first tick - so a naive `nanosleep(ms, NULL)` here
   * returns after a single millisecond instead of the requested duration.
   *
   * The standard "loop on EINTR with the remaining time" idiom also does not
   * work reliably: some platforms clamp the returned remainder, and we were
   * observed to exit early under heavy SIGALRM load.
   *
   * Instead we record an absolute wake-up time via CLOCK_MONOTONIC and sleep
   * in short (10 ms) chunks, rechecking the clock on every iteration. Each
   * chunk may be cut short by SIGALRM, but the outer loop terminates only
   * when wall-clock time actually reaches the target - so the function always
   * sleeps at least `ms` milliseconds regardless of signal activity. The
   * second nanosleep() argument is NULL because we do not care about the
   * remainder; the wall-clock check is the source of truth.
   *
   * Trade-off: accuracy is bounded by the chunk size (~10 ms over-sleep in
   * the worst case) plus scheduler jitter. This is intentional - the caller
   * is asking for a coarse delay, not precision timing.
   */

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  uint64_t target_ns = (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec
                     + (uint64_t)ms * 1000000ULL;

  const struct timespec chunk = { .tv_sec = 0, .tv_nsec = 10 * 1000000 }; /* 10 ms */
  for (;;) {
    struct timespec c = chunk;
    nanosleep(&c, NULL); /* EINTR is fine; the wall-clock check below is authoritative. */

    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
    if (now_ns >= target_ns) break;
  }
}

void
Machine_busy_wait_ms(uint32_t ms)
{
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

void
Machine_busy_wait_us(uint32_t us)
{
  struct timespec ts;
  ts.tv_sec = us / 1000000;
  ts.tv_nsec = (us % 1000000) * 1000;
  nanosleep(&ts, NULL);
}

void
Machine_sleep(uint32_t seconds)
{
}

bool
Machine_get_unique_id(char *id_str)
{
#ifdef __APPLE__
  uuid_t uuid;
  struct timespec timeout = {0, 0};
  if (gethostuuid(uuid, &timeout) == 0) {
    /* Convert 16-byte UUID to 32-character hex string */
    for (int i = 0; i < 16; i++) {
      sprintf(&id_str[i * 2], "%02x", uuid[i]);
    }
    id_str[32] = '\0';
    return true;
  }
  perror("Failed to get host UUID");
  return false;
#else
  FILE *fp = fopen("/etc/machine-id", "r");
  if (fp) {
    if (fgets(id_str, 33, fp) == NULL) {
      perror("Failed to read /etc/machine-id");
      fclose(fp);
      return false;
    }
    fclose(fp);
    return true;
  }
  perror("Failed to open /etc/machine-id");
  return false;
#endif
}

uint32_t
Machine_stack_usage(void)
{
  return 0;
}

void
Machine_exit(int status)
{
  exit(status);
}

void
Machine_reboot(void)
{
  exit_status = MACHINE_EXIT_REBOOT;
  exit(MACHINE_EXIT_REBOOT);
}

uint64_t
Machine_uptime_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

/* family-mruby: config query for boot diagnostics */
int
Machine_get_config_int(int type)
{
  switch(type)
  {
    case 0:
      return MRB_TICK_UNIT;
    case 1:
      return MRB_TIMESLICE_TICK_COUNT;
  }
  return 0;
}
