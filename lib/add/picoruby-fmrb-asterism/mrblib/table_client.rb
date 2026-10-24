# FmrbAsterism::TableClient: the device side of a game host's table
# (doc/app_refactor, R4). The othello table, the drawing board and mahjong
# each copied the same part: staying connected (now FmrbAsterism::Link),
# the list of the hosts' tables (looked for on on_join for rescan_ms, else
# every list_every_ms, and from the state puts), joining one (the token)
# and leaving it, asking the member's view when a put shows a newer
# version and every view_every_ms anyway (which also tells the host it is
# still there), and a host that leaves the network and comes back. This
# class holds that part; the game (the seats, the moves, the strokes, the
# screen) stays in the application.
#
#   def on_create
#     @tc = FmrbAsterism::TableClient.new(self, "othello", tag: "othello_table")
#   end
#
#   def on_update
#     if @tc.update               # connected: states, the list, the join
#       ...the game's own calls (collect its answers)
#       @tc.update_view           # the view: collect it, ask it when due
#     end
#   end
#
#   def on_destroy
#     leave_table if @tc.token     # the app's own: @tc.leave and its state
#     @tc.close
#   end
#
# The application's methods are called by name (as Link calls link_up),
# never as stored blocks. Required:
#   table_state(path, state)  a put of one of the game's tables (a Hash);
#                             returns what the list keeps for it
#                             (tables[path])
#   table_joined(reply)       join answered with a token (token is set);
#                             the view is asked right after
#   table_refused(why)        join refused (why: the host's word as a
#                             String) or not answered (why: nil); table is nil
#   table_view(view)          the member's view (a Hash)
# Optional (called when the application has them):
#   table_link_up(link)       after every connect (the states' subscription
#                             and on_join / on_leave are given already)
#   table_link_down(link)     a connect failed or the connection went away
#   table_watch               every update while connected, after the poll
#                             and before the states are read
#   tables_changed(added)     a table came onto the list (true) or a host's
#                             tables left it (false)
#   table_host_gone           this table's host left the network
#   table_host_back           it came back (the view is asked again)
#   table_view_failed         a view was not answered
#   table_dropped             the host does not know this member any more;
#                             without it the client leaves (leave)
module FmrbAsterism
  class TableClient
    HOST_APP = "games"
    CALL_TIMEOUT = 3.0
    VIEW_EVERY_MS = 3000
    LIST_EVERY_MS = 30000
    RESCAN_MS = 3000
    RESCAN_STEP_MS = 300
    HOST_GONE_VIEW_MS = 30000

    attr_reader :link, :kind, :tables, :table_list, :table, :token, :want_v, :view_v
    # The periods (ms): a view every view_every_ms, and on a newer version
    # once view_min_ms has gone by since the last; tables every
    # list_every_ms, every rescan_step_ms for rescan_ms after on_join; with
    # the host gone a view every host_gone_view_ms (should on_join not come).
    attr_accessor :call_timeout, :view_every_ms, :view_min_ms, :list_every_ms,
                  :rescan_ms, :rescan_step_ms, :host_gone_view_ms

    # owner: the application. kind: the start of the game's table names
    # ("othello" finds <host>/games/othello1). app: this application's name
    # on the network. wifi_wait_ms: nil waits on the ESP32 only (the sim and
    # the browser have their network at once).
    def initialize(owner, kind, tag: nil, app: "game", host_app: HOST_APP, retry_ms: 3000,
                   wifi_wait_ms: nil, settings: nil)
      @owner = owner
      @kind = kind
      @tag = tag || kind
      @host_app = host_app
      @call_timeout = CALL_TIMEOUT
      @view_every_ms = VIEW_EVERY_MS
      @view_min_ms = 0
      @list_every_ms = LIST_EVERY_MS
      @rescan_ms = RESCAN_MS
      @rescan_step_ms = RESCAN_STEP_MS
      @host_gone_view_ms = HOST_GONE_VIEW_MS
      @tables = {}
      @table_list = []
      @scan_at = 0
      @rescan_until = 0
      @sub = nil
      @table = nil
      @token = nil
      @join_f = nil
      @view_f = nil
      @view_at = 0
      @want_v = 0
      @view_v = 0
      @host_gone = false
      @gone_at = 0
      @on_up = owner.respond_to?(:table_link_up)
      @on_down = owner.respond_to?(:table_link_down)
      @on_watch = owner.respond_to?(:table_watch)
      @on_changed = owner.respond_to?(:tables_changed)
      @on_gone = owner.respond_to?(:table_host_gone)
      @on_back = owner.respond_to?(:table_host_back)
      @on_failed = owner.respond_to?(:table_view_failed)
      @on_dropped = owner.respond_to?(:table_dropped)
      if wifi_wait_ms.nil?
        wifi_wait_ms = ::FmrbConst::PLATFORM == "esp32" ? ::FmrbAsterism::Link::DEFAULT_WIFI_WAIT_MS : 0
      end
      @link = ::FmrbAsterism::Link.new(self, app, tag: @tag, retry_ms: retry_ms, wifi_wait_ms: wifi_wait_ms,
                                       settings: settings)
    end

    # ------------------------------------------------------------ the loop

    # Call once per on_update: the connection, then (connected) the
    # states, the list and an answered join. Returns whether connected.
    def update
      return false unless @link.update
      subscribe_states if @sub.nil?
      @owner.table_watch if @on_watch
      watch_states
      find_tables
      collect_join if @table
      true
    end

    # After the game's own answers: an answered view, and a view asked
    # when it is due.
    def update_view(now = ::Machine.board_millis)
      return unless @table && @link.connected?
      collect_view
      ask_view if @token && @view_f.nil? && view_due?(now)
    end

    def close
      @link.close
    end

    # ------------------------------------------------------------ the state

    # :connecting before the first try, :up while the session is there,
    # :down after a failure or a loss (connecting again).
    def net
      return :up if @link.connected?
      @link.reason.empty? ? :connecting : :down
    end

    def reconnecting?
      @link.state == :reconnecting
    end

    def joining?
      !@join_f.nil?
    end

    def host_gone?
      @host_gone
    end

    # The table (or another) as a proxy for the game's own calls.
    def proxy(path = @table)
      ::Asterism[path, timeout: @call_timeout]
    end

    def host_of(path)
      path.split("/")[0]
    end

    # One of this game's tables ("<host>/games/<kind>...").
    def ours?(path)
      path.split("/")[2].to_s.start_with?(@kind)
    end

    # ------------------------------------------------------------ the table

    # Joins the table at path as name. nil when the call went out (or one is
    # on its way already), the error's text when it could not be sent.
    def join(path, name)
      return nil if @join_f || path.nil?
      @table = path
      @join_f = proxy(path).async.join(name)
      nil
    rescue ::Asterism::Error => e
      @join_f = nil
      @table = nil
      e.message.to_s
    end

    # Leaves the table (the host is told, not waited for).
    def leave
      begin
        proxy.async.leave(@token) if @token
      rescue ::Asterism::Error
      end
      forget
    end

    # Off the table without telling the host.
    def forget
      @table = nil
      @token = nil
      @view_f = nil
      @view_v = 0
      @host_gone = false
    end

    def ask_view
      return if @view_f || @token.nil?
      @view_at = ::Machine.board_millis
      @view_f = proxy.async.view(@token)
    rescue ::Asterism::Error
      @view_f = nil
    end

    # The view again now (after the game's own call is answered).
    def refresh_view
      @view_f = nil
      ask_view
    end

    # A newer version, or the view that says this member is still there.
    def view_due?(now)
      return now - @view_at >= @host_gone_view_ms if @host_gone
      @want_v > @view_v && now - @view_at >= @view_min_ms || now - @view_at >= @view_every_ms
    end

    # ------------------------------------------------------------ Link's calls

    def link_up(link)
      # Who is there (forgotten by Asterism.close, so given at each connect).
      ::Asterism.on_join { |n| node_joined(n) }
      ::Asterism.on_leave { |n| node_left(n) }
      @scan_at = 0
      @rescan_until = ::Machine.board_millis + @rescan_ms
      subscribe_states
      @owner.table_link_up(link) if @on_up
    end

    def link_down(link)
      @sub = nil
      @join_f = nil
      @view_f = nil
      @owner.table_link_down(link) if @on_down
    end

    # ------------------------------------------------------------ inside

    def subscribe_states
      @sub = ::Asterism.session.subscribe("asterism/*/#{@host_app}/*/state", depth: 8)
    rescue ::Asterism::Error => e
      ::Log.info("#{@tag}: subscribe: #{e.message}")
      @sub = nil
    end

    def watch_states
      return unless @sub
      @sub.each_pending do |key, payload, att|
        begin
          k = key.to_s
          next unless k.start_with?("asterism/") && k.end_with?("/state")
          p = k.byteslice(9, k.bytesize - 15)
          next unless ours?(p)
          pv = ::Asterism::Codec.unpack(payload)
          next unless pv.is_a?(Hash)
          add_table(p)
          if p == @table
            v = pv["v"].to_i
            @want_v = v if v > @want_v
          end
          @tables[p] = @owner.table_state(p, pv)
        rescue => e
          ::Log.info("#{@tag}: state: #{e.message}")
        end
      end
    end

    # The game's tables of the hosts on the network (Asterism.each reads the
    # liveliness the application already follows: nothing is sent).
    def find_tables
      now = ::Machine.board_millis
      every = now < @rescan_until ? @rescan_step_ms : @list_every_ms
      return if now - @scan_at < every
      @scan_at = now
      ::Asterism.each("*/#{@host_app}/*").each do |px|
        p = px.asterism_path
        add_table(p) if ours?(p)
      end
    rescue ::Asterism::Error => e
      ::Log.info("#{@tag}: scan: #{e.message}")
    end

    def add_table(p)
      return if @tables.key?(p)
      @tables[p] = nil
      @table_list = @tables.keys.sort
      ::Log.info("#{@tag}: table #{p} on the list")
      @owner.tables_changed(true) if @on_changed
    end

    # on_leave: a host's tables leave the list; at its table: the host gone.
    def node_left(n)
      gone = @table_list.select { |p| host_of(p) == n }
      unless gone.empty?
        gone.each { |p| @tables.delete(p) }
        @table_list = @tables.keys.sort
        ::Log.info("#{@tag}: #{n} left the network: #{gone.size} table(s) off the list")
        @owner.tables_changed(false) if @on_changed
      end
      if @table && host_of(@table) == n
        @host_gone = true
        @gone_at = ::Machine.board_millis
        @owner.table_host_gone if @on_gone
      end
    end

    # on_join: look for tables now; this table's host back: ask the view.
    def node_joined(n)
      @rescan_until = ::Machine.board_millis + @rescan_ms
      @scan_at = 0
      if @table && host_of(@table) == n && @host_gone
        @host_gone = false
        @view_f = nil
        @view_at = 0
        ::Log.info("#{@tag}: #{n} is back")
        @owner.table_host_back if @on_back
      end
    end

    def collect_join
      f = @join_f
      return unless f && f.done?
      @join_f = nil
      r = nil
      begin
        r = f.value
      rescue ::Asterism::Error
        @table = nil
        @owner.table_refused(nil)
        return
      end
      if r.is_a?(Hash) && r["token"]
        @token = r["token"].to_s
        @want_v = 0
        @view_v = 0
        @host_gone = false
        ::Log.info("#{@tag}: joined #{@table} (#{f.took_ms} ms)")
        @owner.table_joined(r)
        ask_view
      else
        @table = nil
        @owner.table_refused(r.is_a?(Hash) ? r["error"].to_s : r.to_s)
      end
    end

    def collect_view
      f = @view_f
      return unless f && f.done?
      @view_f = nil
      sent = @view_at
      v = nil
      begin
        v = f.value
      rescue ::Asterism::Error
        @owner.table_view_failed if @on_failed
        return
      end
      return unless v.is_a?(Hash)
      if v["error"]
        ::Log.info("#{@tag}: the host does not know us: back to the list")
        if @on_dropped
          @owner.table_dropped
        else
          leave
        end
        return
      end
      @view_v = v["v"].to_i
      # Answered a view sent after the host left: it is back.
      @host_gone = false if @host_gone && sent > @gone_at
      @owner.table_view(v)
    end
  end
end
