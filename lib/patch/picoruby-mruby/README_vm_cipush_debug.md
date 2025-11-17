# vm.c cipush() Debug Patch

## Purpose

This patch adds debug logging to detect when `mrb_realloc()` moves the `cibase` pointer in `cipush()` function.

## Target File

`components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/vm.c`

## How to Apply

### Option 1: Manual Patch

Replace the `cipush()` function (lines 354-385 in vm.c) with the content from `vm_cipush_debug.c`.

### Option 2: Compile-time Flag

Add the following to your build configuration to enable debug logging:

```cmake
add_definitions(-DFMRB_DEBUG_CI_REALLOC)
```

Or in sdkconfig:
```
CONFIG_COMPILER_OPTIMIZATION_DEBUG=y
CFLAGS += -DFMRB_DEBUG_CI_REALLOC
```

## Expected Debug Output

When `cipush()` triggers a realloc, you'll see:

```
[CIPUSH REALLOC] BEFORE: cibase=0x3fc00000 ci=0x3fc00400 ciend=0x3fc00800 size=32 new_size=64
[CIPUSH REALLOC] AFTER:  cibase=0x3fc10000 ci=0x3fc10400 ciend=0x3fc10c00 (moved=YES)
[CIPUSH REALLOC] WARNING: cibase moved from 0x3fc00000 to 0x3fc10000 (delta=65536 bytes)
```

## Analysis

- If `moved=YES`, all old `ci` pointers become invalid
- This is the likely cause of crashes when returning from `mrb_funcall()`
- The delta shows how far the memory block moved

## Disable Patch

To disable debug logging, remove the `-DFMRB_DEBUG_CI_REALLOC` flag from build configuration.

## Note

This patch is for debugging purposes only. For production, consider:
1. Pre-allocating a larger cibase to avoid realloc
2. Using offset/index instead of direct pointers
3. Adding range checks before accessing ci pointers
