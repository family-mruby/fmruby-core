#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Directory the boot-time measurement runs against. /app/demo is the largest
// app category, so it exercises the same directory depth and metadata volume
// as the launcher scan.
#define FMRB_FS_BENCH_DIR "/app/demo"

/**
 * @brief Measure the per-operation cost of the filesystem HAL.
 *
 * Times the three access patterns the launcher scan is built from: listing a
 * directory, opening/reading/closing an existing file, and opening a path that
 * does not exist (path resolution only). Results are reported as averages via
 * FMRB_LOGI.
 *
 * No Ruby VM is involved, so the numbers are engine independent and can be
 * compared directly across builds (mruby vs Spinel) and across storage
 * configuration changes (e.g. CONFIG_LITTLEFS_MMAP_PARTITION).
 *
 * Each phase stops early once it exceeds an internal wall-clock budget, so a
 * slow filesystem cannot stretch boot time without bound.
 *
 * Compiled out (including its static buffers) when FMRB_FS_BENCH_ENABLED is 0
 * in fs_bench.c.
 *
 * @param dir_path Directory to measure, in the virtual namespace (e.g. "/app/demo").
 *                 Must contain regular files. Nothing is written or removed.
 */
void fs_bench_run(const char *dir_path);

#ifdef __cplusplus
}
#endif
