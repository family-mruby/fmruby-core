#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include "../include/picoruby_fmrb_msgpack.h"

// MessagePack.pack(obj) -> String (binary)
// This is a stub - actual implementation is in ports/esp32/msgpack.c
static mrb_value mrb_msgpack_pack(mrb_state *mrb, mrb_value self)
{
    mrb_value obj;
    mrb_get_args(mrb, "o", &obj);

    uint8_t *buf = NULL;
    size_t size = 0;

    if (!fmrb_msgpack_pack(mrb, obj, &buf, &size)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to pack object to msgpack");
    }

    // Create Ruby string from binary data
    mrb_value result = mrb_str_new(mrb, (const char *)buf, size);

    // Free the buffer allocated by fmrb_msgpack_pack
    mrb_free(mrb, buf);

    return result;
}

// MessagePack.unpack(str) -> Object
// This is a stub - actual implementation is in ports/esp32/msgpack.c
static mrb_value mrb_msgpack_unpack(mrb_state *mrb, mrb_value self)
{
    mrb_value str;
    mrb_get_args(mrb, "S", &str);

    mrb_value result;
    const uint8_t *buf = (const uint8_t *)RSTRING_PTR(str);
    size_t size = RSTRING_LEN(str);

    if (!fmrb_msgpack_unpack(mrb, buf, size, &result)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to unpack msgpack data");
    }

    return result;
}

void mrb_picoruby_fmrb_msgpack_gem_init(mrb_state *mrb)
{
    struct RClass *msgpack_module = mrb_define_module(mrb, "MessagePack");

    mrb_define_module_function(mrb, msgpack_module, "pack", mrb_msgpack_pack, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, msgpack_module, "unpack", mrb_msgpack_unpack, MRB_ARGS_REQ(1));
}

void mrb_picoruby_fmrb_msgpack_gem_final(mrb_state *mrb)
{
    // Cleanup if needed
}
