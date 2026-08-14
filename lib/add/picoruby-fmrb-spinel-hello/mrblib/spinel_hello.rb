# Fmrb::SpinelHello -- the smallest "Spinel as a gem": one method that returns a
# string produced by Spinel-compiled Ruby (spinel_hello_core.rb), reached from
# an mruby app. This gem is doc/spinel_aot/adding_a_spinel_gem.md made real.
#
#   h = Fmrb::SpinelHello.new
#   h.greet   #=> "Hello Spinel"   (built by native code)
#   h.close
#
# The Spinel instance is created on the calling task and torn down on close;
# open/close are reference counted so several users share one instance.
module Fmrb
  class SpinelHello
    def initialize
      Fmrb::SpinelHello.open
    end

    def greet
      ::SpinelHelloNative.greet
    end

    def close
      Fmrb::SpinelHello.close
    end

    def self.open
      @refs ||= 0
      if @refs == 0
        raise RuntimeError, "the Spinel Hello backend is not in this build" unless ::SpinelHelloNative.available?
        rc = ::SpinelHelloNative.begin_instance
        raise RuntimeError, "could not start the Spinel Hello instance (#{rc})" if rc < 0
      end
      @refs += 1
    end

    def self.close
      @refs ||= 0
      return if @refs == 0
      @refs -= 1
      ::SpinelHelloNative.end_instance if @refs == 0
    end
  end
end
