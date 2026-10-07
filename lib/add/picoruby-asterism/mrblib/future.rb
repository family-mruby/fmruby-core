module Asterism
  # The answer to a call that is on its way (Proxy#async). done? never
  # waits; value waits (polling, and answering calls meanwhile) until the
  # answer or the time limit, then returns the value or raises.
  class Future
    attr_reader :path, :method_name

    def self.answered(path, method, reply)
      f = new(path, method, nil, 0, 0)
      f.set_reply(reply)
      f
    end

    def initialize(path, method, get, deadline_ms, timeout_ms)
      @path = path
      @method_name = method.to_s
      @get = get
      @deadline = deadline_ms
      @timeout_ms = timeout_ms
      @reply = nil
      @finished = false
      @started = ::Asterism.now_ms
      @took = nil
    end

    def set_reply(reply)
      @reply = reply
      @finished = true
      @took = 0
    end

    # Milliseconds from the call to its answer (nil until done).
    def took_ms
      @took
    end

    # True once the answer came, the time ran out or the connection closed.
    # Call Asterism.poll in the update loop for it to move on.
    def done?
      collect
      @finished
    end

    def value
      raw = raw_value
      unless raw.is_a?(Array) && (raw[0] == "ok" || raw[0] == "error")
        raise ::Asterism::Error, "malformed answer from #{@path}"
      end
      raise ::Asterism::RemoteError.new(raw[1], raw[2]) if raw[0] == "error"
      raw[1]
    end

    # The decoded answer (meta replies are not ["ok", value] pairs).
    def raw_value
      ::Asterism.wait_until { done? } unless done?
      if @reply.nil?
        raise ::Asterism::Disconnected, (::Asterism.lost_reason || "not connected") unless ::Asterism.connected?
        if @took && @took < @timeout_ms
          # Finished early without a reply: nobody answers that key (the
          # object is not exposed, or its application is gone).
          raise ::Asterism::Timeout, "no answer from #{@path} #{@method_name} (nobody answers)"
        end
        raise ::Asterism::Timeout, "no answer from #{@path} #{@method_name} within #{@timeout_ms} ms"
      end
      raw = ::Asterism::Codec.unpack(@reply)
      raise ::Asterism::Error, "malformed answer from #{@path}" if raw.nil?
      raw
    end

    def collect
      return if @finished
      g = @get
      # The Array form, not a block: a block called from C costs another
      # interpreter entry on the C stack, and this runs inside nested waits.
      replies = g.each_reply
      @reply = replies[0][1] if @reply.nil? && replies.size > 0
      if !@reply.nil? || g.done? || ::Asterism.now_ms >= @deadline || !::Asterism.connected?
        @finished = true
        @took = ::Asterism.now_ms - @started
      end
    end
  end
end
