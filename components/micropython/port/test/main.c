/**
 * Host-only smoke test for the generated embed package.
 *
 * Built and run by "rake micropython:smoke". It links nothing from fmruby-core,
 * so a failure here means the generated mp_embed/ tree is incomplete or the
 * configuration in mpconfigport.h does not stand on its own -- which is much
 * easier to diagnose before the IDF build is involved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/reader.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "port/micropython_embed.h"

// Exercises the pieces the firmware depends on: print, integer and float
// arithmetic, containers, exceptions and an explicit GC pass.
static const char *script =
    "print('hello')\n"
    "print('sum', sum(x * x for x in range(10)))\n"
    "print('float', 1.0 / 4)\n"
    "print('list', [c for c in 'fmrb'])\n"
    "try:\n"
    "    1 // 0\n"
    "except ZeroDivisionError as e:\n"
    "    print('caught', repr(e))\n"
    "import gc\n"
    "gc.collect()\n"
    "print('free', gc.mem_free() > 0)\n"
    "try:\n"
    "    import nosuchmodule\n"
    "except ImportError as e:\n"
    "    print('import', repr(e))\n"
    "import random\n"
    "random.seed(7)\n"
    "print('random', [random.randint(0, 9) for _ in range(4)])\n";

// The random module is seeded from the firmware's clock (fmrb_mp.c). The host
// test has no clock worth reading and wants repeatable output anyway, so a
// fixed seed stands in.
unsigned long fmrb_mp_random_seed(void) {
    return 1;
}

// The VM hook (MICROPY_VM_HOOK_LOOP) is implemented by fmrb_mp.c in the
// firmware, which this host-only link does not include. The smoke test has
// no stop requests to poll, so an empty hook is the correct stand-in.
void fmrb_mp_vm_hook(void) {
}

// External import is on (MICROPY_ENABLE_EXTERNAL_IMPORT), and its two hooks are
// implemented in modules/fmrb_module.c on top of the firmware's file layer,
// which this host-only link does not have. Standing in for them with "nothing
// is there" keeps the link honest: the smoke test checks that built-in imports
// still resolve without a filesystem, which is exactly what the stubs describe.
mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

void mp_reader_new_file(mp_reader_t *reader, qstr filename) {
    (void)reader;
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_reader_t reader;
    mp_reader_new_file(&reader, filename);
    return mp_lexer_new(filename, reader);
}

// _fmrb is registered by modules/fmrb_module.c with MP_REGISTER_MODULE, so the
// generated module table names it whether or not that file is in the link --
// and it is not here, because its bridge half needs the firmware. An empty
// module satisfies the table. The smoke test checks the generated tree and the
// configuration, not the bindings; those are covered on the device.
static const mp_rom_map_elem_t stub_fmrb_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__fmrb) },
};
static MP_DEFINE_CONST_DICT(stub_fmrb_globals, stub_fmrb_globals_table);

const mp_obj_module_t fmrb_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&stub_fmrb_globals,
};

#define HEAP_SIZE (64 * 1024)
#define STACK_LIMIT (256 * 1024)

int main(void) {
    int stack_top;
    char *heap = malloc(HEAP_SIZE);
    if (heap == NULL) {
        return 1;
    }

    mp_embed_init(heap, HEAP_SIZE, &stack_top);
    // MICROPY_STACK_CHECK is on and mp_embed_init leaves the limit at 0, which
    // makes every check fail. The limit has to be set before any Python code
    // runs; the firmware does the same from the app task's own stack budget.
    mp_stack_set_limit(STACK_LIMIT);
    mp_embed_exec_str(script);
    mp_embed_deinit();

    free(heap);
    return 0;
}
