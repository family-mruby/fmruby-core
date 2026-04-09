#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the log ring buffer.
 * @param buf  Pointer to memory region (from POOL_ID_LOG_BUFFER)
 * @param size Size of the memory region in bytes
 */
void fmrb_log_buffer_init(void *buf, size_t size);

/**
 * Write a formatted log message to the ring buffer (thread-safe).
 * Called from FMRB_LOG macros.
 */
void fmrb_log_buffer_printf(const char *tag, char level, const char *format, ...);

/**
 * Read log lines from the ring buffer.
 * @param out_buf    Output buffer for concatenated lines (null-terminated)
 * @param out_size   Size of output buffer
 * @param max_lines  Maximum number of lines to read
 * @param read_pos   In/Out: read position cursor (0 = read from oldest available)
 * @return Number of lines read
 */
int fmrb_log_buffer_read_lines(char *out_buf, size_t out_size, int max_lines, uint32_t *read_pos);

/**
 * Get the current write position (for detecting new log entries).
 */
uint32_t fmrb_log_buffer_get_write_pos(void);

/**
 * Set minimum log level for buffer collection.
 * @param level 'E', 'W', 'I', or 'D'
 */
void fmrb_log_buffer_set_level(char level);

/**
 * Get current buffer collection level.
 */
char fmrb_log_buffer_get_level(void);

/**
 * Check if a given level is enabled for buffer collection.
 */
int fmrb_log_buffer_level_enabled(char level);

#ifdef __cplusplus
}
#endif
