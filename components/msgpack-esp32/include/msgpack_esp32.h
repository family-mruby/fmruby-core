#ifndef MSGPACK_ESP32_H
#define MSGPACK_ESP32_H

#include "msgpack.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MessagePack ESP32 Wrapper
 *
 * This header provides convenient access to MessagePack-C library
 * for use in ESP32/ESP-IDF and Linux environments.
 *
 * Basic usage:
 * @code
 * #include "msgpack_esp32.h"
 *
 * // Packing example
 * msgpack_sbuffer sbuf;
 * msgpack_sbuffer_init(&sbuf);
 * msgpack_packer pk;
 * msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
 *
 * msgpack_pack_array(&pk, 3);
 * msgpack_pack_int(&pk, 1);
 * msgpack_pack_true(&pk);
 * msgpack_pack_str(&pk, 5);
 * msgpack_pack_str_body(&pk, "hello", 5);
 *
 * // Unpacking example
 * msgpack_unpacked result;
 * msgpack_unpacked_init(&result);
 * msgpack_unpack_return ret = msgpack_unpack_next(&result, sbuf.data, sbuf.size, NULL);
 *
 * msgpack_sbuffer_destroy(&sbuf);
 * msgpack_unpacked_destroy(&result);
 * @endcode
 */

// Re-export common MessagePack types for convenience
typedef msgpack_object msgpack_esp32_object_t;
typedef msgpack_sbuffer msgpack_esp32_sbuffer_t;
typedef msgpack_packer msgpack_esp32_packer_t;
typedef msgpack_unpacked msgpack_esp32_unpacked_t;
typedef msgpack_unpack_return msgpack_esp32_unpack_return_t;

// Common constants
#define MSGPACK_ESP32_UNPACK_SUCCESS MSGPACK_UNPACK_SUCCESS
#define MSGPACK_ESP32_UNPACK_EXTRA_BYTES MSGPACK_UNPACK_EXTRA_BYTES
#define MSGPACK_ESP32_UNPACK_CONTINUE MSGPACK_UNPACK_CONTINUE
#define MSGPACK_ESP32_UNPACK_PARSE_ERROR MSGPACK_UNPACK_PARSE_ERROR
#define MSGPACK_ESP32_UNPACK_NOMEM_ERROR MSGPACK_UNPACK_NOMEM_ERROR

#ifdef __cplusplus
}
#endif

#endif // MSGPACK_ESP32_H