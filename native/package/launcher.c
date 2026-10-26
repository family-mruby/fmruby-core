/*
 * launcher.c - one machine's entry of a package (doc/linux_native/, L3a).
 *
 * APPLaunch starts a program from a .desktop file whose Exec may not take
 * arguments (no shell, no metacharacters), so each machine gets a small
 * executable of its own: family-mruby-<suffix> runs the program installed
 * beside it, family-mruby, with --machine <machine> in front of whatever it
 * was given. Built by native/CMakeLists.txt with FMRB_LAUNCH_PROGRAM and
 * FMRB_LAUNCH_MACHINE; it replaces itself (execv), so APPLaunch's signals
 * and exit status are the program's.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef FMRB_LAUNCH_PROGRAM
#error "FMRB_LAUNCH_PROGRAM must be set by native/CMakeLists.txt"
#endif
#ifndef FMRB_LAUNCH_MACHINE
#error "FMRB_LAUNCH_MACHINE must be set by native/CMakeLists.txt"
#endif

#define MAX_ARGS 64

int main(int argc, char **argv)
{
    char dir[4096];
    ssize_t n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    if (n <= 0) {
        fprintf(stderr, "family-mruby launcher: /proc/self/exe: %s\n", strerror(errno));
        return 127;
    }
    dir[n] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash != NULL) *slash = '\0';
    char program[4096 + sizeof(FMRB_LAUNCH_PROGRAM) + 1];
    snprintf(program, sizeof(program), "%s/%s", dir, FMRB_LAUNCH_PROGRAM);

    char *args[MAX_ARGS + 4];
    int k = 0;
    args[k++] = program;
    args[k++] = "--machine";
    args[k++] = FMRB_LAUNCH_MACHINE;
    for (int i = 1; i < argc && k < MAX_ARGS + 3; i++) args[k++] = argv[i];
    args[k] = NULL;
    execv(program, args);
    fprintf(stderr, "family-mruby launcher: cannot run %s: %s\n", program, strerror(errno));
    return 127;
}
