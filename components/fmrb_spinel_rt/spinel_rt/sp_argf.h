#ifndef SP_ARGF_H
#define SP_ARGF_H
/* sp_argf.h -- ARGV/ARGF state + cold accessors.
 *
 * sp_argv is populated by the generated main() (codegen emits
 * `sp_argv.len = argc - 1; sp_argv.data = ...` directly), so its storage
 * stays defined in the generated TU -- just non-static now, resolved at
 * the final link the same way lib/sp_core.c reaches sp_sprintf. sp_argf_obj
 * (ARGF's single-instance read cursor) follows the same pattern.
 */
#include "sp_types.h"   /* sp_int */
#include <stdio.h>      /* FILE */

typedef struct{const char**data;sp_int len;}sp_Argv;
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern sp_Argv sp_argv;               /* defined in the generated TU */
#endif

typedef struct { FILE *cur; int started; const char *fname; } sp_Argf;
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern sp_Argf sp_argf_obj;            /* defined in the generated TU */
#endif

/* the ARGV-as-poly-array materialization cache: allocated lazily by
   sp_get_ARGV (lib/sp_cold.c), read by sp_re_mark_globals's GC root scan
   (spinel_rt.h) -- extern so both sides see the same object. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern sp_StrArray *sp_argv_array_cache;
#endif

sp_StrArray *sp_get_ARGV(void);
int sp_argf_ensure(void);
const char *sp_argf_gets(void);
const char *sp_argf_read(void);
sp_StrArray *sp_argf_readlines(void);
const char *sp_argf_filename(void);
sp_bool sp_argf_eof(void);

#endif
