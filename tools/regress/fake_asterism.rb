# frozen_string_literal: true
#
# fake_asterism.rb - the Asterism network an app sees, driven by the scene.
#
# The apps' own connection code (FmrbAsterism::Link, ::TableClient,
# fmrb-asterism.rb's settings) is loaded as it ships; what is faked is the
# Asterism core under it: Asterism.connect and the connection it returns
# (poll / close, subscriptions whose blocks get the scene's puts in the
# poll, topics whose puts are kept in Net.published, conn.each /
# conn[path] proxies whose calls a game host answers (Regress::Net.host),
# on_join / on_leave), and Asterism.profile_defaults.
#
# A scene drives it through Regress::Net:
#   net.host.tables["games-host-a1b2c3/games/othello1"] = {"st" => "wait", ...}   # put as its state
#   net.host.answer(:view) { |args| {...} }   # what view(token) answers
#   net.put("asterism/x/y/z", value)          # a put on a key the app subscribed
#   net.down! / net.up!                       # the router goes away / comes back
module Regress
  class FakeFuture
    attr_accessor :ready, :result, :fail

    def initialize(result = nil, ready: true, fail: false)
      @result = result
      @ready = ready
      @fail = fail
    end

    def done? = @ready
    def took_ms = 5

    def value
      raise ::Asterism::Error, "timeout" if @fail
      @result
    end
    alias wait value
  end

  # A game host (asterism-games): its tables, and what each call answers.
  class FakeHost
    attr_accessor :away, :hold, :node
    attr_reader :calls, :tables

    def initialize
      @node = "games-host-a1b2c3"
      @tables = {}
      @answers = {}
      @calls = []
      @away = false
      @hold = false
      @held = []
    end

    # What a call answers: a value, or a block given the call's args.
    def answer(name, value = nil, &blk)
      @answers[name.to_sym] = blk || ->(_args) { value }
    end

    def call(path, name, args)
      @calls << [path, name, *args]
      f = FakeFuture.new(nil, ready: !@hold)
      if @away
        f.fail = true
      else
        a = @answers[name.to_sym]
        f.result = a ? a.call(args) : default_answer(name, args)
      end
      @held << f if @hold
      f
    end

    def release
      @held.each { |f| f.ready = true }
      @held = []
    end

    def default_answer(name, args)
      case name.to_sym
      when :join then { "token" => "tok-a1b2", "seat" => -1 }
      when :leave then true
      else "ok"
      end
    end
  end

  class FakeAsync
    def initialize(host, path)
      @host = host
      @path = path
    end

    def method_missing(name, *args, **kw)
      @host.call(@path, name, kw.empty? ? args : args + [kw])
    end

    def respond_to_missing?(*) = true
  end

  class FakeProxy
    attr_reader :asterism_path

    def initialize(host, path)
      @host = host
      @asterism_path = path
    end

    def async = FakeAsync.new(@host, @asterism_path)

    # A synchronous call answers at once (or raises when the host is away).
    def method_missing(name, *args, **kw)
      @host.call(@asterism_path, name, kw.empty? ? args : args + [kw]).value
    end

    def respond_to_missing?(*) = true
  end

  # What a subscription's block is given with a value (Asterism::SampleInfo):
  # the scene's values come in order, nothing lost.
  class FakeInfo
    attr_reader :key, :path, :node, :app, :name, :seq, :run_id, :time, :encoding, :timestamp, :lost, :shared, :from

    def initialize(key)
      @key = key
      @path = key.delete_prefix("asterism/")
      parts = @path.split("/")
      @node = parts[0]
      @app = parts[1]
      @name = parts[2..].to_a.join("/")
      @from = "#{@node}/#{@app}"
      @seq = nil
      @run_id = nil
      @time = nil
      @encoding = "msgpack"
      @timestamp = nil
      @lost = 0
      @shared = nil
    end

    def restarted? = false
  end

  # A subscription (Asterism::Subscription): the values put on keys that
  # match its pattern wait here until the connection's poll gives them to
  # the block, as the real one does.
  class FakeSub
    attr_reader :pattern

    def initialize(pattern, blk)
      @pattern = pattern
      @blk = blk
      @queue = []
      @closed = false
    end

    def push(key, value) = @queue << [key, value]

    def deliver
      q = @queue
      @queue = []
      q.each { |k, v| @blk.call(v, FakeInfo.new(k)) unless @closed }
      nil
    end

    def received = 0
    def malformed = 0
    def dropped = 0
    def closed? = @closed
    def close = (@closed = true; nil)
  end

  # The connection's Zenoh session (conn.session): what the ROS 2 node
  # is built on.
  class FakeSession
    def close = nil
    def zid = "0123456789abcdef"
    def method_missing(n, *, **) = Env.note_unstubbed("session.#{n}")
    def respond_to_missing?(*) = true
  end

  # A topic (Asterism::Topic): its values are kept in Net.published.
  class FakeTopic
    attr_reader :name, :key

    def initialize(name, key)
      @name = name
      @key = key
      @seq = 0
    end

    def put(value)
      Net.published << [@key, value]
      @seq += 1
      nil
    end

    def seq = @seq
    def close = nil
  end

  # A connection (Asterism::Connection), the one the app opened.
  class FakeConn
    attr_reader :node_id, :app, :subs, :topics

    def initialize(node, app)
      @node_id = node
      @app = app
      @subs = []
      @topics = {}
      @open = true
      @session = FakeSession.new
    end

    def net = Net
    def path = "#{@node_id}/#{@app}"
    def connected? = @open && net.connected
    def reconnecting? = net.reconnecting
    def lost_reason = "the connection was lost"

    def poll
      r = net.poll_plan.shift
      r = true if r.nil?
      unless r
        net.connected = false
        return false
      end
      @subs.dup.each(&:deliver)
      true
    end

    def close
      @open = false
      return nil unless net.conn.equal?(self)
      net.connected = false
      net.on_join_block = nil
      net.on_leave_block = nil
      nil
    end

    def session
      raise ::Asterism::Disconnected, "not connected" unless connected?
      @session
    end

    def subscribe(pattern, depth: nil, &blk)
      s = FakeSub.new(pattern, blk)
      @subs << s
      s
    end

    def publish(name, type: nil, unit: nil, desc: nil, ts: false, encoding: nil, shared: nil)
      @topics[name] = FakeTopic.new(name, "asterism/#{@node_id}/#{@app}/#{name}")
    end

    def define_type(name, fields, desc: nil) = (net.types[name] = fields; nil)
    def status(h = nil, **) = nil
    def on_join(&b) = (net.on_join_block = b)
    def on_leave(&b) = (net.on_leave_block = b)
    # The objects on the network whose paths match (the host's tables).
    def each(pattern = "**", &blk)
      list = net.host.away ? [] : net.host.tables.keys.select { |p| net.match?(pattern, p) }.map { |p| FakeProxy.new(net.host, p) }
      blk ? list.each(&blk) : list
    end
    def [](path, timeout: nil, timeout_ms: nil) = FakeProxy.new(net.host, path)
    def call_async(path, name, args, _kw = nil, _ms = nil) = net.host.call(path, name, args.to_a)
    def expose(name, obj = nil, **) = (net.exposed[name.to_s] = obj; nil)
    def current_call = net.caller ? Struct.new(:from).new(net.caller) : nil
    def nodes = net.nodes
    def profile(**kw) = ::Asterism.profile_defaults(**kw)
    def method_missing(n, *, **) = Env.note_unstubbed("conn.#{n}")
    def respond_to_missing?(*) = true
  end

  # The scene's handle on the network.
  module Net
    class << self
      attr_accessor :host, :connected, :reconnecting, :refuse, :conn, :poll_plan, :on_join_block, :on_leave_block,
                    :exposed, :nodes, :profile_h, :calls, :caller, :ros, :ros_out, :published, :types

      def reset
        @host = FakeHost.new
        @connected = false
        @reconnecting = false
        @refuse = nil
        @conn = nil
        @poll_plan = []
        @on_join_block = nil
        @on_leave_block = nil
        @exposed = {}
        @nodes = []
        @calls = []
        @profile_h = {}
        @caller = nil
        @ros = {}
        @ros_out = []
        @published = []
        @types = {}
      end

      # Calls a method of an object the app exposed, as node `from` would.
      def call_exposed(name, meth, *args, from: "fmruby-d4e5f6")
        @caller = from
        obj = @exposed.fetch(name.to_s)
        # Exposed with caller: :from, a method takes the caller as from:.
        if obj.method(meth).parameters.any? { |_t, n| n == :from }
          obj.public_send(meth, *args, from: from)
        else
          obj.public_send(meth, *args)
        end
      ensure
        @caller = nil
      end

      # Puts every table's state (as the host does after a change).
      def put_states
        @host.tables.each { |p, st| put("asterism/#{p}/state", st) }
      end

      # A put on key, given to every subscription whose pattern matches at
      # the connection's next poll.
      def put(key, value)
        return unless @conn
        @conn.subs.each { |s| s.push(key, value) if match?("asterism/#{s.pattern}", key) }
      end

      def match?(pattern, key)
        pp = pattern.split("/")
        kk = key.split("/")
        i = 0
        while i < pp.size
          return true if pp[i] == "**"
          return false if i >= kk.size
          return false unless pp[i] == "*" || pp[i] == kk[i]
          i += 1
        end
        i == kk.size
      end

      def down! = (@poll_plan = [false])
      def host_leaves! = (@host.away = true; @on_leave_block&.call(@host.node))
      def host_back! = (@host.away = false; @on_join_block&.call(@host.node))
    end
    reset
  end
end

module Asterism
  class Error < StandardError; end
  class RemoteError < Error
    def remote_kind = nil
    def remote_message = message
  end
  class Disconnected < Error; end

  module CDR
    PACK = false
  end

  # ROS 2 over the session: a node whose subscriptions the scene feeds
  # (Regress::Net.ros[topic].call(msg)) and whose publishes are kept.
  module ROS
    def self.now_ns = Regress::Env.ms * 1_000_000

    class Node
      def initialize(session, name)
        @session = session
        @name = name
      end

      def publisher(topic, _type, **) = Publisher.new(topic)
      def poll = true
      def close = nil

      def subscribe(topic, _type, **, &blk)
        Regress::Net.ros[topic] = blk
        nil
      end
    end

    class Publisher
      def initialize(topic) = @topic = topic
      def publish(msg) = (Regress::Net.ros_out << [@topic, msg]; nil)
      alias << publish
    end
  end
  class TimeoutError < Error; end
  class CallError < Error; end

  module Codec
    def self.pack(v) = v
    def self.unpack(v) = v
  end

  module Profile
    NAME_MAX = 32
  end

  class Credentials
    attr_reader :router, :name

    def self.load(_path) = raise(Asterism::Error, "no credentials in the regress stand-in")
  end

  class << self
    def net = Regress::Net

    def profile_defaults(**kw)
      return net.profile_h.dup if kw.empty?
      kw.each do |k, v|
        raise ArgumentError, "name too long" if k == :name && v.to_s.bytesize > Profile::NAME_MAX
        v.nil? ? net.profile_h.delete(k.to_s) : net.profile_h[k.to_s] = v.to_s
      end
      net.profile_h.dup
    end

    def connect(locator = nil, node: nil, app: nil, mode: nil, listen: nil, credentials: nil, **)
      net.calls << [:connect, app]
      raise Asterism::Error, net.refuse if net.refuse
      net.connected = true
      net.conn = Regress::FakeConn.new(node, app)
    end

    def now_ms = Regress::Env.ms
    def method_missing(n, *, **) = Regress::Env.note_unstubbed("Asterism.#{n}")
    def respond_to_missing?(*) = true
  end
end
