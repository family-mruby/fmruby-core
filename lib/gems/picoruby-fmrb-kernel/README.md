# picoruby-fmrb-kernel

Family mruby OS Kernel API for PicoRuby

## Overview

This mrbgem provides the kernel-level API for Family mruby OS. The kernel is responsible for:

- **Process Management**: Spawning and terminating apps
- **Inter-Process Communication (IPC)**: Message passing between processes
- **System Resource Management**: Memory, tasks, and hardware resources
- **Event Dispatching**: Routing input events to appropriate apps

## Architecture

```
┌─────────────────────────────────────┐
│          User Apps                  │
│  (inherits from FmrbApp)            │
├─────────────────────────────────────┤
│      picoruby-fmrb-app              │
│   (App Framework & Lifecycle)       │
├─────────────────────────────────────┤
│    picoruby-fmrb-kernel             │
│  (System Calls & IPC)               │
├─────────────────────────────────────┤
│    C Layer (fmrb_app.c, etc.)       │
│  (FreeRTOS, Hardware Abstraction)   │
└─────────────────────────────────────┘
```

## Usage

### System Information

```ruby
puts Kernel.version      # => "Family mruby OS v0.1.0"
puts Kernel.free_memory  # => Available memory in bytes
puts Kernel.task_count   # => Number of active tasks
```

### Process Management (TODO)

```ruby
# Spawn a new app
Kernel.spawn_app("my_app", app_irep)

# Terminate an app
Kernel.kill_app(proc_id)
```

### Inter-Process Communication (TODO)

```ruby
# Send message to another process
Kernel.send_message(proc_id: 2, message: "Hello")

# Receive message from message queue
msg = Kernel.receive_message()
```

## Development Status

- [x] Basic gem structure
- [ ] Process management bindings
- [ ] IPC bindings
- [ ] System resource monitoring
- [ ] Event dispatching

## License

MIT
