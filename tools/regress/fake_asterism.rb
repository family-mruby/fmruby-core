# frozen_string_literal: true
#
# fake_asterism.rb - the Asterism network an app sees, driven by the scene.
#
# The apps' own connection code (FmrbAsterism::Link, ::TableClient,
# fmrb-asterism.rb's settings) is loaded as it ships; what is faked is the
# Asterism core under it: connect / poll / close, the session's
# subscriptions and puts, Asterism.each / Asterism[path] proxies whose
# calls a game host answers (Regress::Net.host), on_join / on_leave.
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

  class FakeSub
    attr_reader :key

    def initialize(key)
      @key = key
      @queue = []
    end

    def push(key, payload) = @queue << [key, payload]

    def each_pending
      q = @queue
      @queue = []
      q.each { |k, p| yield k, p, nil }
      nil
    end

    def undeclare = nil
    alias close undeclare
  end

  class FakeSession
    attr_reader :subs, :puts, :queryables

    def initialize
      @subs = []
      @puts = []
      @queryables = []
    end

    def subscribe(key, depth: 1, **)
      s = FakeSub.new(key)
      @subs << s
      s
    end

    def put(key, value, **)
      @puts << [key, value]
      nil
    end

    def declare_publisher(key, **) = FakePublisher.new(self, key)
    def declare_queryable(key, **) = (@queryables << key; FakeSub.new(key))
    def get(*, **) = []
    def delete(*, **) = nil
    def liveliness(*, **) = nil
    def close = nil
    def zid = "0123456789abcdef"
    def method_missing(n, *, **) = Env.note_unstubbed("session.#{n}")
    def respond_to_missing?(*) = true
  end

  class FakePublisher
    def initialize(session, key)
      @session = session
      @key = key
    end

    def put(value, **) = @session.put(@key, value)
    def undeclare = nil
  end

  # The scene's handle on the network.
  module Net
    class << self
      attr_accessor :host, :connected, :reconnecting, :refuse, :session, :poll_plan, :on_join_block, :on_leave_block,
                    :exposed, :nodes, :profile_h, :calls, :caller, :ros, :ros_out

      def reset
        @host = FakeHost.new
        @connected = false
        @reconnecting = false
        @refuse = nil
        @session = FakeSession.new
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

      # A put on key, delivered to every subscription whose pattern matches.
      def put(key, value)
        @session.subs.each { |s| s.push(key, value) if match?(s.key, key) }
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

    def profile(**kw)
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
      net.session = Regress::FakeSession.new
      self
    end

    def close
      net.connected = false
      net.on_join_block = nil
      net.on_leave_block = nil
      nil
    end

    def poll
      r = net.poll_plan.shift
      r = true if r.nil?
      net.connected = false unless r
      r
    end

    def session
      raise Asterism::Error, "not connected" unless net.connected
      net.session
    end

    def connected? = net.connected
    def reconnecting? = net.reconnecting
    def reconnecting = net.reconnecting
    def lost_reason = "the connection was lost"
    def on_join(&b) = (net.on_join_block = b)
    def on_leave(&b) = (net.on_leave_block = b)
    # The objects on the network whose paths match (the host's tables).
    def each(pattern)
      return [] if net.host.away
      net.host.tables.keys.select { |p| net.match?(pattern, p) }.map { |p| Regress::FakeProxy.new(net.host, p) }
    end
    def [](path, timeout: nil) = Regress::FakeProxy.new(net.host, path)
    def call_async(path, name, *args) = net.host.call(path, name, args)
    def expose(name, obj = nil, **) = (net.exposed[name.to_s] = obj; nil)
    def respond_to(*) = nil
    def current_call = net.caller ? Struct.new(:from).new(net.caller) : nil
    def nodes = net.nodes
    def method_missing(n, *, **) = Regress::Env.note_unstubbed("Asterism.#{n}")
    def respond_to_missing?(*) = true
  end
end
