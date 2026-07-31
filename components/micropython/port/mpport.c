/**
 * Port-provided hooks that the MicroPython core declares but does not define.
 *
 * These are compiled alongside the generated mp_embed/ tree, both by the host
 * smoke test and by the firmware build, so they must not depend on anything
 * from fmruby-core.
 */

#include "py/builtin.h"
#include "py/runtime.h"

// MICROPY_PY_IO puts open() in builtins and in the io module unconditionally,
// leaving the implementation to the port. Guest apps get no filesystem access,
// so it always fails rather than being silently absent.
mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)n_args;
    (void)args;
    (void)kwargs;
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("filesystem access is not available"));
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
