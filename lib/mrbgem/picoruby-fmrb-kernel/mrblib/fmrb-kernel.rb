# Family mruby OS - Kernel Framework
# Kernel provides system-level services for the Family mruby OS
#
# Responsibilities:
#   - Process/App lifecycle management
#   - Inter-Process Communication (IPC)
#   - System resource management
#   - Event dispatching to apps
#
# System calls:
#   - Kernel.spawn_app(app_name, app_irep)
#   - Kernel.kill_app(proc_id)
#   - Kernel.send_message(proc_id, message)
#   - Kernel.receive_message()
#

module Kernel
  # System information
  def self.version
    "Family mruby OS v0.1.0"
  end

  # Process management (to be implemented)
  def self.spawn_app(app_name, app_irep)
    # TODO: Implement app spawning
    raise NotImplementedError, "spawn_app not yet implemented"
  end

  def self.kill_app(proc_id)
    # TODO: Implement app termination
    raise NotImplementedError, "kill_app not yet implemented"
  end

  # IPC (to be implemented)
  def self.send_message(proc_id, message)
    # TODO: Implement message sending
    raise NotImplementedError, "send_message not yet implemented"
  end

  def self.receive_message
    # TODO: Implement message receiving
    raise NotImplementedError, "receive_message not yet implemented"
  end

  # System resource info
  def self.free_memory
    # TODO: Get free memory from C layer
    0
  end

  def self.task_count
    # TODO: Get task count from C layer
    0
  end
end
