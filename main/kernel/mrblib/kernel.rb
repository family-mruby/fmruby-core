# Family mruby OS - Kernel Main Loop
# This is the core of the Family mruby OS, running at 60Hz

# Kernel entry point
puts "Family mruby OS Kernel starting..."

begin
  kernel = FmrbKernel.new
  puts "[Kernel] Kernel created successfully"
  kernel.start
rescue => e
  puts "[Kernel] Exception caught: #{e.class}"
  puts "[Kernel] Message: #{e.message}"
  puts "[Kernel] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end

puts "Family mruby OS Kernel exit"
