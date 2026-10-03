#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the per-task CPU usage sampler (doc/reference/cpu_usage.md).
 *
 * Measurement only. It does something only in a build whose sdkconfig has
 * CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y, which the repository defaults do
 * not set; otherwise it is an empty function. Every 5 s it prints the busy
 * share of each core and the share of every task as "cpu:" log lines.
 */
void cpu_stats_start(void);

#ifdef __cplusplus
}
#endif
