/* sp_system.h -- system()/backtick support in libspinel_rt.a.
 *
 * `sp_last_status` backs Ruby's `$?` (the exit status of the last
 * `system` / backtick). `sp_system_args` runs argv[0] with the rest as
 * arguments (the multi-arg `system(...)` form), setting `sp_last_status`
 * and returning whether the child exited 0. The bool result is declared
 * as `int` to stay decoupled from the runtime's typedefs.
 */
#ifndef SP_SYSTEM_H
#define SP_SYSTEM_H

#ifdef SP_MULTI_CTX
/* per-instance: the current instance's field (sp_ctx.c), reached through a
   call because this header stays free of the runtime's own headers */
int *sp_ctx_last_status(void);
#define sp_last_status (*sp_ctx_last_status())
#else
extern int sp_last_status;
#endif
int sp_system_args(int argc, const char *const *argv);

#endif
