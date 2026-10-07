# Asterism::ROS: a minimal ROS 2 node over rmw_zenoh's wire format
# (doc/ruby_asterism/design.md ch. 5, R1). Topics only, std_msgs/String only.
#
#   s = Asterism::Zenoh::Session.open("tcp/192.168.10.2:7447")
#   node = Asterism::ROS::Node.new(s, "fmruby_talker")
#   pub = node.publisher("/chatter", Asterism::ROS::StdMsgs::String)
#   sub = node.subscription("/chatter_back", Asterism::ROS::StdMsgs::String)
#   loop do
#     s.poll
#     pub.publish("hello")
#     sub.each_pending { |msg, info| puts msg }   # info: Attachment or nil
#   end
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

    class Node
      attr_reader :name, :namespace, :domain, :zid, :key

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

      # Withdraws the node and its publishers and subscriptions. Idempotent.
      def close
        @entities.each { |e| e.close }
        @entities = []
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
