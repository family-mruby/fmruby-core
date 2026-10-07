# Asterism: Ruby objects on other machines, called like local ones.
#
#   Asterism.connect("tcp/192.168.10.2:7447", node: "fmruby-90bce8", app: "demo")
#   Asterism.expose("apu", apu, methods: [:play, :stop])
#   p = Asterism["linux/demo/apu"]     # <node>/<app>/<object>
#   p.play("cde")                      # waits for the answer (2 s by default)
#   Asterism.poll                      # call regularly (answers incoming calls)
#
# Keys (doc/ruby_asterism/design.md ch. 4):
#   asterism/<node>/<app>/<object>/call   get: payload [method, args, kwargs]
#                                         reply ["ok", value] or
#                                         ["error", class name, message]
#   asterism/<node>/<app>/<object>/meta   get: reply {"methods" => [[name, arity], ...]}
#   asterism/<node>                       liveliness: the node is up
#   asterism/<node>/<app>/<object>        liveliness: the object is exposed
#
# Everything is polled: nothing runs behind the application's back. A call
# that waits for its answer keeps polling meanwhile and answers the calls
# that come in, so two machines calling each other do not lock up.
module Asterism
  ROOT = "asterism"
  DEFAULT_TIMEOUT_MS = 2000
  # Waits that may be open inside each other (a call answered while waiting
  # that itself calls and waits, and so on).
  MAX_NESTING = 4
  # Pause between polls while a call waits (ms).
  WAIT_STEP_MS = 2

  class Error < StandardError; end

  # A value that MessagePack cannot carry (only nil, true, false, Integer,
  # Float, String, Symbol (sent as a String), Array and Hash can be sent).
  class EncodeError < Error; end

  # No answer within the time limit (the object is gone, or slow).
  class Timeout < Error; end

  # The connection is closed or was lost (Asterism::Zenoh::Error inside).
  class Disconnected < Error; end

  # The remote method raised, or could not be called (not exposed, wrong
  # number of arguments). remote_class is the class name on the other side.
  class RemoteError < Error
    attr_reader :remote_class, :remote_message

    def initialize(remote_class, remote_message)
      @remote_class = remote_class.to_s
      @remote_message = remote_message.to_s
      super("#{@remote_class}: #{@remote_message}")
    end
  end

  # ------------------------------------------------------------ values

  module Codec
    MAX_DEPTH = 16

    # The value as it will be sent (Symbols become Strings), or EncodeError.
    def self.check(v, depth = 0)
      raise ::Asterism::EncodeError, "value nested too deep (max #{MAX_DEPTH})" if depth > MAX_DEPTH
      if v.nil? || v == true || v == false || v.is_a?(Integer) || v.is_a?(Float) || v.is_a?(String)
        v
      elsif v.is_a?(Symbol)
        v.to_s
      elsif v.is_a?(Array)
        v.map { |e| check(e, depth + 1) }
      elsif v.is_a?(Hash)
        h = {}
        v.each { |k, x| h[check(k, depth + 1)] = check(x, depth + 1) }
        h
      else
        raise ::Asterism::EncodeError, "cannot send a #{v.class} (only nil, true, false, Integer, " \
                           "Float, String, Symbol, Array, Hash)"
      end
    end

    def self.pack(v)
      ::MessagePack.pack(check(v))
    end

    # nil when the bytes are not MessagePack.
    def self.unpack(bytes)
      return nil if bytes.nil? || bytes.empty?
      ::MessagePack.unpack(bytes)
    rescue
      nil
    end
  end

  # ------------------------------------------------------------ clock

  # Milliseconds and a short pause. PicoRuby's Machine when it is there,
  # otherwise Time and sleep.
  def self.now_ms
    if machine?
      ::Machine.board_millis
    else
      (Time.now.to_f * 1000).to_i
    end
  end

  def self.pause(ms)
    if machine?
      ::Machine.delay_ms(ms)
    else
      sleep(ms / 1000.0)
    end
  end

  def self.machine?
    @machine = (Object.const_defined?(:Machine) && ::Machine.respond_to?(:delay_ms)) if @machine.nil?
    @machine
  end

  # ------------------------------------------------------------ names

  # A node, app or object name: one key chunk, no wildcard or special
  # characters.
  def self.check_name(what, name)
    s = name.to_s
    bad = s.empty? || s.start_with?("@")
    ["/", "*", "$", "?", "#"].each { |c| bad = true if s.include?(c) }
    raise ArgumentError, "invalid #{what} name: #{name.inspect}" if bad
    s
  end

  # "node/app/object" -> [node, app, object]
  def self.split_path(path)
    parts = path.to_s.split("/")
    raise ArgumentError, "expected <node>/<app>/<object>, got #{path.inspect}" unless parts.size == 3
    check_name("node", parts[0])
    check_name("app", parts[1])
    check_name("object", parts[2])
    parts
  end

  # Glob over "/"-separated chunks: * is one chunk, ** any number of them.
  def self.glob?(pattern, path)
    glob_parts?(pattern.split("/"), 0, path.split("/"), 0)
  end

  def self.glob_parts?(pat, i, parts, j)
    while i < pat.size
      if pat[i] == "**"
        k = j
        while k <= parts.size
          return true if glob_parts?(pat, i + 1, parts, k)
          k += 1
        end
        return false
      end
      return false if j >= parts.size
      return false unless pat[i] == "*" || pat[i] == parts[j]
      i += 1
      j += 1
    end
    j == parts.size
  end

  # ------------------------------------------------------------ connection

  # Opens the connection. locator, mode: and listen: go to
  # Asterism::Zenoh::Session.open (client of a router by default; mode: :peer
  # with or without listen: for no router). node: is this machine's ID and
  # app: this application's name. Raises Disconnected when it cannot connect,
  # and Error when the same <node>/<app> is already on the network.
  def self.connect(locator, node:, app:, mode: nil, listen: nil)
    raise Error, "already connected (Asterism.close first)" if connected?
    close
    node = check_name("node", node)
    app = check_name("app", app)
    begin
      s = Asterism::Zenoh::Session.open(locator, mode: mode, listen: listen)
    rescue Asterism::Zenoh::Error => e
      raise Disconnected, e.message
    end
    begin
      if already_there?(s, node, app)
        s.close
        raise Error, "#{node}/#{app} is already on the network"
      end
      @node_token = s.liveliness("#{ROOT}/#{node}")
      @queryable = s.queryable("#{ROOT}/#{node}/#{app}/**", 32)
      @watch = s.liveliness_watch("#{ROOT}/**", 64)
    rescue Asterism::Zenoh::Error => e
      s.close
      raise Disconnected, e.message
    end
    @session = s
    @node = node
    @app = app
    @objects = {}  # name => [object, {method name => arity}, token]
    @alive = {}    # "node/app/object" => true (from liveliness)
    @nodes = {}    # node => true (from liveliness)
    @proxies = {}
    @depth = 0
    @lost = nil
    self
  end

  # Whether objects of node/app are alive already (a second copy of the
  # same application). Waits for the answer (a router answers at once).
  def self.already_there?(s, node, app)
    g = s.liveliness_get("#{ROOT}/#{node}/#{app}/**", 1000)
    t0 = now_ms
    found = false
    while true
      s.poll
      g.each_reply { |_k, _v| found = true }
      break if found || g.done? || now_ms - t0 > 1500
      pause(WAIT_STEP_MS)
    end
    found
  end

  def self.connected?
    !@session.nil? && !@session.closed?
  end

  def self.node_id
    @node
  end

  def self.app
    @app
  end

  # Why the connection went away (nil while connected or after close).
  def self.lost_reason
    @lost
  end

  # Closes the connection (and with it every exposed object). Idempotent.
  def self.close
    s = @session
    @session = nil
    @objects = {}
    @alive = {}
    @nodes = {}
    @proxies = {}
    s.close if s
    @node_token = nil
    @queryable = nil
    @watch = nil
    nil
  end

  def self.session!
    raise Disconnected, (@lost || "not connected") unless @session
    @session
  end

  def self.lost(reason)
    @lost = reason
    close
    @lost = reason
  end

  # ------------------------------------------------------------ exposing

  # Makes obj callable from other machines as <node>/<app>/<name>. Only the
  # listed methods can be called. methods: is an Array of names, or a Hash
  # name => number of arguments (checked before calling; -1 for any).
  def self.expose(name, obj, methods:)
    s = session!
    name = check_name("object", name)
    table = {}
    if methods.is_a?(Hash)
      methods.each { |m, n| table[m.to_s] = n.to_i }
    else
      methods.each { |m| table[m.to_s] = -1 }
    end
    raise ArgumentError, "no method to expose" if table.empty?
    table.each_key do |m|
      raise ArgumentError, "#{obj.class} has no public method #{m}" unless obj.respond_to?(m)
    end
    unexpose(name)
    token = s.liveliness("#{ROOT}/#{@node}/#{@app}/#{name}")
    @objects[name] = [obj, table, token]
    "#{@node}/#{@app}/#{name}"
  rescue Asterism::Zenoh::Error => e
    raise Disconnected, e.message
  end

  def self.unexpose(name)
    entry = @objects ? @objects.delete(name.to_s) : nil
    entry[2].close if entry
    !entry.nil?
  end

  def self.exposed
    @objects ? @objects.keys : []
  end

  # ------------------------------------------------------------ polling

  # Call from the application's update loop. Answers the calls that came
  # in and follows who is alive. false once the connection is closed.
  def self.poll
    return false unless @session
    pump
  end

  def self.pump
    s = @session
    return false unless s
    unless s.poll
      lost("the connection was lost")
      return false
    end
    answer_calls
    follow_liveliness
    !@session.nil?
  rescue Asterism::Zenoh::Error => e
    lost(e.message)
    false
  end

  # Polls until the block is true. Answers incoming calls meanwhile.
  def self.wait_until
    raise Error, "calls nested too deep (max #{MAX_NESTING})" if @depth.to_i >= MAX_NESTING
    @depth = @depth.to_i + 1
    begin
      until yield
        break unless pump
        break if yield
        pause(WAIT_STEP_MS)
      end
    ensure
      @depth -= 1
    end
  end

  def self.depth
    @depth.to_i
  end

  def self.follow_liveliness
    w = @watch
    return unless w
    w.each_pending do |key, alive|
      rest = key[ROOT.length + 1, key.length].to_s
      parts = rest.split("/")
      if parts.size == 1
        if alive
          @nodes[rest] = true
        else
          @nodes.delete(rest)
        end
      elsif parts.size == 3
        if alive
          @alive[rest] = true
        else
          @alive.delete(rest)
        end
      end
    end
  end

  # The node IDs alive now, this one first.
  def self.nodes
    return [] unless @session
    list = [@node]
    @nodes.each_key { |n| list << n unless list.include?(n) }
    @alive.each_key do |path|
      n = path.split("/")[0]
      list << n unless list.include?(n)
    end
    list
  end

  # The exposed objects alive now whose <node>/<app>/<object> matches the
  # pattern (* is one chunk, ** any number), this application's own
  # included. With a block, yields a proxy for each; else returns them.
  def self.each(pattern = "**")
    paths = []
    if @session
      @objects.each_key do |name|
        path = "#{@node}/#{@app}/#{name}"
        paths << path if glob?(pattern, path)
      end
      @alive.each_key { |path| paths << path if glob?(pattern, path) && !paths.include?(path) }
    end
    proxies = paths.map { |path| self[path] }
    return proxies unless block_given?
    proxies.each { |px| yield px }
    proxies.size
  end

  # A proxy for <node>/<app>/<object>. Nothing is sent until a method is
  # called on it.
  def self.[](path, timeout_ms = DEFAULT_TIMEOUT_MS)
    split_path(path)
    @proxies ||= {}
    key = "#{path}|#{timeout_ms}"
    @proxies[key] ||= Proxy.new(path.to_s, timeout_ms)
  end

  # ------------------------------------------------------------ calling

  # Starts a call; returns a Future. Raises EncodeError (before sending)
  # and Disconnected.
  def self.call_async(path, method, args, kwargs, timeout_ms)
    parts = split_path(path)
    payload = Codec.pack([method.to_s, args, kwargs])
    if parts[0] == @node && parts[1] == @app && @session
      # This application's own object: no network (a session does not see
      # its own queryable), same encoding and checks as a remote call.
      return Future.answered(path, method, dispatch(parts[2], payload))
    end
    begin
      g = session!.get("#{ROOT}/#{path}/call", timeout_ms, nil, payload)
    rescue Asterism::Zenoh::Error => e
      lost(e.message)
      raise Disconnected, e.message
    end
    Future.new(path, method, g, now_ms + timeout_ms, timeout_ms)
  end

  def self.call(path, method, args, kwargs, timeout_ms)
    call_async(path, method, args, kwargs, timeout_ms).value
  end

  # The meta reply of path: {"methods" => [[name, arity], ...]}.
  def self.meta(path, timeout_ms = DEFAULT_TIMEOUT_MS)
    parts = split_path(path)
    if parts[0] == @node && parts[1] == @app && @session
      m = meta_of(parts[2])
      raise RemoteError.new("NameError", "no object #{parts[2]} exposed by #{@node}/#{@app}") unless m
      return m
    end
    begin
      g = session!.get("#{ROOT}/#{path}/meta", timeout_ms)
    rescue Asterism::Zenoh::Error => e
      lost(e.message)
      raise Disconnected, e.message
    end
    f = Future.new(path, "meta", g, now_ms + timeout_ms, timeout_ms)
    f.raw_value
  end

  # ------------------------------------------------------------ answering

  def self.answer_calls
    qa = @queryable
    return unless qa
    # Without a block, so that a handler that waits (and so polls) does not
    # re-enter the queryable's iteration.
    qa.each_pending.each do |q|
      begin
        answer(q)
      ensure
        begin
          q.finish
        rescue Asterism::Zenoh::Error
          # the session went away meanwhile
        end
      end
    end
  end

  # asterism/<node>/<app>/<object>/<call|meta>; the node and app may come
  # as wildcards (the query matched this application's queryable).
  def self.answer(q)
    parts = q.key.split("/")
    return unless parts.size == 5
    obj = parts[3]
    kind = parts[4]
    prefix = "#{ROOT}/#{@node}/#{@app}/"
    if kind == "meta"
      names = obj == "*" ? @objects.keys : [obj]
      names.each do |n|
        m = meta_of(n)
        q.reply("#{prefix}#{n}/meta", Codec.pack(m)) if m
      end
    elsif kind == "call" && obj != "*"
      q.reply("#{prefix}#{obj}/call", dispatch(obj, q.payload))
    end
  end

  def self.meta_of(name)
    entry = @objects[name]
    return nil unless entry
    list = []
    entry[1].each { |m, n| list << [m, n] }
    { "methods" => list }
  end

  # Runs a call on an exposed object; returns the packed reply.
  def self.dispatch(obj_name, payload)
    req = Codec.unpack(payload)
    unless req.is_a?(Array) && req.size == 3 && req[0].is_a?(String) && req[1].is_a?(Array) && req[2].is_a?(Hash)
      return error_reply("ArgumentError", "malformed call (expected [method, args, kwargs])")
    end
    entry = @objects[obj_name]
    return error_reply("NameError", "no object #{obj_name} exposed by #{@node}/#{@app}") unless entry
    name = req[0]
    args = req[1]
    kw = req[2]
    arity = entry[1][name]
    if arity.nil?
      return error_reply("NoMethodError", "undefined method '#{name}' for #{@node}/#{@app}/#{obj_name} (not exposed)")
    end
    if arity >= 0 && args.size != arity
      return error_reply("ArgumentError", "wrong number of arguments (given #{args.size}, expected #{arity})")
    end
    begin
      value = invoke(entry[0], name, args, kw)
      Codec.pack(["ok", value])
    rescue EncodeError => e
      error_reply("Asterism::EncodeError", "the return value: #{e.message}")
    rescue => e
      error_reply(e.class.to_s, e.message)
    end
  end

  def self.invoke(obj, name, args, kw)
    if kw.empty?
      obj.public_send(name, *args)
    else
      opts = {}
      kw.each { |k, v| opts[k.to_s.to_sym] = v }
      obj.public_send(name, *args, **opts)
    end
  end

  def self.error_reply(klass, message)
    ::MessagePack.pack(["error", klass.to_s, message.to_s])
  end
end
