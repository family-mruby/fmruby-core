// Local implementations of the GA file-transfer operations for the
// Modern (ESP32-P4) target, where the "GA storage" is the core's own
// /flash LittleFS. On retro these operations stream over the SPI link
// to the WROVER; on Modern no link receiver exists and none is needed:
// the host task calls these directly instead of sending link messages.
//
// Path semantics mirror fmruby-graphics-audio file_transfer_handler.c:
// requested paths are relative to /flash (a leading '/' is tolerated),
// parent directories are created on write, and RMDIR is restricted to
// /flash/cache and keeps the target root directory in place.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

fmrb_err_t host_file_local_write(const char *path, uint16_t path_len,
                                 const uint8_t *data, uint32_t len);
// *out_checksum receives the CRC32 of the stored file, using the same routine
// and seed as the WROVER's file_crc32, so callers can compare it against a
// locally computed one exactly as they do on retro. 0 when the file is absent.
fmrb_err_t host_file_local_status(const char *path, uint16_t path_len,
                                  uint8_t *out_exists, uint32_t *out_size,
                                  uint32_t *out_checksum);
fmrb_err_t host_file_local_delete(const char *path, uint16_t path_len);
// Returns FMRB_OK when the tree walk succeeded; *out_deleted receives the
// number of removed entries, *out_status the WROVER-compatible status code
// (0=ok, 1=rejected outside cache, 2=partial failure).
fmrb_err_t host_file_local_rmdir(const char *path, uint16_t path_len,
                                 uint32_t *out_deleted, uint8_t *out_status);

#ifdef __cplusplus
}
#endif
