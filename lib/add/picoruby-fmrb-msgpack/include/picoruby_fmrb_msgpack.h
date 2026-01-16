#ifndef PICORUBY_FMRB_MSGPACK_H
#define PICORUBY_FMRB_MSGPACK_H

#include <mruby.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Serialize mruby value to MessagePack binary
 *
 * Supports basic mruby types: Integer, String, Symbol, Array, Hash, true/false/nil
 *
 * @param mrb mruby state
 * @param value mruby value to serialize
 * @param out_buf Output buffer pointer (will be allocated, caller must free)
 * @param out_size Output size in bytes
 * @return mrb_bool true on success, false on failure
 */
mrb_bool fmrb_msgpack_pack(mrb_state *mrb, mrb_value value, uint8_t **out_buf, size_t *out_size);

/**
 * @brief Deserialize MessagePack binary to mruby value
 *
 * @param mrb mruby state
 * @param buf Input buffer containing msgpack binary
 * @param size Size of input buffer
 * @param out_value Output mruby value
 * @return mrb_bool true on success, false on failure
 */
mrb_bool fmrb_msgpack_unpack(mrb_state *mrb, const uint8_t *buf, size_t size, mrb_value *out_value);

void mrb_picoruby_fmrb_msgpack_gem_init(mrb_state *mrb);
void mrb_picoruby_fmrb_msgpack_gem_final(mrb_state *mrb);

#ifdef __cplusplus
}
#endif

#endif // PICORUBY_FMRB_MSGPACK_H
