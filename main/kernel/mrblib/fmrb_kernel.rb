# Family mruby OS - Kernel Main Loop
# This is the core of the Family mruby OS, running at 60Hz


Log.info "Family mruby OS Kernel starting..."

begin
  kernel = FmrbKernel.new
  Log.info "Kernel created successfully"
  kernel.start
rescue => e
  Log.error "Exception caught: #{e.class}"
  Log.error "Message: #{e.message}"
  Log.error "Backtrace:"
  Log.error e.backtrace.join("\n") if e.backtrace
end

Log.info "Family mruby OS Kernel exit"
