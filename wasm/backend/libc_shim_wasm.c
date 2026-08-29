/*
 * libc_shim_wasm.c - libc calls Emscripten ships headers for but no
 * implementation of. Not an fmrb abstraction: these are POSIX names the
 * shared code's linux branches call directly.
 */

#include <string.h>
#include <sys/sysinfo.h>

#include "esp_heap_caps.h"

/* FmrbApp.system_info's linux branch. Answer with the heap budget the
 * FreeRTOS-port heap accounting keeps (wasm/stub/heap_wasm.c). */
int sysinfo(struct sysinfo *info)
{
    memset(info, 0, sizeof(*info));
    info->mem_unit = 1;
    info->totalram = 256u * 1024u * 1024u;
    info->freeram = heap_caps_get_free_size(0);
    return 0;
}
