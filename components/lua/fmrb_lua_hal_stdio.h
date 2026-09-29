/*
 * fmrb_lua_hal_stdio.h -- send the Lua core's file access through the fmrb file HAL.
 *
 * Force-included (-include) into every Lua core source (components/lua/lua/,
 * a submodule that is not edited). The Lua libraries open, reopen, delete and
 * rename files with the C library's stdio calls (io.open, io.lines, io.input,
 * io.output, io.tmpfile, os.remove, os.rename, os.tmpname, loadfile, dofile,
 * require). Those reach the VFS directly and skip the file HAL's lock, so an
 * app killed in the middle of one could leave littlefs locked for every other
 * task (doc/fs_kill_hang). The macros below turn each of those calls into one
 * that goes through the HAL; the stream the Lua code gets back is an ordinary
 * FILE (fopencookie) whose reads, writes, seeks and close go to the HAL
 * handle, so every other stdio call in the Lua core works on it unchanged.
 *
 * The standard streams (stdin / stdout / stderr) are not opened by name and
 * are left alone.
 *
 * Order matters: lprefix.h sets the feature macros every Lua source expects
 * before its first system header, and <stdio.h> must be parsed before the
 * macros exist (they would otherwise rewrite its prototypes).
 */
#ifndef FMRB_LUA_HAL_STDIO_H
#define FMRB_LUA_HAL_STDIO_H

#include "lprefix.h"
#include <stdio.h>

FILE *fmrb_lua_fopen(const char *path, const char *mode);
FILE *fmrb_lua_freopen(const char *path, const char *mode, FILE *stream);
int   fmrb_lua_remove(const char *path);
int   fmrb_lua_rename(const char *from, const char *to);
FILE *fmrb_lua_tmpfile(void);
char *fmrb_lua_tmpnam(char *buf);

#define fopen(p, m)      fmrb_lua_fopen((p), (m))
#define freopen(p, m, f) fmrb_lua_freopen((p), (m), (f))
#define remove(p)        fmrb_lua_remove(p)
#define rename(a, b)     fmrb_lua_rename((a), (b))
#define tmpfile()        fmrb_lua_tmpfile()
#define tmpnam(b)        fmrb_lua_tmpnam(b)

#endif /* FMRB_LUA_HAL_STDIO_H */
