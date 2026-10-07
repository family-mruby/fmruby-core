# Asterism::ROS: a minimal ROS 2 node over rmw_zenoh's wire format
# (doc/ruby_asterism/design.md ch. 5; topics R1, services R2). Types:
# std_msgs/String and example_interfaces/srv/AddTwoInts.
#
#   s = Asterism::Zenoh::Session.open("tcp/192.168.10.2:7447")
#   node = Asterism::ROS::Node.new(s, "fmruby_talker")
#   pub = node.publisher("/chatter", Asterism::ROS::StdMsgs::String)
#   sub = node.subscription("/chatter_back", Asterism::ROS::StdMsgs::String)
#   add = Asterism::ROS::ExampleInterfaces::AddTwoInts
#   node.service("/add_two_ints_fmrb", add) { |req| { sum: req.a + req.b } }
#   cli = node.client("/add_two_ints", add)
#   loop do
#     node.poll                                   # session.poll + answer services
#     pub.publish("hello")
#     sub.each_pending { |msg, info| puts msg }   # info: Attachment or nil
#   end
#   cli.call(a: 2, b: 3).sum                      # waits (polling), or
#   c = cli.call_async(a: 2, b: 3); c.done?; c.value
#   node.close
#
# What goes on the wire (rmw_zenoh_cpp 0.2.x, ROS 2 Jazzy):
# - data key: <domain>/<topic without the outer slashes>/<DDS type name>/<type hash>
#   e.g. 0/chatter/std_msgs::msg::dds_::String_/RIHS01_df66...
# - payload: CDR (Asterism::CDR)
# - attachment (required by rmw_zenoh subscribers; a sample without one is
#   dropped): int64 sequence number, int64 source time (ns since the epoch),
#   both little endian, then the GID as a one-byte length (16) and 16 bytes.
# - liveliness tokens, which make the node and its topics appear in
#   `ros2 node list` / `ros2 topic list`:
#     @ros2_lv/<domain>/<zid>/<nid>/<nid>/NN/<enclave>/<namespace>/<node>
#     @ros2_lv/<domain>/<zid>/<nid>/<id>/MP|MS/<enclave>/<namespace>/<node>/
#       <topic>/<DDS type name>/<type hash>/<qos>
#   with "/" written as "%" in enclave, namespace and topic ("%" alone for "/").
# - services: the server is a complete queryable on
#   <domain>/<service>/<DDS service type>/<service type hash>, the client
#   gets that key with target ALL_COMPLETE and no consolidation. Request and
#   reply both carry the attachment above: the client's sequence number and
#   GID go out with the request, and the reply carries them back (with the
#   server's time), which is how the client pairs them. Tokens SS (server)
#   and SC (client), shaped like MP / MS.
#
# Nothing here depends on the host: it uses an Asterism::Zenoh::Session
# (put with attachment:, subscribe, liveliness, zid) and Asterism::CDR.
module Asterism
  module ROS
    LIVELINESS_ROOT = "@ros2_lv"
    # rmw_zenoh's QoS chunk for the default profile (reliable, volatile,
    # keep last 10): fields that equal the default are left empty.
    DEFAULT_QOS = "::,10:,:,:,,"
    GID_SIZE = 16

    # A name chunk of a liveliness token: "/" becomes "%", "" or "/" is "%".
    def self.mangle(name)
      s = name.to_s
      return "%" if s.empty? || s == "/"
      out = ""
      i = 0
      n = s.bytesize
      while i < n
        c = s.byteslice(i, 1)
        out << (c == "/" ? "%" : c)
        i += 1
      end
      out
    end

    # Drops one leading and one trailing "/".
    def self.strip_slashes(name)
      s = name.to_s
      s = s.byteslice(1, s.bytesize - 1) if s.bytesize > 0 && s.byteslice(0, 1) == "/"
      s = s.byteslice(0, s.bytesize - 1) if s.bytesize > 0 && s.byteslice(s.bytesize - 1, 1) == "/"
      s
    end

    # A full topic name: absolute names stay, relative ones go under the
    # namespace. "~" and substitutions are not supported.
    def self.resolve(topic, namespace)
      t = topic.to_s
      raise ArgumentError, "empty topic name" if t.empty?
      return t if t.byteslice(0, 1) == "/"
      ns = namespace.to_s
      ns == "/" || ns.empty? ? "/" + t : ns + "/" + t
    end

    # Nanoseconds since the epoch, or 0 when there is no clock.
    def self.now_ns
      if Object.const_defined?(:Time)
        t = ::Time.now
        t.to_i * 1_000_000_000 + t.usec * 1000
      else
        0
      end
    rescue
      0
    end

    # 16 bytes derived from a string (32-bit FNV-1a, four lanes with
    # different seeds; small enough not to overflow a 64-bit Integer).
    # rmw_zenoh hashes the entity's liveliness key the same way (with XXH3);
    # receivers only use the GID to tell publishers apart.
    def self.gid_for(key)
      out = ""
      lane = 0
      while lane < 4
        h = 0x811c9dc5 ^ (lane * 0x9e3779b9 & 0xffffffff)
        i = 0
        n = key.bytesize
        while i < n
          h = ((h ^ key.getbyte(i)) * 0x01000193) & 0xffffffff
          i += 1
        end
        out << ::Asterism::CDR.le_bytes(h, 4)
        lane += 1
      end
      out
    end

    # A service call got no answer in time (or nobody serves that name).
    class Timeout < ::StandardError; end

    # Milliseconds from an arbitrary origin (the board's clock when there is
    # one), and a short pause; the same helpers Asterism's calls use.
    def self.now_ms
      ::Asterism.now_ms
    end

    def self.pause(ms)
      ::Asterism.pause(ms)
    end

    # The attachment of a sample: sequence number, source time, publisher GID.
    class Attachment
      attr_reader :sequence, :stamp_ns, :gid

      def initialize(sequence, stamp_ns, gid)
        @sequence = sequence
        @stamp_ns = stamp_ns
        @gid = gid
      end

      def encode
        g = @gid.to_s
        ::Asterism::CDR.le_bytes(@sequence, 8) + ::Asterism::CDR.le_bytes(@stamp_ns, 8) +
          ::Asterism::CDR.le_bytes(g.bytesize, 1) + g
      end

      # nil when the bytes are not an attachment of this shape.
      def self.decode(bytes)
        return nil if bytes.nil? || bytes.bytesize < 17
        glen = bytes.getbyte(16)
        return nil if bytes.bytesize < 17 + glen
        new(le_int(bytes, 0), le_int(bytes, 8), bytes.byteslice(17, glen))
      end

      def self.le_int(bytes, at)
        top = bytes.getbyte(at + 7)
        v = top >= 0x80 ? top - 0x100 : top
        i = 6
        while i >= 0
          v = (v << 8) | bytes.getbyte(at + i)
          i -= 1
        end
        v
      end
    end

    module StdMsgs
      # std_msgs/msg/String: a single string field (data).
      module String
        ROS_NAME = "std_msgs/msg/String"
        TYPE_NAME = "std_msgs::msg::dds_::String_"
        # RIHS01 type hash of std_msgs/msg/String (the same in Jazzy and later).
        TYPE_HASH = "RIHS01_df668c740482bbd48fb39d76a70dfd4bd59db1288021743503259e948f6b1a18"

        def self.encode(data)
          ::Asterism::CDR::Writer.new.string(data.to_s).to_s
        end

        def self.decode(bytes)
          ::Asterism::CDR::Reader.new(bytes).string
        end
      end
    end

    module ExampleInterfaces
      # example_interfaces/srv/AddTwoInts: int64 a, int64 b -> int64 sum.
      module AddTwoInts
        ROS_NAME = "example_interfaces/srv/AddTwoInts"
        TYPE_NAME = "example_interfaces::srv::dds_::AddTwoInts_"
        # RIHS01 hash of the service type (not of Request / Response), as it
        # appears in rmw_zenoh's keys and tokens (Jazzy).
        TYPE_HASH = "RIHS01_e118de6bf5eeb66a2491b5bda11202e7b68f198d6f67922cf30364858239c81a"

        class Request
          attr_accessor :a, :b

          def initialize(a: 0, b: 0)
            @a = a
            @b = b
          end

          # msg: a Request, or a Hash with :a and :b.
          def self.encode(msg)
            m = msg.is_a?(Hash) ? new(a: msg[:a] || 0, b: msg[:b] || 0) : msg
            ::Asterism::CDR::Writer.new.int64(m.a.to_i).int64(m.b.to_i).to_s
          end

          def self.decode(bytes)
            r = ::Asterism::CDR::Reader.new(bytes)
            new(a: r.int64, b: r.int64)
          end

          def to_h
            { a: @a, b: @b }
          end
        end

        class Response
          attr_accessor :sum

          def initialize(sum: 0)
            @sum = sum
          end

          # msg: a Response, or a Hash with :sum.
          def self.encode(msg)
            m = msg.is_a?(Hash) ? new(sum: msg[:sum] || 0) : msg
            ::Asterism::CDR::Writer.new.int64(m.sum.to_i).to_s
          end

          def self.decode(bytes)
            new(sum: ::Asterism::CDR::Reader.new(bytes).int64)
          end

          def to_h
            { sum: @sum }
          end
        end
      end
    end

    class Node
      attr_reader :name, :namespace, :domain, :zid, :key, :session

      # session: an open Asterism::Zenoh::Session. Declares the node's
      # liveliness token at once.
      def initialize(session, name, namespace: "/", domain: 0, enclave: "/")
        @session = session
        @name = name.to_s
        raise ArgumentError, "bad node name #{@name.inspect}" if @name.empty? || @name.include?("/")
        @namespace = namespace.to_s
        @domain = domain.to_i
        @enclave = enclave.to_s
        @zid = session.zid
        @nid = 0
        @next_id = 1
        @entities = []
        @services = []
        @key = "#{LIVELINESS_ROOT}/#{@domain}/#{@zid}/#{@nid}/#{@nid}/NN/" \
               "#{::Asterism::ROS.mangle(@enclave)}/#{::Asterism::ROS.mangle(@namespace)}/#{@name}"
        @token = session.liveliness(@key)
      end

      def publisher(topic, type, qos: DEFAULT_QOS)
        e = ::Asterism::ROS::Publisher.new(self, @session, topic, type, qos)
        @entities << e
        e
      end

      def subscription(topic, type, qos: DEFAULT_QOS, depth: 16)
        e = ::Asterism::ROS::Subscription.new(self, @session, topic, type, qos, depth)
        @entities << e
        e
      end

      # Serves `service` (a name) with the block: it gets the request (an
      # object of type::Request) and returns the response (a type::Response
      # or a Hash of its fields). The block runs from node.poll (or
      # service.handle_pending), never behind the application's back.
      def service(service, type, qos: DEFAULT_QOS, depth: 8, &handler)
        raise ArgumentError, "service needs a block" unless handler
        e = ::Asterism::ROS::Service.new(self, @session, service, type, qos, depth, handler)
        @entities << e
        @services << e
        e
      end

      def client(service, type, qos: DEFAULT_QOS)
        e = ::Asterism::ROS::Client.new(self, @session, service, type, qos)
        @entities << e
        e
      end

      # node.call("/add_two_ints", AddTwoInts, a: 1, b: 2) -> response.
      # A client per service name is made on first use and kept.
      def call(service, type, request = nil, timeout_ms: ::Asterism::ROS::Client::DEFAULT_TIMEOUT_MS, **fields)
        @clients ||= {}
        c = @clients[service]
        if c.nil? || c.closed?
          c = client(service, type)
          @clients[service] = c
        end
        c.call(request, timeout_ms: timeout_ms, **fields)
      end

      # Polls the session (Asterism::Zenoh::Session#poll) and answers the
      # requests that came in for this node's services. Returns what
      # session.poll returns (false once the session is closed).
      def poll(steps = 8)
        ok = @session.poll(steps)
        @services.each { |sv| sv.handle_pending }
        ok
      end

      # Withdraws the node and its publishers, subscriptions, services and
      # clients. Idempotent.
      def close
        @entities.each { |e| e.close }
        @entities = []
        @services = []
        @clients = {}
        @token.close if @token
        @token = nil
        nil
      end

      def closed?
        @token.nil?
      end

      # For the entities: [data key, liveliness token key] of a topic.
      def entity_keys(kind, topic, type, qos)
        full = ::Asterism::ROS.resolve(topic, @namespace)
        data = "#{@domain}/#{::Asterism::ROS.strip_slashes(full)}/#{type::TYPE_NAME}/#{type::TYPE_HASH}"
        id = @next_id
        @next_id += 1
        token = "#{LIVELINESS_ROOT}/#{@domain}/#{@zid}/#{@nid}/#{id}/#{kind}/" \
                "#{::Asterism::ROS.mangle(@enclave)}/#{::Asterism::ROS.mangle(@namespace)}/#{@name}/" \
                "#{::Asterism::ROS.mangle(full)}/#{type::TYPE_NAME}/#{type::TYPE_HASH}/#{qos}"
        [data, token]
      end
    end

    class Publisher
      attr_reader :topic_key, :token_key, :gid, :sequence

      def initialize(node, session, topic, type, qos)
        @session = session
        @type = type
        @topic_key, @token_key = node.entity_keys("MP", topic, type, qos)
        @gid = ::Asterism::ROS.gid_for(@token_key)
        @sequence = 0
        @token = session.liveliness(@token_key)
      end

      # msg: what the type's encode takes (a String for StdMsgs::String).
      def publish(msg)
        raise ::Asterism::Zenoh::Error, "publisher closed" if @token.nil?
        @sequence += 1
        att = ::Asterism::ROS::Attachment.new(@sequence, ::Asterism::ROS.now_ns, @gid)
        @session.put(@topic_key, @type.encode(msg), attachment: att.encode)
        nil
      end

      def close
        @token.close if @token
        @token = nil
        nil
      end
    end

    class Subscription
      attr_reader :topic_key, :token_key, :errors

      def initialize(node, session, topic, type, qos, depth)
        @type = type
        @topic_key, @token_key = node.entity_keys("MS", topic, type, qos)
        @errors = 0
        @sub = session.subscribe(@topic_key, depth)
        @token = session.liveliness(@token_key)
      end

      # Yields each message received so far (decoded) and its Attachment (nil
      # when the sample had none). Samples that do not decode are counted in
      # errors and skipped. Returns the number yielded; without a block, an
      # Array of [msg, attachment].
      def each_pending
        out = []
        @sub.each_pending.each do |entry|
          msg = begin
            @type.decode(entry[1])
          rescue ::Asterism::CDR::DecodeError
            @errors += 1
            nil
          end
          next if msg.nil?
          out << [msg, ::Asterism::ROS::Attachment.decode(entry[2])]
        end
        return out unless block_given?
        out.each { |m| yield m[0], m[1] }
        out.size
      end

      def dropped
        @sub.dropped
      end

      def close
        @sub.close
        @token.close if @token
        @token = nil
        nil
      end
    end
  end
end

module Asterism
  module ROS
    # The server side of a service (node.service). A complete queryable on
    # the service key, plus the SS token.
    class Service
      attr_reader :service_key, :token_key, :handled, :errors

      def initialize(node, session, service, type, qos, depth, handler)
        @type = type
        @handler = handler
        @service_key, @token_key = node.entity_keys("SS", service, type, qos)
        @handled = 0
        @errors = 0
        @queryable = session.queryable(@service_key, depth, complete: true)
        @token = session.liveliness(@token_key)
      end

      # Answers the requests waiting now with the block given to
      # node.service. A request that cannot be decoded, or has no attachment
      # (rmw_zenoh clients always send one), is counted in errors and
      # finished without an answer. An exception from the block finishes
      # that request without an answer and is raised from here. Returns the
      # number answered.
      def handle_pending
        return 0 if @token.nil?
        n = 0
        # The Array form: no block called from C (stack depth, see README).
        qs = @queryable.each_pending
        begin
          qs.each do |q|
            n += 1 if answer(q)
            q.finish
          end
        ensure
          # Also the ones after a request whose block raised (finish is
          # idempotent; an unanswered query just ends for the client).
          qs.each { |q| q.finish }
        end
        n
      end

      def close
        @queryable.close
        @token.close if @token
        @token = nil
        nil
      end

      def closed?
        @token.nil?
      end

      private

      def answer(q)
        info = ::Asterism::ROS::Attachment.decode(q.attachment)
        req = nil
        begin
          req = @type::Request.decode(q.payload) if info
        rescue ::Asterism::CDR::DecodeError
          req = nil
        end
        if req.nil?
          @errors += 1
          return false
        end
        res = @handler.call(req)
        # The client's sequence number and GID go back with the reply.
        att = ::Asterism::ROS::Attachment.new(info.sequence, ::Asterism::ROS.now_ns, info.gid)
        q.reply(@service_key, @type::Response.encode(res), attachment: att.encode)
        @handled += 1
        true
      end
    end

    # A call on its way (client.call_async). done? never waits (node.poll
    # moves it on); value waits, polling the node's session.
    class Call
      attr_reader :sequence, :response, :took_ms

      def initialize(client, get, sequence, timeout_ms)
        @client = client
        @get = get
        @sequence = sequence
        @timeout_ms = timeout_ms
        @started = ::Asterism::ROS.now_ms
        @deadline = @started + timeout_ms
        @response = nil
        @finished = false
        @took_ms = nil
        @nobody = false
      end

      # True once the response came, the time ran out, or no server answered.
      def done?
        collect
        @finished
      end

      # The response (type::Response); waits until done. Raises
      # Asterism::ROS::Timeout when there was none.
      def value
        @client.wait_for(self) unless done?
        if @response.nil?
          raise ::Asterism::Zenoh::Error, "session is closed" if @client.session_closed?
          if @nobody
            raise ::Asterism::ROS::Timeout, "no answer from #{@client.service_name} (nobody serves it)"
          end
          raise ::Asterism::ROS::Timeout, "no answer from #{@client.service_name} within #{@timeout_ms} ms"
        end
        @response
      end

      def collect
        return if @finished
        g = @get
        # The Array form, as in Asterism::Future (no block called from C).
        g.each_reply.each do |r|
          next unless @response.nil?
          info = ::Asterism::ROS::Attachment.decode(r[2])
          # rmw_zenoh pairs the reply by the sequence number it carries back.
          next if info.nil? || info.sequence != @sequence
          begin
            @response = @client.decode_response(r[1])
          rescue ::Asterism::CDR::DecodeError
            @response = nil
          end
        end
        now = ::Asterism::ROS.now_ms
        if !@response.nil? || g.done? || now >= @deadline
          @nobody = @response.nil? && now < @deadline
          @finished = true
          @took_ms = now - @started
        end
      end
    end

    # The client side of a service (node.client). Sends each request as a
    # get on the service key, plus the SC token.
    class Client
      DEFAULT_TIMEOUT_MS = 2000
      # Pause between polls while a call waits (ms).
      WAIT_STEP_MS = 2

      attr_reader :service_key, :token_key, :gid, :sequence, :service_name

      def initialize(node, session, service, type, qos)
        @node = node
        @session = session
        @type = type
        @service_name = ::Asterism::ROS.resolve(service, node.namespace)
        @service_key, @token_key = node.entity_keys("SC", service, type, qos)
        @gid = ::Asterism::ROS.gid_for(@token_key)
        @sequence = 0
        @token = session.liveliness(@token_key)
      end

      # Sends the request and returns a Call at once. request: a
      # type::Request, a Hash, or the fields as keywords.
      def call_async(request = nil, timeout_ms: DEFAULT_TIMEOUT_MS, **fields)
        raise ::Asterism::Zenoh::Error, "client closed" if @token.nil?
        req = request.nil? ? fields : request
        payload = @type::Request.encode(req)
        @sequence += 1
        att = ::Asterism::ROS::Attachment.new(@sequence, ::Asterism::ROS.now_ns, @gid)
        g = @session.get(@service_key, timeout_ms, nil, payload, attachment: att.encode,
                         target: :all_complete, consolidation: :none)
        ::Asterism::ROS::Call.new(self, g, @sequence, timeout_ms)
      end

      # Sends the request and waits for the response (polling the node, so
      # this node's services keep answering meanwhile). Raises
      # Asterism::ROS::Timeout when no response came in time.
      def call(request = nil, timeout_ms: DEFAULT_TIMEOUT_MS, **fields)
        call_async(request, timeout_ms: timeout_ms, **fields).value
      end

      def wait_for(c)
        until c.done?
          break unless @node.poll
          break if c.done?
          ::Asterism::ROS.pause(WAIT_STEP_MS)
        end
      end

      def session_closed?
        @session.closed?
      end

      def decode_response(bytes)
        @type::Response.decode(bytes)
      end

      def close
        @token.close if @token
        @token = nil
        nil
      end

      def closed?
        @token.nil?
      end
    end
  end
end
