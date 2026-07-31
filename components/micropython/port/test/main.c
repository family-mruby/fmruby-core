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
    "print('free', gc.mem_free() > 0)\n";

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
