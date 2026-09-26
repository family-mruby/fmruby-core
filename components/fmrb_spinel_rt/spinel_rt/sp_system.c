/* sp_system.c -- system()/backtick support in libspinel_rt.a.
 * See sp_system.h.
 *
 * Self-contained (libc + OS process API only); does not include
 * spinel_rt.h, so it carries its own sp_bool/TRUE/FALSE locally. */
#include "sp_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

typedef int sp_bool;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_last_status = 0;
#endif


#if defined(SP_NO_PROCESS) || defined(SP_NO_MMAN)
/* No fork/exec on this port (SP_NO_PROCESS, sp_types.h -- this file keeps to
   libc headers, so it tests the knobs itself). */
extern void sp_raise_cls(const char *cls, const char *msg) __attribute__((noreturn));
int sp_system_args(int argc, const char *const *argv) {
  (void)argc; (void)argv;
  sp_raise_cls("NotImplementedError", "Kernel#system is not supported on this port");
}
#else
int sp_system_args(int argc, const char *const *argv) {
  if (argc <= 0 || argv == NULL || argv[0] == NULL) {
    sp_last_status = -1;
    return FALSE;
  }
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) {
    sp_last_status = -1;
    return FALSE;
  }
  if (pid == 0) {
    if (argc == 1) {
      execl("/bin/sh", "sh", "-c", argv[0], (char *)NULL);
    }
    else {
      execvp(argv[0], (char * const *)argv);
    }
    _exit(127);
  }
  /* Same rule as Process.waitpid2 (#4381): a blocking wait answers for the OS
     worker, and a started green thread is pinned to its worker, so it would
     stall the very thread that may have to drain this child's output before it
     can exit. sp_sched_wait_child polls and hands the scheduler back while any
     other thread is alive, and keeps the blocking wait when none is. */
  int status = 0;
  {
    extern int sp_sched_wait_child(int pid, int *status);
    if (sp_sched_wait_child((int)pid, &status) < 0) { sp_last_status = -1; return FALSE; }
  }
  sp_last_status = status;
  return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? TRUE : FALSE;
}
#endif /* SP_NO_PROCESS */
