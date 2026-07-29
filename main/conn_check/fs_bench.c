#include "fs_bench.h"

// Diagnostic switch. Set to 0 to compile the benchmark and its static buffers
// out of the image. Kept as a source-level constant rather than a Kconfig
// option because this is a measurement tool, not a product feature.
#define FMRB_FS_BENCH_ENABLED 0

#if FMRB_FS_BENCH_ENABLED

#include "fmrb_hal_file.h"
#include "fmrb_hal_time.h"
#include "fmrb_log.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "fs_bench";

#define FS_BENCH_MAX_FILES   16
#define FS_BENCH_NAME_LEN    48
#define FS_BENCH_PATH_LEN    128
// Mirrors the launcher's IO#read default so the data-read cost is comparable.
#define FS_BENCH_READ_LEN    1024
// Per-phase wall-clock cap. A phase stops as soon as it exceeds this, so the
// bench costs at most ~1 s of boot even when every operation is slow. The
// average is what we are after, not a fixed iteration count.
#define FS_BENCH_PHASE_BUDGET_US  400000

static char    s_names[FS_BENCH_MAX_FILES][FS_BENCH_NAME_LEN];
static char    s_big_name[FS_BENCH_NAME_LEN];
static char    s_path[FS_BENCH_PATH_LEN];
static uint8_t s_read_buf[FS_BENCH_READ_LEN];

static uint32_t avg_us(uint32_t total_us, uint32_t count)
{
    return count ? (total_us / count) : 0;
}

// Compose "<dir>/<name><suffix>" into s_path. Returns false if it would not
// fit, in which case the caller skips that entry rather than measuring a
// truncated path.
static bool build_path(const char *dir, const char *name, const char *suffix)
{
    int n = snprintf(s_path, sizeof(s_path), "%s/%s%s", dir, name, suffix);
    return (n > 0 && n < (int)sizeof(s_path));
}

void fs_bench_run(const char *dir_path)
{
    if (dir_path == NULL) {
        return;
    }

    FMRB_LOGI(TAG, "--- FS bench on %s ---", dir_path);

    // Phase 1: directory listing. Note that fmrb_hal_file_readdir() stat()s
    // every entry to fill in size/mtime, so this figure includes one extra
    // path resolution per entry. The launcher's Dir.read uses the mruby
    // directory HAL, which does not stat.
    fmrb_dir_t dir = NULL;
    fmrb_time_t t0 = fmrb_hal_time_get_us();
    if (fmrb_hal_file_opendir(dir_path, &dir) != FMRB_OK) {
        FMRB_LOGW(TAG, "opendir %s failed, bench skipped", dir_path);
        return;
    }

    fmrb_file_info_t info;
    uint32_t entries = 0;
    uint32_t big_size = 0;
    int name_count = 0;
    s_big_name[0] = '\0';
    while (fmrb_hal_file_readdir(dir, &info) == FMRB_OK) {
        entries++;
        if (info.is_dir || strlen(info.name) >= FS_BENCH_NAME_LEN) {
            continue;
        }
        if (name_count < FS_BENCH_MAX_FILES) {
            snprintf(s_names[name_count], FS_BENCH_NAME_LEN, "%s", info.name);
            name_count++;
        }
        // Remember the largest file for the size-sweep phase below.
        if (info.size > big_size) {
            big_size = (uint32_t)info.size;
            snprintf(s_big_name, FS_BENCH_NAME_LEN, "%s", info.name);
        }
    }
    fmrb_hal_file_closedir(dir);
    uint32_t list_us = (uint32_t)(fmrb_hal_time_get_us() - t0);
    FMRB_LOGI(TAG, "listdir : %u entries, %u us total, %u us/entry (readdir stats each entry)",
              (unsigned)entries, (unsigned)list_us,
              (unsigned)avg_us(list_us, entries));

    if (name_count == 0) {
        FMRB_LOGW(TAG, "no regular files in %s, file phases skipped", dir_path);
        return;
    }

    // Phase 2: open + read + close of existing files. Distinct files are used
    // rather than the same file N times so the metadata traversal is not
    // served from a warm cache, matching how the launcher scan behaves.
    uint32_t hits = 0;
    uint32_t bytes = 0;
    t0 = fmrb_hal_time_get_us();
    for (int i = 0; i < name_count; i++) {
        if (!build_path(dir_path, s_names[i], "")) {
            continue;
        }
        fmrb_file_t f = NULL;
        if (fmrb_hal_file_open(s_path, FMRB_O_RDONLY, &f) != FMRB_OK) {
            continue;
        }
        size_t n = 0;
        fmrb_hal_file_read(f, s_read_buf, FS_BENCH_READ_LEN, &n);
        fmrb_hal_file_close(f);
        bytes += (uint32_t)n;
        hits++;
        if (fmrb_hal_time_get_us() - t0 > FS_BENCH_PHASE_BUDGET_US) {
            break;
        }
    }
    uint32_t hit_us = (uint32_t)(fmrb_hal_time_get_us() - t0);
    FMRB_LOGI(TAG, "open+read+close: %u files, %u us total, %u us/file (%u B read)",
              (unsigned)hits, (unsigned)hit_us,
              (unsigned)avg_us(hit_us, hits), (unsigned)bytes);

    // Phase 3: open of a path that does not exist. This is the cost of path
    // resolution alone, which is what the launcher's extension probe and its
    // "is this entry a directory" probe pay on every miss.
    uint32_t misses = 0;
    t0 = fmrb_hal_time_get_us();
    for (int i = 0; i < name_count; i++) {
        if (!build_path(dir_path, s_names[i], ".absent")) {
            continue;
        }
        fmrb_file_t f = NULL;
        if (fmrb_hal_file_open(s_path, FMRB_O_RDONLY, &f) == FMRB_OK) {
            // Should not happen; close it so the slot is not leaked.
            fmrb_hal_file_close(f);
        }
        misses++;
        if (fmrb_hal_time_get_us() - t0 > FS_BENCH_PHASE_BUDGET_US) {
            break;
        }
    }
    uint32_t miss_us = (uint32_t)(fmrb_hal_time_get_us() - t0);
    FMRB_LOGI(TAG, "open miss      : %u tries, %u us total, %u us/try",
              (unsigned)misses, (unsigned)miss_us,
              (unsigned)avg_us(miss_us, misses));

    // Phase 4: read one large file end to end. Phase 2 caps every file at
    // FS_BENCH_READ_LEN, so on its own it cannot tell a per-file fixed cost
    // apart from a per-byte one. This phase is the second point on the size
    // axis: if us/KB here matches phase 2, reads are still paying per byte;
    // if it collapses, what is left is fixed open/close overhead.
    uint32_t big_us = 0;
    uint32_t big_read = 0;
    if (s_big_name[0] != '\0' && build_path(dir_path, s_big_name, "")) {
        t0 = fmrb_hal_time_get_us();
        fmrb_file_t f = NULL;
        if (fmrb_hal_file_open(s_path, FMRB_O_RDONLY, &f) == FMRB_OK) {
            size_t n = 0;
            do {
                n = 0;
                if (fmrb_hal_file_read(f, s_read_buf, FS_BENCH_READ_LEN, &n) != FMRB_OK) {
                    break;
                }
                big_read += (uint32_t)n;
            } while (n == FS_BENCH_READ_LEN);
            fmrb_hal_file_close(f);
            big_us = (uint32_t)(fmrb_hal_time_get_us() - t0);
            FMRB_LOGI(TAG, "large read     : %s, %u B in %u us (%u us/KB)",
                      s_big_name, (unsigned)big_read, (unsigned)big_us,
                      (unsigned)(big_read ? (big_us * 1024) / big_read : 0));
        } else {
            FMRB_LOGW(TAG, "large read: open %s failed", s_path);
        }
    }

    FMRB_LOGI(TAG, "--- FS bench end (%u ms) ---",
              (unsigned)((list_us + hit_us + miss_us + big_us) / 1000));
}

#else  // FMRB_FS_BENCH_ENABLED

void fs_bench_run(const char *dir_path)
{
    (void)dir_path;
}

#endif // FMRB_FS_BENCH_ENABLED
