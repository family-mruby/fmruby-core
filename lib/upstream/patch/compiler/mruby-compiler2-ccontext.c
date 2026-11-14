#include <string.h>
#include "../include/mrc_ccontext.h"
#include "../include/mrc_parser_util.h"

#if defined(MRC_TARGET_MRUBY)
/* The Prism xallocator used to route allocations through this mrb_state. Define
   it in the compiler library so every executable that links libmruby (not just
   the mrbc/mruby/mirb front-ends) resolves the symbol. The front-ends assign
   it unconditionally for the mruby target, so it must exist regardless of
   MRC_ALLOC_LIBC even though only the non-libc allocator dereferences it.
   family-mruby no longer allocates through it -- see fmrb_prism_realloc. */
mrb_state *global_mrb = NULL;
#endif

#if defined(MRC_TARGET_MRUBY) && !defined(MRC_ALLOC_LIBC)
/*
 * family-mruby: prism's allocator (prism_xallocator.h).
 *
 * Upstream sends every prism allocation to mrb_malloc(global_mrb). That holds
 * where a process has one VM; here each app is a VM on its own FreeRTOS task,
 * and some tasks (Spinel apps -- the editor among them) have no VM at all,
 * while global_mrb points at whoever compiled last. Reaching prism from the
 * editor through global_mrb would write GC bookkeeping into another task's VM,
 * possibly run its collector from the wrong task, longjmp into its jmpbuf when
 * memory runs out, or dereference NULL before any app has ever compiled.
 *
 * So the choice is made per call, in this order:
 *
 *   1. a scratch allocator installed by a caller (the editor's type inference
 *      bridge installs one so a completion request parses out of the document
 *      pool instead of the app's own heap, and can throw the whole parse away
 *      at once). Installed only for the duration of a call, under that
 *      caller's own lock.
 *   2. the calling task's own VM, if it has one -- byte for byte what upstream
 *      did for the compile path, including raising NoMemoryError, so an app
 *      compile keeps feeding the GC the same pressure signal it always has.
 *   3. no VM on this task: the task's own estalloc heap, which is where the
 *      bytes came from anyway (mrb_basic_alloc_func reads the per-task heap,
 *      not the mrb_state). Allocation failure returns NULL here, since there
 *      is no VM to raise in.
 */
#include <string.h>

/* Implemented in main/app/fmrb_app.c: the mruby VM of the calling task, NULL
   when this task runs no mruby VM. Not weak on purpose -- a build that reaches
   prism without the fmrb runtime should fail to link rather than silently fall
   back to the process-wide pointer this patch exists to stop using. */
extern void *fmrb_current_compile_mrb(void);

/* A caller-installed allocator, realloc-shaped: (NULL, n) allocates, (p, 0)
   frees, (p, n) resizes. Callers declare this prototype themselves rather than
   through a shared header -- everything here is linked into one firmware, and
   an exported include path for two lines is more machinery than it deserves
   (same reasoning as editor_core.c's syntax-highlight prototype). */
typedef void *(*fmrb_prism_alloc_fn)(void *ptr, size_t size);

/* Per task, not per process. The installer and the prism call it serves are
   always the same task, while other tasks keep compiling apps meanwhile: a
   plain global would send their allocations into the installer's scratch heap,
   which is freed the moment that request ends -- a use-after-free in whichever
   app happened to be compiling. That is the same family of bug this patch
   exists to remove, and the caller's own lock does not help, since the compile
   path never takes it. */
static __thread fmrb_prism_alloc_fn s_prism_scratch = NULL;

void
fmrb_prism_set_scratch_allocator(fmrb_prism_alloc_fn fn)
{
  s_prism_scratch = fn;
}

void *
fmrb_prism_realloc(void *ptr, size_t size)
{
  if (s_prism_scratch) {
    return s_prism_scratch(ptr, size);
  }

  mrb_state *mrb = (mrb_state *)fmrb_current_compile_mrb();
  if (mrb) {
    if (size == 0) {
      mrb_free(mrb, ptr);
      return NULL;
    }
    return mrb_realloc(mrb, ptr, size);
  }

  return mrb_basic_alloc_func(ptr, size);
}

void *
fmrb_prism_calloc(size_t nmemb, size_t size)
{
  if (nmemb != 0 && size > (size_t)-1 / nmemb) return NULL;

  size_t total = nmemb * size;
  void *p = fmrb_prism_realloc(NULL, total);
  if (p) memset(p, 0, total);
  return p;
}
#endif

MRC_API mrc_ccontext *
mrc_ccontext_new(mrb_state *mrb)
{
  mrc_ccontext temp_c = {0};
#if defined(MRC_TARGET_MRUBY) && !defined(MRC_ALLOC_LIBC)
  global_mrb = mrb;
#endif
  temp_c.mrb = mrb;
  mrc_ccontext *c = (mrc_ccontext *)mrc_calloc((&temp_c), 1, sizeof(mrc_ccontext));
  c->p = (mrc_parser_state *)mrc_calloc((&temp_c), 1, sizeof(mrc_parser_state));
  c->mrb = temp_c.mrb;
  return c;
}


MRC_API void
mrc_ccontext_cleanup_local_variables(mrc_ccontext *cc)
{
  cc->keep_lv = FALSE;

  if (cc->options && cc->options->scopes) {
    for (int i = 0; i < cc->options->scopes[0].locals_count; i++) {
      mrc_free(cc, (void *)cc->options->scopes[0].locals[i].source);
    }
    mrc_free(cc, cc->options);
  }
}

MRC_API const char *
mrc_ccontext_filename(mrc_ccontext *c, const char *s)
{
  if (s) {
    size_t len = strlen(s);
    char *p = (char*)mrc_malloc(c, len + 1);

    if (p == NULL) return NULL;
    memcpy(p, s, len + 1);
    if (c->filename) {
      mrc_free(c, c->filename);
    }
    c->filename = p;
  }
  return c->filename;
}

MRC_API void
mrc_ccontext_free(mrc_ccontext *c)
{
  if (c->options) {
    /* pm_options_free() releases the scope and locals arrays but not the
       per-local name copies (they are PM_STRING_CONSTANT, which pm_string_free
       leaves alone) nor the options struct itself, so free those here. The
       copies must go first, before pm_options_free() releases the arrays. */
    for (size_t s = 0; s < c->options->scopes_count; s++) {
      pm_options_scope_t *scope = &c->options->scopes[s];
      for (size_t l = 0; l < scope->locals_count; l++) {
        mrc_free(c, (void *)scope->locals[l].source);
      }
    }
    pm_options_free(c->options);
    mrc_free(c, c->options);
    c->options = NULL;
  }
  if (c->filename_table) mrc_free(c, c->filename_table);
  if (c->filename) mrc_free(c, c->filename);
  pm_parser_free(c->p);
  mrc_diagnostic_list_free(c);
  if (c->p->lex_callback) {
    mrc_free(c, c->p->lex_callback);
  }
  mrc_free(c, c->p);
  mrc_free(c, c);
}
