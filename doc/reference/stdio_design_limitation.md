# stdio Design Limitation: Global $stdout/$stdin in Sandbox Execution

## Overview

When running headless scripts via the Shell's `run` command, the script's
standard I/O (`puts`, `print`, `gets`) is redirected by swapping the global
variables `$stdout` and `$stdin` before Sandbox execution and restoring them
afterwards. This works but has inherent limitations due to the global nature
of these variables.

## Current Implementation

```ruby
old_stdout = $stdout
$stdout = ShellStdout.new(self)  # redirects puts/print to Shell display
sandbox.execute
$stdout = old_stdout             # restore
```

`Kernel#puts` and `Kernel#print` (defined in picoruby-machine/mrblib/kernel.rb)
delegate to `$stdout.puts` / `$stdout.print`, so swapping `$stdout` effectively
redirects all output from the script.

## Known Risks

### 1. Ensure-based restoration is fragile

If an exception occurs between the swap and the restore, `$stdout` remains
replaced. The Shell itself uses `$stdout` for internal operations (`on_resize`
log output, etc.), so a leaked swap can break the Shell. All swap sites must
use `ensure` blocks, but this is a convention, not a structural guarantee.

### 2. Background job interference (cooperative scheduling)

mruby Tasks use cooperative scheduling within a single FreeRTOS task.
Only one Task runs at a time, so there is no true concurrency. However,
`$stdout` is a single global variable shared across all Tasks:

```
Task A: $stdout = ShellStdout_A  ->  yield (sleep/IO)
Task B: $stdout = ShellStdout_B  ->  yield
Task A: puts "hello"  <- writes to ShellStdout_B, not _A
```

The current implementation avoids this by ensuring each Task swaps and
restores `$stdout` within its own execution slice. This works because
cooperative scheduling guarantees no preemption mid-slice, but it relies
on correct swap/restore discipline in every Task.

### 3. Nesting is unsafe

If a script running under `run` creates its own Sandbox or calls `irb`,
the `$stdout` swap becomes nested. The inner swap overwrites the outer
one, and restoring in the wrong order corrupts the state. Currently there
is no nesting detection or stack-based save/restore mechanism.

## Ideal Solution (Future Work)

Each Sandbox should have its own I/O context, independent of global variables:

```ruby
sandbox = Sandbox.new("run")
sandbox.stdout = ShellStdout.new(self)
sandbox.stdin  = ShellStdin.new(self)
sandbox.execute
# $stdout is never touched
```

This requires changes at the mruby C level:
- Add `stdout`/`stdin` fields to the Sandbox/Task C structure (`SandboxState`)
- Modify `Kernel#puts` / `Kernel#print` to check the current Task's I/O
  context first, falling back to `$stdout` only if no task-local I/O is set
- This would eliminate all three risks described above

### Trade-offs

- Increased complexity in the mruby VM integration layer
- Small memory overhead per Sandbox (two additional mrb_value fields)
- Requires careful handling of the "default" case (no task-local I/O set)

## References

- `shell.app.rb`: `ShellStdout`, `ShellStdin`, `run_foreground`, `run_background`
- `picoruby-machine/mrblib/kernel.rb`: `Kernel#puts`, `Kernel#print` definitions
- `picoruby-sandbox/src/mruby/sandbox.c`: `SandboxState` structure
- `picoruby-task-ext/mrblib/task.rb`: Task class (wraps Sandbox)
