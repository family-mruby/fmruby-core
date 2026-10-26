# FmrbAsterism::Link: staying connected (doc/archive/app_refactor, R1).
#
# What every application on Asterism did for itself: wait for WiFi, connect
# with the machine's settings (FmrbAsterism.settings), poll, notice a lost
# connection, close it and keep the reason, and connect again every
# retry_ms -- also when the router still has the previous run of the same
# application ("already on the network"). The application writes once what
# it does on a connection (expose, subscribe, on_join / on_leave: Asterism
# forgets all of them at close) and calls update once per on_update.
#
#   def on_create
#     @link = FmrbAsterism::Link.new(self, "step", tag: "step_sensor")
#   end
#
#   def link_up(link)       # after every connect: expose, subscribe, on_join
#     ::Asterism.expose("pedometer", ...)
#   end
#
#   def link_down(link)     # optional: a connect failed or the connection
#   end                     # went away (link.state, link.reason say which)
#
#   def on_update
#     if @link.update        # true while the session is there
#       ...
#     end
#   end
#
#   def on_destroy
#     @link.close
#   end
#
# The owner's methods are called by name (link_up, link_down), not as
# blocks given to Link: nothing is captured, and the same object is the
# one an application already has.
#
# state (a Symbol):
#   :waiting_wifi    WiFi has no address yet (up to wifi_wait_ms, then it
#                    tries anyway); machines without WiFi do not wait
#   :waiting_screen  screen_wait_ms was given and screen_ready has not been
#                    called yet (up to screen_wait_ms)
#   :connecting      the next update connects. connect blocks (a TLS
#                    handshake takes 10-20 s), so the update that sets this
#                    returns first and the application draws "connecting"
#   :connected       connected
#   :reconnecting    connected, but the session is lost and the Zenoh
#                    binding opens it again by itself (Asterism.reconnecting?)
#   :retrying        a connect failed; again after retry_ms
#   :still_there     a connect was refused because this node/app is still on
#                    the network (the previous run); again after retry_ms
#   :lost            the connection went away (closed); again after retry_ms
#   :failed          a connect failed and retry_ms is nil (no more tries)
#   :closed          close was called
# reason: why it is not connected ("" while connected or before the first
# try): the connect's error, or "asterism: <Asterism.lost_reason>".
module FmrbAsterism
  class Link
    DEFAULT_RETRY_MS = 3000
    DEFAULT_WIFI_WAIT_MS = 20000

    attr_reader :state, :reason, :node, :router, :app, :connects, :took_ms

    # owner: the application (link_up(link) is called on it after every
    # connect, link_down(link) after a failure or a loss when it has one).
    # app: the application's name on the network (<node>/<app>).
    # tag: the prefix of the log lines. retry_ms: nil connects only once.
    # screen_wait_ms: nil does not wait for the screen.
    # settings: a FmrbAsterism::Settings (FmrbAsterism.settings by default).
    def initialize(owner, app, tag: nil, retry_ms: DEFAULT_RETRY_MS, wifi_wait_ms: DEFAULT_WIFI_WAIT_MS,
                   screen_wait_ms: nil, settings: nil)
      @owner = owner
      @app = app
      @tag = tag || app
      @retry_ms = retry_ms
      @wifi_wait_ms = wifi_wait_ms
      @screen_wait_ms = screen_wait_ms
      @screen_ready = screen_wait_ms.nil?
      st = settings || ::FmrbAsterism.settings
      @node = st.node
      @router = st.router
      @credentials = st.credentials
      @settings_error = st.error
      @started = now_ms
      @retry_at = 0
      @state = :waiting_wifi
      @reason = ""
      @connects = 0
      @took_ms = 0
    end

    # The session is there (also while Zenoh opens it again by itself).
    def connected?
      @state == :connected || @state == :reconnecting
    end

    # Connected and not reconnecting: what is put now reaches the others.
    def up?
      @state == :connected
    end

    # The application's screen is ready (on_resize after request_fullscreen):
    # the first connect no longer waits for it.
    def screen_ready
      @screen_ready = true
    end

    # Call once per on_update. Polls while connected, else connects when it
    # is time. Returns connected?.
    def update
      st = @state
      if st == :connected || st == :reconnecting
        poll
      elsif st == :connecting
        connect
      elsif st == :closed || st == :failed
        return false
      elsif st == :lost && @retry_ms.nil?
        return false
      else
        prepare
      end
      connected?
    end

    # The connection is given up now (an error of the application's own
    # network work, say): closed, why kept, and tried again after retry_ms.
    def lost(why)
      ::Log.warn("#{@tag}: lost: #{why}")
      @reason = why.to_s
      close_session
      @state = :lost
      @retry_at = now_ms + (@retry_ms || 0)
      tell_down
      nil
    end

    # Closes the connection for good (on_destroy).
    def close
      close_session
      @state = :closed
      nil
    end

    # ---------------------------------------------------------------- inside

    def now_ms
      ::Machine.board_millis
    end

    # Before a connect: the retry time, WiFi, the screen; then one update
    # in :connecting, for the application to draw it.
    def prepare
      now = now_ms
      return if now < @retry_at
      unless wifi_ready?(now)
        @state = :waiting_wifi
        return
      end
      unless @screen_ready || now - @started > @screen_wait_ms
        @state = :waiting_screen
        return
      end
      @state = :connecting
    end

    # WiFi has an address, the machine has no WiFi (wifi_info nil), or
    # wifi_wait_ms has gone by since the Link was made (try anyway).
    def wifi_ready?(now)
      return true unless Object.const_defined?(:FmrbApp)
      info = ::FmrbApp.wifi_info
      return true if info.nil? || info[:connected]
      now - @started > @wifi_wait_ms
    end

    def connect
      t0 = now_ms
      close_session
      begin
        raise ::Asterism::Error, @settings_error if @settings_error
        words = @router.to_s.split(" ")
        if words[0] == "listen"
          ::Asterism.connect(nil, node: @node, app: @app, mode: :peer, listen: words[1])
        elsif words[0] == "peer"
          ::Asterism.connect(words[1], node: @node, app: @app, mode: :peer)
        else
          ::Asterism.connect(@router, node: @node, app: @app, credentials: @credentials)
        end
        @took_ms = now_ms - t0
        @state = :connected
        @reason = ""
        @owner.link_up(self)
        @connects += 1
        ::Log.info("#{@tag}: #{@node}/#{@app} connected (#{@router}, #{@took_ms} ms)")
      rescue => e
        @took_ms = now_ms - t0
        msg = e.is_a?(::Asterism::Error) ? e.message.to_s : "#{e.class}: #{e.message}"
        ::Log.warn("#{@tag}: connect: #{msg}")
        @reason = msg
        close_session
        if @retry_ms.nil?
          @state = :failed
        else
          @state = msg.include?("already on the network") ? :still_there : :retrying
          @retry_at = now_ms + @retry_ms
        end
        tell_down
      end
    end

    def poll
      ok = false
      begin
        ok = ::Asterism.poll
      rescue => e
        return lost("#{e.class}: #{e.message}")
      end
      return lost("asterism: #{::Asterism.lost_reason}") unless ok && ::Asterism.connected?
      @state = ::Asterism.reconnecting? ? :reconnecting : :connected
    end

    def close_session
      ::Asterism.close
    rescue => e
      ::Log.warn("#{@tag}: close: #{e.message}")
    end

    def tell_down
      @owner.link_down(self) if @owner.respond_to?(:link_down)
    end
  end
end
