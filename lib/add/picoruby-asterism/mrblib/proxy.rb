module Asterism
  # A stand-in for an object on another machine (Asterism["node/app/obj"]).
  # Any method it does not have itself becomes a call that waits for the
  # answer. respond_to? and methods come from the object's meta.
  #
  # Its own methods (not forwarded): async, asterism_path, asterism_meta,
  # asterism_refresh, respond_to?, methods, inspect, to_s and those of Object.
  class Proxy
    # Conversions Ruby asks for implicitly (puts, Array#flatten, ...): never
    # forwarded.
    LOCAL_ONLY = [:to_ary, :to_str, :to_hash, :to_proc, :to_a, :to_io]

    def initialize(path, timeout_ms)
      @path = path
      @timeout_ms = timeout_ms
      @meta = nil
    end

    def asterism_path
      @path
    end

    def method_missing(name, *args, **kw, &blk)
      return super if ::Asterism::Proxy::LOCAL_ONLY.include?(name)
      raise ::ArgumentError, "a block cannot be sent to #{@path}" if blk
      ::Asterism.call(@path, name, args, kw, @timeout_ms)
    end

    def respond_to_missing?(name, include_private = false)
      return false if ::Asterism::Proxy::LOCAL_ONLY.include?(name.to_sym)
      remote_names.include?(name.to_s)
    rescue ::Asterism::Error
      false
    end

    # The exposed methods (Symbols), from the meta. Raises when the object
    # does not answer.
    def methods(*_args)
      remote_names.map { |n| n.to_sym }
    end

    # The meta reply, fetched once: {"methods" => [[name, arity], ...]}.
    def asterism_meta
      @meta ||= ::Asterism.meta(@path, @timeout_ms)
    end

    # Forget the meta (the object may have been exposed again).
    def asterism_refresh
      @meta = nil
      self
    end

    def remote_names
      list = asterism_meta["methods"]
      return [] unless list.is_a?(Array)
      list.map { |pair| pair.is_a?(Array) ? pair[0].to_s : pair.to_s }
    end

    # The same object, calls that do not wait: each returns a Future.
    def async
      AsyncProxy.new(@path, @timeout_ms)
    end

    def inspect
      "#<Asterism::Proxy #{@path}>"
    end

    def to_s
      inspect
    end
  end

  class AsyncProxy
    def initialize(path, timeout_ms)
      @path = path
      @timeout_ms = timeout_ms
    end

    def method_missing(name, *args, **kw, &blk)
      return super if ::Asterism::Proxy::LOCAL_ONLY.include?(name)
      raise ::ArgumentError, "a block cannot be sent to #{@path}" if blk
      ::Asterism.call_async(@path, name, args, kw, @timeout_ms)
    end

    def respond_to_missing?(name, _include_private = false)
      !::Asterism::Proxy::LOCAL_ONLY.include?(name.to_sym)
    end

    def inspect
      "#<Asterism::AsyncProxy #{@path}>"
    end
  end
end
