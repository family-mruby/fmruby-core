# Family mruby OS - Kernel Main Loop
# This is the core of the Family mruby OS, running at 60Hz

# Kernel entry point
puts "Family mruby OS Kernel starting..."

begin
  kernel_handler = KernelHandler.new
  puts "[Kernel] KernelHandler created successfully"
  kernel_handler.start
rescue => e
  puts "[ERROR] Exception caught: #{e.class}"
  puts "[ERROR] Message: #{e.message}"
  puts "[ERROR] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end

puts "Family mruby OS Kernel exit"
