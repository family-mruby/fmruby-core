#include <string.h>
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>

#include "msgpack_esp32.h"
#include "../../include/picoruby_fmrb_msgpack.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"

#define TAG "MSGPACK"

// Forward declaration
static int pack_mrb_value(mrb_state *mrb, msgpack_packer *pk, mrb_value value);

// Whether a String carries mruby's binary flag (String#b, force_encoding
// "BINARY"): such a String is packed as bin and a bin unpacks to one.
static mrb_bool is_binary_string(mrb_value value)
{
#ifdef MRB_STR_BINARY
    return RSTR_BINARY_P(mrb_str_ptr(value)) ? TRUE : FALSE;
#else
    (void)value;
    return FALSE;
#endif
}

static mrb_value new_binary_string(mrb_state *mrb, const char *ptr, size_t len)
{
    mrb_value s = mrb_str_new(mrb, ptr, len);
#ifdef MRB_STR_BINARY
    mrb_str_ptr(s)->flags |= MRB_STR_BINARY;
#endif
    return s;
}

/**
 * @brief Recursively pack mrb_value into msgpack format
 */
static int pack_mrb_value(mrb_state *mrb, msgpack_packer *pk, mrb_value value)
{
    int ret = 0;

    switch (mrb_type(value)) {
    case MRB_TT_FALSE:
        if (mrb_nil_p(value)) {
            ret = msgpack_pack_nil(pk);
        } else {
            ret = msgpack_pack_false(pk);
        }
        break;

    case MRB_TT_TRUE:
        ret = msgpack_pack_true(pk);
        break;

    case MRB_TT_INTEGER:
        ret = msgpack_pack_int64(pk, mrb_integer(value));
        break;

    case MRB_TT_FLOAT:
        ret = msgpack_pack_double(pk, mrb_float(value));
        break;

    case MRB_TT_SYMBOL: {
        const char *sym_name = mrb_sym_name(mrb, mrb_symbol(value));
        size_t len = strlen(sym_name);
        ret = msgpack_pack_str(pk, len);
        if (ret == 0) {
            ret = msgpack_pack_str_body(pk, sym_name, len);
        }
        break;
    }

    case MRB_TT_STRING: {
        const char *str = RSTRING_PTR(value);
        size_t len = RSTRING_LEN(value);
        if (is_binary_string(value)) {
            // A binary String (String#b) is bytes: bin, not str.
            ret = msgpack_pack_bin(pk, len);
            if (ret == 0) {
                ret = msgpack_pack_bin_body(pk, str, len);
            }
        } else {
            ret = msgpack_pack_str(pk, len);
            if (ret == 0) {
                ret = msgpack_pack_str_body(pk, str, len);
            }
        }
        break;
    }

    case MRB_TT_ARRAY: {
        mrb_int len = RARRAY_LEN(value);
        ret = msgpack_pack_array(pk, len);
        if (ret != 0) break;

        for (mrb_int i = 0; i < len; i++) {
            mrb_value elem = mrb_ary_ref(mrb, value, i);
            ret = pack_mrb_value(mrb, pk, elem);
            if (ret != 0) break;
        }
        break;
    }

    case MRB_TT_HASH: {
        mrb_value keys = mrb_hash_keys(mrb, value);
        mrb_int len = RARRAY_LEN(keys);
        ret = msgpack_pack_map(pk, len);
        if (ret != 0) break;

        for (mrb_int i = 0; i < len; i++) {
            mrb_value key = mrb_ary_ref(mrb, keys, i);
            mrb_value val = mrb_hash_get(mrb, value, key);

            // Pack key
            ret = pack_mrb_value(mrb, pk, key);
            if (ret != 0) break;

            // Pack value
            ret = pack_mrb_value(mrb, pk, val);
            if (ret != 0) break;
        }
        break;
    }

    default:
        FMRB_LOGE(TAG, "Unsupported mruby type: %d", mrb_type(value));
        return -1;
    }

    return ret;
}

mrb_bool fmrb_msgpack_pack(mrb_state *mrb, mrb_value value, uint8_t **out_buf, size_t *out_size)
{
    if (!out_buf || !out_size) {
        FMRB_LOGE(TAG, "Invalid output parameters");
        return FALSE;
    }

    // Initialize msgpack buffer
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);

    // Initialize msgpack packer
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    // Pack the value
    int ret = pack_mrb_value(mrb, &pk, value);
    if (ret != 0) {
        FMRB_LOGE(TAG, "Failed to pack mruby value: %d", ret);
        msgpack_sbuffer_destroy(&sbuf);
        return FALSE;
    }

    // Allocate output buffer using mrb_malloc (mruby memory)
    *out_buf = (uint8_t *)mrb_malloc(mrb, sbuf.size);
    if (!*out_buf) {
        FMRB_LOGE(TAG, "Failed to allocate output buffer: %zu bytes", sbuf.size);
        msgpack_sbuffer_destroy(&sbuf);
        return FALSE;
    }

    // Copy data to output buffer
    memcpy(*out_buf, sbuf.data, sbuf.size);
    *out_size = sbuf.size;

    msgpack_sbuffer_destroy(&sbuf);

    FMRB_LOGD(TAG, "Packed mruby value to %zu bytes", *out_size);
    return TRUE;
}

// Forward declaration
static mrb_bool unpack_msgpack_object(mrb_state *mrb, msgpack_object *obj, mrb_value *out);

/**
 * @brief Recursively convert msgpack_object to mrb_value
 *
 * FALSE for what is not a value here (an ext type, an integer above
 * INT64_MAX): the whole payload is refused rather than read as nil.
 */
static mrb_bool unpack_msgpack_object(mrb_state *mrb, msgpack_object *obj, mrb_value *out)
{
    switch (obj->type) {
    case MSGPACK_OBJECT_NIL:
        *out = mrb_nil_value();
        return TRUE;

    case MSGPACK_OBJECT_BOOLEAN:
        *out = mrb_bool_value(obj->via.boolean);
        return TRUE;

    case MSGPACK_OBJECT_POSITIVE_INTEGER:
        if (obj->via.u64 > (uint64_t)INT64_MAX) {
            FMRB_LOGW(TAG, "integer above int64 refused");
            return FALSE;
        }
        *out = mrb_int_value(mrb, (mrb_int)obj->via.u64);
        return TRUE;

    case MSGPACK_OBJECT_NEGATIVE_INTEGER:
        *out = mrb_int_value(mrb, (mrb_int)obj->via.i64);
        return TRUE;

    case MSGPACK_OBJECT_FLOAT32:
    case MSGPACK_OBJECT_FLOAT64:
        *out = mrb_float_value(mrb, obj->via.f64);
        return TRUE;

    case MSGPACK_OBJECT_STR:
        *out = mrb_str_new(mrb, obj->via.str.ptr, obj->via.str.size);
        return TRUE;

    case MSGPACK_OBJECT_BIN:
        *out = new_binary_string(mrb, obj->via.bin.ptr, obj->via.bin.size);
        return TRUE;

    case MSGPACK_OBJECT_ARRAY: {
        mrb_value ary = mrb_ary_new_capa(mrb, obj->via.array.size);
        for (uint32_t i = 0; i < obj->via.array.size; i++) {
            mrb_value elem;
            if (!unpack_msgpack_object(mrb, &obj->via.array.ptr[i], &elem)) return FALSE;
            mrb_ary_push(mrb, ary, elem);
        }
        *out = ary;
        return TRUE;
    }

    case MSGPACK_OBJECT_MAP: {
        mrb_value hash = mrb_hash_new_capa(mrb, obj->via.map.size);
        for (uint32_t i = 0; i < obj->via.map.size; i++) {
            mrb_value key;
            mrb_value val;
            if (!unpack_msgpack_object(mrb, &obj->via.map.ptr[i].key, &key)) return FALSE;
            if (!unpack_msgpack_object(mrb, &obj->via.map.ptr[i].val, &val)) return FALSE;
            mrb_hash_set(mrb, hash, key, val);
        }
        *out = hash;
        return TRUE;
    }

    default:
        // ext types (and anything newer): not a value of this MessagePack
        FMRB_LOGW(TAG, "msgpack type %d refused", obj->type);
        return FALSE;
    }
}

mrb_bool fmrb_msgpack_unpack(mrb_state *mrb, const uint8_t *buf, size_t size, mrb_value *out_value)
{
    if (!buf || !out_value || size == 0) {
        FMRB_LOGE(TAG, "Invalid unpack parameters");
        return FALSE;
    }

    // Initialize msgpack unpacker
    msgpack_unpacked result;
    msgpack_unpacked_init(&result);

    // Unpack the data
    size_t off = 0;
    msgpack_unpack_return ret = msgpack_unpack_next(&result, (const char *)buf, size, &off);

    if (ret != MSGPACK_UNPACK_SUCCESS) {
        FMRB_LOGW(TAG, "Failed to unpack msgpack data: %d", ret);
        msgpack_unpacked_destroy(&result);
        return FALSE;
    }
    // Exactly one value: bytes after it are refused, not ignored.
    if (off != size) {
        FMRB_LOGW(TAG, "%u bytes after the msgpack value refused", (unsigned)(size - off));
        msgpack_unpacked_destroy(&result);
        return FALSE;
    }

    // Convert msgpack_object to mrb_value
    mrb_bool ok = unpack_msgpack_object(mrb, &result.data, out_value);

    msgpack_unpacked_destroy(&result);

    if (ok) {
        FMRB_LOGD(TAG, "Unpacked msgpack data successfully");
    }
    return ok;
}
