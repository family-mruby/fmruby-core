# Fix: Sandbox initialization crash (SIGSEGV in mrb_gc_mark)

## Symptom

Running `irb` command in the shell causes SIGSEGV in `mrb_gc_mark`, called from
`mrb_task_mark_all` during incremental GC. The crash occurs at the first
`Sandbox.new` call. Register dump shows `rsi=0x2c0` (invalid pointer).

```
Thread 9 received signal SIGSEGV, Segmentation fault.
0x... in mrb_gc_mark ()
#1  mrb_task_mark_all ()
#2  incremental_gc ()
...
#N  mrb_sandbox_initialize ()
```

## Root cause

In `picoruby-sandbox/src/mruby/sandbox.c`, `mrb_sandbox_initialize()`:

```c
mrb_value name;                        // <-- uninitialized!
mrb_get_args(mrb, "|S", &name);       // optional arg: no-op when absent
if (mrb_nil_p(name)) {                // garbage value is not nil -> skipped
    name = mrb_str_new_cstr(mrb, "sandbox");
}
```

`mrb_get_args` with `|S` does NOT modify the variable when the optional argument
is not provided. The uninitialized stack variable `name` retains a garbage value
(`0x2c0`), which is then stored in `t->name` of the task struct via
`mrc_create_task`. When GC runs and `mrb_task_mark_all` tries to mark the task's
name, it calls `mrb_gc_mark` with the garbage pointer, causing SIGSEGV.

## Fix

### 1. sandbox.c - Initialize `name` variable (root cause fix)

```diff
- mrb_value name;
+ mrb_value name = mrb_nil_value();
  mrb_get_args(mrb, "|S", &name);
```

### 2. task.c - `mrb_create_task`: protect name from GC

Added `mrb_gc_protect(mrb, name_val)` before allocations that may trigger GC.
The task is not yet in any queue at this point, so `mrb_task_mark_all` would not
visit it during GC. Without protection, the name string could be collected.

### 3. task.c - `mrb_task_init_context`: clear stale pointers after free

After freeing `stbase` and `cibase`, added `c->ci = NULL; c->fib = NULL;` to
prevent GC from following stale pointers if `mrb_malloc` inside
`task_init_context` triggers a collection.

### 4. task.c - `mrb_task_reset_context`: full context reset

The original implementation only reset `c->ci = c->cibase` and
`c->status = MRB_TASK_CREATED`, leaving stale data in `cibase[0]` (proc,
target_class, stack, n, nk) and `c->fib`. The new implementation:

- Saves the current proc from whichever ci frame has it
- Zeros `cibase[0]` completely before re-initializing
- Sets `c->fib = NULL`
- Re-initializes `ci->stack`, `ci->pc`, `ci->proc`, `ci->target_class`
- Clears the entire VM stack to nil

This prevents GC from marking stale pointers when the sandbox's execute/reset
cycle reuses a task context.

## Files modified

- `fmruby-core/components/picoruby-esp32/picoruby/mrbgems/picoruby-sandbox/src/mruby/sandbox.c`
- `fmruby-core/components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c`

## Notes

- The root cause fix is item 1 (sandbox.c) only. Items 2-4 (task.c) are
  defensive fixes for latent issues that were discovered during investigation
  but did not directly cause this crash.
- The `0x2c0` value was consistent across runs because it came from the same
  stack frame layout of `mrb_sandbox_initialize`.
- This same uninitialized variable pattern may exist in other `mrb_get_args`
  call sites with optional arguments (`|` format specifier).
