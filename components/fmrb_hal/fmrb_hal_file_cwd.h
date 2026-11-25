#pragma once

/*
 * The file HAL's own use of the per-app working directory
 * (fmrb_hal_file_cwd.c). Only the platform file HALs include this; everyone
 * else goes through fmrb_hal_file.h.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fold a virtual path into an absolute one.
 *
 * "//", "." and ".." are folded and ".." stops at "/". A relative path is
 * first put under the calling app's working directory.
 *
 * @return false when the result does not fit in @p out_len bytes.
 */
bool fmrb_hal_file_normalize(const char *path, char *out, size_t out_len);

/**
 * @brief The path to resolve: @p path itself when it is absolute (left
 *        exactly as it is), otherwise its folded form under the calling
 *        app's working directory, written to @p buf.
 * @return @p path, @p buf, or NULL when the folded form does not fit.
 */
const char *fmrb_hal_file_cwd_apply(const char *path, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
