# Fmrb::SpinelHello -- the smallest "Spinel as a gem": one method that returns a
# string produced by Spinel-compiled Ruby (spinel_hello_core.rb), reached from
# an mruby app. This gem is doc/spinel_aot/adding_a_spinel_gem.md made real.
#
#   h = Fmrb::SpinelHello.new
#   h.greet("world")   #=> "Hello world!"   (built by native code)
#   h.close
#
# The Spinel instance is created on the calling task and torn down on close;
# open/close are reference counted so several users share one instance.
#
# The instance is one for the whole machine and belongs to the first app that
# opened it. Another app greets with the same core on mruby instead -- slower,
# same string -- and #backend says which one is answering (:spinel / :ruby).
module Fmrb
  class SpinelHello
    attr_reader :backend

    def initialize
      @backend = :spinel
      @core = nil
      unless Fmrb::SpinelHello.open
        @backend = :ruby
        @core = ::SpinelHelloCore.new
        ::SpinelHelloNative.note_fallback("ruby")
      end
    end

    def greet(name)
      return @core.greet(name.to_s) if @core
      ::SpinelHelloNative.greet(name.to_s)
    end

    def close
      Fmrb::SpinelHello.close if @backend == :spinel
      @core = nil
      nil
    end

    # Returns true when this app holds the instance, false when another app
    # does (the caller then greets on :ruby). Raises when the instance cannot
    # be built at all.
    def self.open
      @refs ||= 0
      if @refs == 0
        raise RuntimeError, "the Spinel Hello backend is not in this build" unless ::SpinelHelloNative.available?
        rc = ::SpinelHelloNative.begin_instance
        return false if rc == ::SpinelHelloNative::BUSY
        raise RuntimeError, "could not start the Spinel Hello instance (#{rc})" if rc < 0
      end
      @refs += 1
      true
    end

    def self.close
      @refs ||= 0
      return if @refs == 0
      @refs -= 1
      ::SpinelHelloNative.end_instance if @refs == 0
    end
  end
end
