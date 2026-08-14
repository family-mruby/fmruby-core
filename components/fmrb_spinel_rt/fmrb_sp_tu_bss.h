/* Where this firmware puts Spinel's large per-TU statics.
 *
 * A generated Spinel program carries its own copy of the exception stack, the
 * catch stack and the dynamic symbol table -- they are per translation unit,
 * not shared through the runtime -- so every program added costs another
 * ~11.4 KB. With five of them that is 57 KB, and on the P4 the memory it comes
 * out of is internal SRAM, which is the scarce one. PSRAM is not: 23 MB of it
 * sits unused (doc/spinel_aot/report/per_tu_internal_ram.md).
 *
 * So point them at PSRAM. This changes where the arrays live and nothing else:
 * same type, same size, same semantics, no allocation, nothing to free. The
 * exception machinery is cold -- setjmp/longjmp on a task, no DMA, no ISR --
 * so the slower memory costs nothing measurable.
 *
 * Not applied on the Linux simulator (no such attribute) or under SP_THREADS
 * (a section attribute and __thread cannot both apply); SP_TU_BSS then stays
 * empty and the arrays land in ordinary .bss, exactly as upstream intends.
 *
 * Reached by -include ahead of sp_mem_override.h, so this definition is in
 * place before sp_types.h supplies its empty default. Kept outside spinel_rt/
 * because that directory is a snapshot of the fork and import_from_fork.rb
 * would delete anything of ours in it.
 */
#ifndef FMRB_SP_TU_BSS_H
#define FMRB_SP_TU_BSS_H

#if defined(ESP_PLATFORM) && !defined(CONFIG_IDF_TARGET_LINUX) && !defined(SP_THREADS)
#include "esp_attr.h"
#define SP_TU_BSS EXT_RAM_BSS_ATTR
#endif

#endif /* FMRB_SP_TU_BSS_H */
