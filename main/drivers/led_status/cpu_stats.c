// Per-task CPU usage sampler (doc/core_alloc/report/c1.md).
//
// Measurement only, and only in a build with
// CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y (a trial build with an extra
// sdkconfig defaults file; the repository defaults leave it off, and then this
// file compiles to an empty cpu_stats_start).
//
// Every CPU_STATS_INTERVAL_MS it takes uxTaskGetSystemState and prints the
// difference against the previous sample:
//   cpu: win=<ms> c0=<busy%> c1=<busy%> gone=<%> n=<tasks> probe=<us>
//   cpu: t <name>/<core> <% of one core> ...   (largest first, >= 0.1%)
// c0/c1 are 100% minus that core's IDLE task. <core> is the task's affinity
// ('-' = not pinned). "gone" is run time that no listed task accounts for:
// tasks deleted during the window, plus the slices still in progress at the
// sample (those land in the next window). probe is the time spent inside
// uxTaskGetSystemState, which walks every task (stack high-water included)
// with the scheduler suspended on this core.
//
// The run-time counter is esp_timer (1 MHz) and is charged at context
// switches, so interrupt time is billed to whichever task it interrupted.

#include "cpu_stats.h"
#include "sdkconfig.h"

#if defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && !defined(CONFIG_IDF_TARGET_LINUX)

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_attr.h"
#include "fmrb_task_config.h"

static const char *TAG = "cpu";

#define CPU_STATS_INTERVAL_MS 5000
#define CPU_STATS_MAX_TASKS   64
#define CPU_STATS_PER_LINE    6

typedef struct {
    UBaseType_t num;   // xTaskNumber: unique per task, never reused
    uint64_t    rt;    // run-time counter at the previous sample
} prev_entry_t;

typedef struct {
    char        name[configMAX_TASK_NAME_LEN];
    int         core;  // affinity, -1 = not pinned
    uint32_t    permille;
} row_t;

FMRB_EXT_RAM_BSS_ATTR static TaskStatus_t s_cur[CPU_STATS_MAX_TASKS];
FMRB_EXT_RAM_BSS_ATTR static prev_entry_t s_prev[CPU_STATS_MAX_TASKS];
FMRB_EXT_RAM_BSS_ATTR static row_t s_rows[CPU_STATS_MAX_TASKS];
static int s_prev_count = 0;
static uint64_t s_prev_total = 0;

static uint64_t prev_rt_of(UBaseType_t num, bool *found)
{
    for (int i = 0; i < s_prev_count; i++) {
        if (s_prev[i].num == num) {
            *found = true;
            return s_prev[i].rt;
        }
    }
    *found = false;
    return 0;
}

static void sample_and_log(void)
{
    uint64_t total = 0;
    int64_t p0 = esp_timer_get_time();
    configRUN_TIME_COUNTER_TYPE total_rt = 0;
    UBaseType_t n = uxTaskGetSystemState(s_cur, CPU_STATS_MAX_TASKS, &total_rt);
    int64_t p1 = esp_timer_get_time();
    total = (uint64_t)total_rt;

    TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCore(0);
    TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCore(1);

    uint64_t win = total - s_prev_total;
    bool first = (s_prev_total == 0);
    uint64_t idle_d[2] = {0, 0};
    uint64_t sum = 0;
    int rows = 0;

    for (UBaseType_t i = 0; i < n; i++) {
        TaskStatus_t *t = &s_cur[i];
        bool found = false;
        uint64_t prev = prev_rt_of(t->xTaskNumber, &found);
        uint64_t d = (uint64_t)t->ulRunTimeCounter - prev;
        sum += d;
        if (t->xHandle == idle0) idle_d[0] = d;
        if (t->xHandle == idle1) idle_d[1] = d;
        if (win > 0) {
            uint32_t pm = (uint32_t)((d * 1000ULL) / win);
            if (pm >= 1) {
#if configTASKLIST_INCLUDE_COREID
                BaseType_t c = t->xCoreID;
#else
                BaseType_t c = xTaskGetCoreID(t->xHandle);
#endif
                // Copy now: the name lives in the TCB, which a task deleted
                // before the log line would take with it.
                strncpy(s_rows[rows].name, t->pcTaskName, sizeof(s_rows[rows].name) - 1);
                s_rows[rows].name[sizeof(s_rows[rows].name) - 1] = '\0';
                s_rows[rows].core = (c == 0 || c == 1) ? (int)c : -1;
                s_rows[rows].permille = pm;
                rows++;
            }
        }
    }

    // Keep this sample as the base of the next window.
    for (UBaseType_t i = 0; i < n; i++) {
        s_prev[i].num = s_cur[i].xTaskNumber;
        s_prev[i].rt = (uint64_t)s_cur[i].ulRunTimeCounter;
    }
    s_prev_count = (int)n;
    s_prev_total = total;

    if (first || win == 0) return;

    // Largest first (insertion sort; a few dozen rows).
    for (int i = 1; i < rows; i++) {
        row_t r = s_rows[i];
        int j = i - 1;
        while (j >= 0 && s_rows[j].permille < r.permille) {
            s_rows[j + 1] = s_rows[j];
            j--;
        }
        s_rows[j + 1] = r;
    }

    uint32_t c0 = (idle_d[0] >= win) ? 0 : (uint32_t)(((win - idle_d[0]) * 1000ULL) / win);
    uint32_t c1 = (idle_d[1] >= win) ? 0 : (uint32_t)(((win - idle_d[1]) * 1000ULL) / win);
    int64_t gone_us = (int64_t)(2 * win) - (int64_t)sum;
    int32_t gone = (int32_t)((gone_us * 1000LL) / (int64_t)win);
    FMRB_LOGI(TAG, "cpu: win=%lums c0=%lu.%lu%% c1=%lu.%lu%% gone=%ld.%ld%% n=%u probe=%luus",
              (unsigned long)(win / 1000),
              (unsigned long)(c0 / 10), (unsigned long)(c0 % 10),
              (unsigned long)(c1 / 10), (unsigned long)(c1 % 10),
              (long)(gone / 10), (long)((gone < 0 ? -gone : gone) % 10),
              (unsigned)n, (unsigned long)(p1 - p0));

    char line[200];
    int pos = 0;
    for (int i = 0; i < rows; i++) {
        char core = (s_rows[i].core < 0) ? '-' : (char)('0' + s_rows[i].core);
        pos += snprintf(line + pos, sizeof(line) - pos, " %s/%c %lu.%lu",
                        s_rows[i].name, core,
                        (unsigned long)(s_rows[i].permille / 10),
                        (unsigned long)(s_rows[i].permille % 10));
        if ((i + 1) % CPU_STATS_PER_LINE == 0 || i == rows - 1 ||
            pos >= (int)sizeof(line) - 40) {
            FMRB_LOGI(TAG, "cpu: t%s", line);
            pos = 0;
            line[0] = '\0';
            // Each line is a synchronous console write; yield between them so
            // the log does not hold this core in one block.
            fmrb_task_delay_ms(1);
        }
    }
}

static void cpu_stats_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        sample_and_log();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CPU_STATS_INTERVAL_MS));
    }
}

void cpu_stats_start(void)
{
    fmrb_task_handle_t handle;
    fmrb_task_create_ex(cpu_stats_task, "cpustat",
                        FMRB_CPU_STATS_TASK_STACK_SIZE, NULL,
                        FMRB_CPU_STATS_TASK_PRIORITY, &handle,
                        FMRB_CPU_STATS_TASK_FLAGS);
    FMRB_LOGI(TAG, "cpu: run-time stats sampler started (every %d ms)",
              CPU_STATS_INTERVAL_MS);
}

#else

void cpu_stats_start(void)
{
}

#endif
