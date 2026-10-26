/*
 * etc_update.h - bring a user's /etc up to a newer distribution's
 * (doc/linux_native/plan.md, "ファイルの置き場所"; report/l3a.md).
 *
 * The native program copies the distribution's etc to the user's /etc on the
 * first start only; from then on the user's copy is the one the machine reads
 * and Config writes. A newer package can bring settings, services and files
 * that copy does not have. This adds them, and only them:
 *
 *   system_conf.toml, system_conf.factory.toml
 *                   every key the user's file lacks (top-level keys, keys of
 *                   a [table], whole tables, arrays of tables the user has
 *                   none of); a key the user has keeps the user's value
 *   services.toml   every service section ([name] with its [name.*]) the
 *                   user's file lacks; the sections the user has are left
 *                   exactly as they are (enable and the rest are the user's)
 *   anything else   copied when the user has no file of that name
 *
 * The user's text is never rewritten: what is missing is inserted (a key at
 * the end of its table, a section at the end of the file) with the comment
 * lines that stand right above it in the distribution's file, so the user's
 * comments and order stay as they are. A file that does not parse is left
 * alone, and the result has to parse before it replaces the user's (written
 * to a temporary file and renamed).
 *
 * Native only, on the main thread before the machine starts (and in the host
 * tests): libc and tomlc99 with its default allocator, nothing of FreeRTOS.
 * When the user has no etc yet, there is nothing to do: the file HAL's first
 * start copies the whole seed.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* One line per change (or per file left alone, with the reason). */
typedef void (*native_etc_log_fn)(void *ctx, const char *line);

/*
 * Compare seed_dir (the distribution's etc) with user_etc and add what the
 * user lacks. Returns the number of additions (keys, sections and files), 0
 * when the user's etc is already complete or does not exist, or -1 when
 * something could not be read or written (the log says what; whatever was
 * already done stays).
 */
int native_etc_update(const char *seed_dir, const char *user_etc,
                      native_etc_log_fn log, void *ctx);

#ifdef __cplusplus
}
#endif
