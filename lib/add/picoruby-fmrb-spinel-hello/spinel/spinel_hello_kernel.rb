# spinel-ext-init: Init_spinel_hello
# spinel-ext-entry: SpinelHelloKernel.greet
#
# The Spinel side of the minimal sample, compiled as a Spinel ext program:
#
#   spinel spinel_hello_kernel.rb -c --ext-init Init_spinel_hello \
#     --ext-entry SpinelHelloKernel.greet
#
# (rake spinel:gen reads the two lines above, so the names live in one place).
# That emits spinel_hello_kernel.c and the header spinel_hello_kernel.h, which
# declares the entry as a plain C function:
#
#   const char *sp_SpinelHelloKernel_s_greet(const char *lv_name);
#
# native/spinel_hello_native.c calls Init_spinel_hello() once per instance and
# then the entry, with the name as a Spinel String and the greeting as the
# return value. See doc/spinel_aot/adding_a_spinel_gem.md.
require_relative "spinel_hello_core"

module SpinelHelloKernel
  def self.greet(name)
    SpinelHelloCore.new.greet(name)
  end
end

# Type inference driver: the entry's argument type is taken from this call.
# Left out of Init_spinel_hello.
if __FILE__ == $0
  puts SpinelHelloKernel.greet("world")
end
