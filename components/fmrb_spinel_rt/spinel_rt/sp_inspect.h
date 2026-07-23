#ifndef SP_INSPECT_H
#define SP_INSPECT_H
/* sp_inspect.h -- generic container #inspect, split out of sp_runtime.h.

   sp_inspect_container formats a boxed array or hash in Ruby #inspect form,
   walking it through the generic sp_json_* / sp_poly_inspect_fn hooks (sp_gc.h)
   and building the result with sp_String (sp_string.h). The per-type inspect
   helpers in sp_runtime.h are now one-line wrappers that box their receiver and
   call this, so all the inspect string-building compiles once in the archive
   rather than in every generated TU. */
#include "sp_gc.h"   /* sp_RbVal */

const char *sp_inspect_container(sp_RbVal v);
#endif /* SP_INSPECT_H */
