#!/usr/bin/env ruby
# frozen_string_literal: true
#
# asterism_msggen.rb: ROS 2 .msg / .srv -> pure Ruby message types for
# Asterism::ROS (doc/ruby_asterism, R3). CRuby with the standard library only.
#
#   ruby asterism_msggen.rb -I /opt/ros/jazzy/share -o out geometry_msgs/msg/Twist
#   ruby asterism_msggen.rb -I share -I my_ws/src -o out my_pkg/msg/Foo.msg
#   ruby asterism_msggen.rb -I share --hash std_msgs/msg/String
#   ruby asterism_msggen.rb -I share --check-json share --all
#
# Each generated file defines one type (a message, or a service with its
# Request and Response) under Asterism::ROS::<PackageInCamelCase>, with its
# fields and defaults, ROS_NAME, TYPE_NAME (the DDS name rmw_zenoh uses),
# TYPE_HASH (RIHS01, computed here) and the CDR conversion in both directions
# on top of Asterism::CDR. A file loads the types it is made of through
# Asterism::ROS.require_type first. The output layout mirrors the ROS names:
# <out>/<pkg>/msg/<Name>.rb and <out>/<pkg>/srv/<Name>.rb.
#
# The type hash follows the type description rules of ROS 2 Jazzy
# (rosidl_generator_type_description, REP 2011): the type and every type it
# references, as {type_name, fields: [{name, type: {type_id, capacity,
# string_capacity, nested_type_name}}]}, without default values, referenced
# types sorted by name, dumped like Python's json.dumps with separators
# (", ", ": ") and ensure_ascii, then SHA-256.
require "digest"
require "fileutils"
require "json"
require "optparse"

module AsterismMsgGen
  ROS_DISTRO = "jazzy"

  # .msg primitive -> [type description id, CDR kind used by Asterism::CDR]
  PRIMITIVES = {
    "bool" => [15, :bool],
    "byte" => [16, :uint8],
    "char" => [3, :uint8],     # .msg char is uint8 in the IDL
    "int8" => [2, :int8],
    "uint8" => [3, :uint8],
    "int16" => [4, :int16],
    "uint16" => [5, :uint16],
    "int32" => [6, :int32],
    "uint32" => [7, :uint32],
    "int64" => [8, :int64],
    "uint64" => [9, :uint64],
    "float32" => [10, :float32],
    "float64" => [11, :float64],
    "string" => [17, :string],
    "wstring" => [18, :wstring]
  }.freeze
  NESTED_ID = 1
  BOUNDED_STRING_ID = 21
  BOUNDED_WSTRING_ID = 22
  ARRAY_OFFSET = { fixed: 48, bounded: 96, unbounded: 144 }.freeze
  # Kinds that are Strings of bytes when they come as an array or sequence.
  BYTE_KINDS = %w[byte char uint8].freeze
  FLOAT_KINDS = %w[float32 float64].freeze
  INT_KINDS = %w[int8 uint8 int16 uint16 int32 uint32 int64 uint64 byte char].freeze

  RUBY_RESERVED = %w[
    BEGIN END alias and begin break case class def defined? do else elsif end
    ensure false for if in module next nil not or redo rescue retry return
    self super then true undef unless until when while yield __FILE__ __LINE__
    __ENCODING__ hash class object_id send method initialize
  ].freeze

  class Error < StandardError; end

  # One field: base is the .msg type word ("float64", "std_msgs/msg/Header"),
  # nested is the full name of a message type (nil for primitives).
  Field = Struct.new(:name, :base, :nested, :array, :size, :string_max, :default, keyword_init: true) do
    def primitive?
      nested.nil?
    end

    def string?
      base == "string" || base == "wstring"
    end
  end

  Constant = Struct.new(:name, :base, :value, keyword_init: true)

  # A message (also a service's Request / Response / Event).
  Message = Struct.new(:full_name, :pkg, :name, :fields, :constants, :source, :service, keyword_init: true)

  Service = Struct.new(:full_name, :pkg, :name, :request, :response, :event, :source, keyword_init: true)

  # "pkg/msg/Name" or "pkg/srv/Name" -> [pkg, kind, name]
  def self.split_name(full)
    parts = full.split("/")
    raise Error, "bad type name #{full.inspect} (want pkg/msg/Name)" unless parts.size == 3
    parts
  end

  def self.camel(pkg)
    pkg.split("_").map { |w| w.empty? ? "" : w[0].upcase + w[1..] }.join
  end

  # ---- parsing -------------------------------------------------------------

  class Parser
    TYPE_RE = %r{\A([A-Za-z][A-Za-z0-9_]*(?:/[A-Za-z][A-Za-z0-9_]*)?)(?:<=(\d+))?(?:\[(<=)?(\d*)\])?\z}
    FIELD_NAME_RE = /\A[a-z][a-z0-9_]*\z/
    CONST_NAME_RE = /\A[A-Z][A-Z0-9_]*\z/

    def initialize(pkg, source)
      @pkg = pkg
      @source = source
    end

    # Lines of a .msg body -> [fields, constants]
    def parse_lines(lines)
      fields = []
      constants = []
      lines.each_with_index do |raw, i|
        line = strip_comment(raw).strip
        next if line.empty?
        begin
          parse_line(line, fields, constants)
        rescue Error => e
          raise Error, "#{@source}:#{i + 1}: #{e.message}"
        end
      end
      [fields, constants]
    end

    def strip_comment(line)
      quote = nil
      line.each_char.with_index do |c, i|
        if quote
          quote = nil if c == quote
        elsif c == '"' || c == "'"
          quote = c
        elsif c == "#"
          return line[0, i]
        end
      end
      line
    end

    def parse_line(line, fields, constants)
      type_word, rest = line.split(/\s+/, 2)
      raise Error, "no name after #{type_word}" if rest.nil? || rest.empty?
      if (m = rest.match(/\A([A-Za-z][A-Za-z0-9_]*)\s*=\s*(.*)\z/)) && !rest.match?(/\A[a-z][a-z0-9_]*\s+\S/)
        constants << parse_constant(type_word, m[1], m[2].strip)
        return
      end
      name, default = rest.split(/\s+/, 2)
      raise Error, "bad field name #{name.inspect}" unless FIELD_NAME_RE.match?(name)
      raise Error, "field name #{name.inspect} clashes with Ruby" if RUBY_RESERVED.include?(name)
      f = parse_type(type_word)
      f.name = name
      f.default = default.nil? || default.empty? ? nil : parse_default(f, default.strip)
      fields << f
    end

    def parse_type(word)
      m = TYPE_RE.match(word)
      raise Error, "bad type #{word.inspect}" unless m
      base = m[1]
      f = Field.new
      if m[2]
        raise Error, "only strings can be bounded: #{word}" unless %w[string wstring].include?(base)
        f.string_max = m[2].to_i
      end
      if word.include?("[")
        if m[3]
          f.array = :bounded
          f.size = m[4].to_i
          raise Error, "bounded sequence without a bound: #{word}" if m[4].empty?
        elsif m[4].empty?
          f.array = :unbounded
        else
          f.array = :fixed
          f.size = m[4].to_i
        end
      end
      if PRIMITIVES.key?(base)
        f.base = base
      else
        f.nested = resolve(base)
        f.base = f.nested
      end
      f
    end

    # A nested type word of a .msg -> its full name.
    def resolve(base)
      if base.include?("/")
        pkg, name = base.split("/")
        "#{pkg}/msg/#{name}"
      elsif base == "Header"
        "std_msgs/msg/Header"
      else
        "#{@pkg}/msg/#{base}"
      end
    end

    def parse_constant(type_word, name, value)
      raise Error, "bad constant name #{name.inspect}" unless CONST_NAME_RE.match?(name)
      raise Error, "constants must be primitive: #{type_word}" unless PRIMITIVES.key?(type_word)
      v = type_word == "string" || type_word == "wstring" ? value : scalar(type_word, value)
      Constant.new(name: name, base: type_word, value: v)
    end

    def parse_default(f, text)
      raise Error, "nested fields have no default" unless f.primitive?
      if f.array
        raise Error, "array default must be [..]: #{text}" unless text.start_with?("[") && text.end_with?("]")
        items = split_list(text[1..-2])
        items.map { |s| f.string? ? unquote(s) : scalar(f.base, s) }
      elsif f.string?
        unquote(text)
      else
        scalar(f.base, text)
      end
    end

    def split_list(body)
      out = []
      cur = +""
      quote = nil
      body.each_char do |c|
        if quote
          cur << c
          quote = nil if c == quote
        elsif c == '"' || c == "'"
          quote = c
          cur << c
        elsif c == ","
          out << cur.strip
          cur = +""
        else
          cur << c
        end
      end
      out << cur.strip unless cur.strip.empty?
      out
    end

    def unquote(s)
      if s.size >= 2 && (s[0] == '"' || s[0] == "'") && s[-1] == s[0]
        s[1..-2].gsub("\\#{s[0]}", s[0])
      else
        s
      end
    end

    def scalar(base, s)
      case base
      when "bool"
        return true if %w[true True 1].include?(s)
        return false if %w[false False 0].include?(s)
        raise Error, "bad bool #{s.inspect}"
      when *FLOAT_KINDS
        Float(s)
      else
        Integer(s)
      end
    rescue ArgumentError
      raise Error, "bad #{base} value #{s.inspect}"
    end
  end

  # ---- the set of known types --------------------------------------------

  class Registry
    attr_reader :include_dirs

    def initialize(include_dirs)
      @include_dirs = include_dirs
      @messages = {}
      @services = {}
    end

    # The .msg / .srv files read so far.
    def sources
      (@messages.values + @services.values).map(&:source).uniq.sort
    end

    # A message by full name ("pkg/msg/Name", or a service part such as
    # "pkg/srv/Name_Request"), parsed on first use.
    def message(full)
      return @messages[full] if @messages.key?(full)
      pkg, kind, name = AsterismMsgGen.split_name(full)
      if kind == "srv"
        base = name.sub(/_(Request|Response|Event)\z/, "")
        raise Error, "unknown service part #{full}" if base == name
        service("#{pkg}/srv/#{base}")
        return @messages.fetch(full)
      end
      raise Error, "not a message: #{full}" unless kind == "msg"
      path = find("#{pkg}/msg/#{name}.msg")
      load_msg_file(path, pkg, name)
    end

    def service(full)
      return @services[full] if @services.key?(full)
      pkg, kind, name = AsterismMsgGen.split_name(full)
      raise Error, "not a service: #{full}" unless kind == "srv"
      path = find("#{pkg}/srv/#{name}.srv")
      load_srv_file(path, pkg, name)
    end

    def type(full)
      _, kind, = AsterismMsgGen.split_name(full)
      kind == "srv" && !full.match?(/_(Request|Response|Event)\z/) ? service(full) : message(full)
    end

    def find(rel)
      @include_dirs.each do |d|
        p = File.join(d, rel)
        return p if File.file?(p)
      end
      raise Error, "#{rel} not found under #{@include_dirs.join(', ')}"
    end

    # A .msg / .srv path -> full name. The package is the directory above
    # msg/ or srv/ (the ROS layout).
    def name_of_file(path)
      dir = File.basename(File.dirname(path))
      pkg = File.basename(File.dirname(File.dirname(path)))
      ext = File.extname(path)
      kind = ext == ".srv" ? "srv" : "msg"
      raise Error, "#{path}: expected <pkg>/#{kind}/<Name>#{ext}" unless dir == kind
      full = "#{pkg}/#{kind}/#{File.basename(path, ext)}"
      if kind == "srv"
        @services[full] ||= load_srv_file(path, pkg, File.basename(path, ext))
      else
        @messages[full] ||= load_msg_file(path, pkg, File.basename(path, ext))
      end
      full
    end

    def load_msg_file(path, pkg, name)
      full = "#{pkg}/msg/#{name}"
      fields, constants = Parser.new(pkg, path).parse_lines(File.read(path).lines)
      @messages[full] = Message.new(full_name: full, pkg: pkg, name: name, fields: fields,
                                    constants: constants, source: path)
    end

    def load_srv_file(path, pkg, name)
      full = "#{pkg}/srv/#{name}"
      lines = File.read(path).lines
      sep = lines.index { |l| l.strip == "---" }
      raise Error, "#{path}: no --- line" unless sep
      parser = Parser.new(pkg, path)
      parts = {}
      [["Request", lines[0...sep]], ["Response", lines[(sep + 1)..]]].each do |suffix, body|
        fields, constants = parser.parse_lines(body)
        mfull = "#{full}_#{suffix}"
        parts[suffix] = @messages[mfull] = Message.new(full_name: mfull, pkg: pkg, name: "#{name}_#{suffix}",
                                                     fields: fields, constants: constants, source: path,
                                                     service: full)
      end
      # The event message every service has (REP 2011 / service introspection).
      event_full = "#{full}_Event"
      event_fields = [
        Field.new(name: "info", base: "service_msgs/msg/ServiceEventInfo", nested: "service_msgs/msg/ServiceEventInfo"),
        Field.new(name: "request", base: parts["Request"].full_name, nested: parts["Request"].full_name, array: :bounded, size: 1),
        Field.new(name: "response", base: parts["Response"].full_name, nested: parts["Response"].full_name, array: :bounded, size: 1)
      ]
      @messages[event_full] = Message.new(full_name: event_full, pkg: pkg, name: "#{name}_Event",
                                          fields: event_fields, constants: [], source: path, service: full)
      @services[full] = Service.new(full_name: full, pkg: pkg, name: name, request: parts["Request"],
                                    response: parts["Response"], event: @messages[event_full], source: path)
    end
  end

  # ---- type hashes ------------------------------------------------------

  class Hasher
    def initialize(registry)
      @reg = registry
    end

    def field_type(f)
      id = if f.nested
             NESTED_ID
           elsif f.string_max
             f.base == "string" ? BOUNDED_STRING_ID : BOUNDED_WSTRING_ID
           else
             PRIMITIVES.fetch(f.base)[0]
           end
      id += ARRAY_OFFSET[f.array] if f.array
      {
        "type_id" => id,
        "capacity" => f.array == :fixed || f.array == :bounded ? f.size : 0,
        "string_capacity" => f.string_max || 0,
        "nested_type_name" => f.nested || ""
      }
    end

    # The individual description of a type (no references), without defaults.
    def individual(full)
      t = @reg.type(full)
      fields = if t.is_a?(Service)
                 [["request_message", t.request.full_name], ["response_message", t.response.full_name],
                  ["event_message", t.event.full_name]].map do |n, nested|
                   { "name" => n, "type" => field_type(Field.new(base: nested, nested: nested)) }
                 end
               else
                 fs = t.fields
                 if fs.empty?
                   # rosidl gives an empty structure one placeholder member.
                   fs = [Field.new(name: "structure_needs_at_least_one_member", base: "uint8")]
                 end
                 fs.map { |f| { "name" => f.name, "type" => field_type(f) } }
               end
      { "type_name" => full, "fields" => fields }
    end

    def full_description(full)
      top = individual(full)
      refs = {}
      queue = top["fields"].map { |f| f["type"]["nested_type_name"] }.reject(&:empty?)
      until queue.empty?
        n = queue.pop
        next if refs.key?(n)
        refs[n] = individual(n)
        queue.concat(refs[n]["fields"].map { |f| f["type"]["nested_type_name"] }.reject(&:empty?))
      end
      { "type_description" => top, "referenced_type_descriptions" => refs.keys.sort.map { |k| refs[k] } }
    end

    def type_hash(full)
      "RIHS01_" + Digest::SHA256.hexdigest(AsterismMsgGen.py_json(full_description(full)))
    end
  end

  # Python's json.dumps(obj, ensure_ascii=True, separators=(", ", ": ")).
  def self.py_json(o)
    case o
    when Hash then "{" + o.map { |k, v| "#{py_str(k.to_s)}: #{py_json(v)}" }.join(", ") + "}"
    when Array then "[" + o.map { |v| py_json(v) }.join(", ") + "]"
    when String then py_str(o)
    when Integer then o.to_s
    when true then "true"
    when false then "false"
    when nil then "null"
    else raise Error, "cannot dump #{o.class}"
    end
  end

  def self.py_str(s)
    out = +'"'
    s.each_char do |c|
      out << case c
             when '"' then '\\"'
             when "\\" then "\\\\"
             when "\n" then "\\n"
             when "\r" then "\\r"
             when "\t" then "\\t"
             when "\b" then "\\b"
             when "\f" then "\\f"
             else
               cp = c.ord
               if cp < 0x20 || cp > 0x7e
                 if cp > 0xffff
                   cp -= 0x10000
                   format("\\u%04x\\u%04x", 0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff))
                 else
                   format("\\u%04x", cp)
                 end
               else
                 c
               end
             end
    end
    out << '"'
  end

  # ---- Ruby code --------------------------------------------------------

  class Emitter
    def initialize(registry, hasher)
      @reg = registry
      @hasher = hasher
    end

    def self.rel_path(full)
      "#{full}.rb"
    end

    # The Ruby constant path of a type, e.g. ::Asterism::ROS::GeometryMsgs::Twist
    def const_of(full)
      pkg, kind, name = AsterismMsgGen.split_name(full)
      if kind == "srv" && (m = name.match(/\A(.+)_(Request|Response|Event)\z/))
        "::Asterism::ROS::#{AsterismMsgGen.camel(pkg)}::#{m[1]}::#{m[2]}"
      else
        "::Asterism::ROS::#{AsterismMsgGen.camel(pkg)}::#{name}"
      end
    end

    # The types a file must load first (the messages its fields are made of,
    # other than the ones it defines itself).
    def runtime_deps(t)
      msgs = t.is_a?(Service) ? [t.request, t.response] : [t]
      own = msgs.map(&:full_name)
      msgs.flat_map { |m| m.fields.map(&:nested).compact }.uniq.reject { |n| own.include?(n) }.sort
    end

    def file(full)
      t = @reg.type(full)
      out = +""
      out << "# #{full} (ROS 2 #{ROS_DISTRO}), generated by asterism_msggen.rb from\n"
      out << "# #{File.basename(t.source)}. Do not edit; regenerate instead.\n"
      deps = runtime_deps(t)
      unless deps.empty?
        out << "\n"
        deps.each { |d| out << "::Asterism::ROS.require_type(#{d.inspect})\n" }
      end
      mod = "::Asterism::ROS::#{AsterismMsgGen.camel(t.pkg)}"
      # Full names, no nesting: on mruby each module body is more compiled
      # code and a deeper compile (report/r3.md). "::" because the file is
      # evaluated inside a method there (Asterism::ROS.require_type).
      out << "\nmodule #{mod}\nend\n\n"
      out << if t.is_a?(Service)
               service_body(t, mod)
             else
               message_body(t, "#{mod}::#{t.name}", @hasher.type_hash(full))
             end
      out
    end

    def service_body(s, mod)
      dds = "#{s.pkg}::srv::dds_::#{s.name}_"
      path = "#{mod}::#{s.name}"
      out = +""
      out << "# #{s.full_name}: a service. Request and Response are its messages;\n"
      out << "# TYPE_NAME and TYPE_HASH are the service's (what rmw_zenoh puts in keys).\n"
      out << "module #{path}\n"
      out << "  ROS_NAME = #{s.full_name.inspect}\n"
      out << "  TYPE_NAME = #{dds.inspect}\n"
      out << "  TYPE_HASH = #{@hasher.type_hash(s.full_name).inspect}\n"
      out << "end\n"
      [s.request, s.response].each do |m|
        out << "\n"
        suffix = m.name.sub("#{s.name}_", "")
        out << message_body(m, "#{path}::#{suffix}", @hasher.type_hash(m.full_name))
      end
      out
    end

    def dds_name(m)
      _, kind, = AsterismMsgGen.split_name(m.full_name)
      "#{m.pkg}::#{kind}::dds_::#{m.name}_"
    end

    def message_body(m, class_path, hash)
      fs = m.fields
      out = +""
      out << "class #{class_path} < ::Asterism::ROS::Message\n"
      out << "  ROS_NAME = #{m.full_name.inspect}\n"
      out << "  TYPE_NAME = #{dds_name(m).inspect}\n"
      out << "  TYPE_HASH = #{hash.inspect}\n"
      out << "  FIELDS = [#{fs.map { |f| ":#{f.name}" }.join(', ')}]\n"
      m.constants.each do |c|
        out << "  #{c.name} = #{lit(c.value, c.base)}\n"
      end
      out << "\n"
      out << "  attr_accessor #{fs.map { |f| ":#{f.name}" }.join(', ')}\n" unless fs.empty?
      out << "\n" unless fs.empty?
      # initialize
      if fs.empty?
        out << "  def initialize\n  end\n"
      else
        out << "  def initialize(#{fs.map { |f| "#{f.name}: nil" }.join(', ')})\n"
        fs.each { |f| out << "    @#{f.name} = #{init_expr(f)}\n" }
        out << "  end\n"
      end
      out << "\n"
      out << "  def self.write(w, m)\n"
      if fs.empty?
        out << "    w.uint8(0)\n"
      else
        fs.each { |f| out << "    #{write_stmt(f)}\n" }
      end
      out << "  end\n\n"
      out << "  def self.read(r)\n"
      if fs.empty?
        out << "    r.uint8\n    new\n"
      else
        out << "    m = new\n"
        fs.each { |f| out << "    m.#{f.name} = #{read_expr(f)}\n" }
        out << "    m\n"
      end
      out << "  end\n"
      out << "end\n"
    end

    def kind(f)
      PRIMITIVES.fetch(f.base)[1]
    end

    def byte_kind?(f)
      BYTE_KINDS.include?(f.base)
    end

    def lit(v, base)
      case v
      when Array then "[" + v.map { |x| lit(x, base) }.join(", ") + "]"
      when String then v.inspect
      when Float
        if v.nan? then "::Float::NAN"
        elsif v.infinite? then v > 0 ? "::Float::INFINITY" : "-::Float::INFINITY"
        else v.to_s
        end
      when Integer
        FLOAT_KINDS.include?(base) ? v.to_f.to_s : v.to_s
      else v.inspect
      end
    end

    def zero(f)
      case f.base
      when "bool" then "false"
      when "string", "wstring" then '""'
      when *FLOAT_KINDS then "0.0"
      else "0"
      end
    end

    # The value a field starts with when it is not given.
    def default_expr(f)
      if f.nested
        c = const_of(f.nested)
        case f.array
        when nil then "#{c}.new"
        when :fixed then "::Array.new(#{f.size}) { #{c}.new }"
        else "[]"
        end
      elsif f.array
        if f.default
          if byte_kind?(f)
            "::Asterism::CDR.byte_string(#{lit(f.default, f.base)})"
          else
            lit(f.default, f.base)
          end
        elsif byte_kind?(f)
          f.array == :fixed ? "\"\\x00\" * #{f.size}" : '""'
        elsif f.array == :fixed
          "::Array.new(#{f.size}, #{zero(f)})"
        else
          "[]"
        end
      else
        f.default.nil? ? zero(f) : lit(f.default, f.base)
      end
    end

    def init_expr(f)
      d = default_expr(f)
      if f.nested && f.array.nil?
        "#{const_of(f.nested)}.from(#{f.name})"
      elsif f.nested
        "#{f.name}.nil? ? #{d} : #{f.name}.map { |e| #{const_of(f.nested)}.from(e) }"
      else
        "#{f.name}.nil? ? #{d} : #{f.name}"
      end
    end

    def bound_args(f)
      case f.array
      when :fixed then "#{f.size}, nil"
      when :bounded then "nil, #{f.size}"
      else "nil, nil"
      end
    end

    def write_stmt(f)
      v = "m.#{f.name}"
      if f.nested
        c = const_of(f.nested)
        return "#{c}.write(w, #{c}.from(#{v}))" if f.array.nil?
        return "w.structs(#{c}, #{v}, #{bound_args(f)})"
      end
      if f.array
        return "w.bytes(#{v}, #{bound_args(f)})" if byte_kind?(f)
        return "w.array(:#{kind(f)}, #{v}, #{bound_args(f)}, #{f.string_max.inspect})" if f.string?
        return "w.array(:#{kind(f)}, #{v}, #{bound_args(f)})"
      end
      return "w.#{kind(f)}(#{v}, #{f.string_max})" if f.string? && f.string_max
      "w.#{kind(f)}(#{v})"
    end

    def read_expr(f)
      if f.nested
        c = const_of(f.nested)
        return "#{c}.read(r)" if f.array.nil?
        return "r.structs(#{c}, #{f.array == :fixed ? f.size : 'nil'})"
      end
      fixed = f.array == :fixed ? f.size : "nil"
      if f.array
        return "r.bytes(#{fixed})" if byte_kind?(f)
        return "r.array(:#{kind(f)}, #{fixed})"
      end
      "r.#{kind(f)}"
    end
  end

  # ---- driver -----------------------------------------------------------

  # Every type full name a set of requested types needs in the output
  # (the requested ones and the messages their fields use).
  def self.closure(registry, names)
    out = []
    queue = names.dup
    until queue.empty?
      n = queue.shift
      next if out.include?(n)
      out << n
      t = registry.type(n)
      msgs = t.is_a?(Service) ? [t.request, t.response] : [t]
      msgs.each { |m| m.fields.each { |f| queue << f.nested if f.nested } }
    end
    out
  end

  # All the hash names of a type: itself and what it references, as in the
  # type_hashes list of the ROS 2 type description JSON.
  def self.hash_names(hasher, full)
    d = hasher.full_description(full)
    [full] + d["referenced_type_descriptions"].map { |r| r["type_name"] }
  end

  # Compares the hashes with <dir>/<pkg>/<msg|srv>/<Name>.json. Returns
  # [checked, mismatches, missing].
  def self.check_json(hasher, names, dirs)
    checked = 0
    bad = []
    missing = []
    names.each do |full|
      pkg, kind, name = split_name(full)
      path = dirs.map { |d| File.join(d, pkg, kind, "#{name}.json") }.find { |p| File.file?(p) }
      if path.nil?
        missing << full
        next
      end
      JSON.parse(File.read(path))["type_hashes"].each do |th|
        mine = hasher.type_hash(th["type_name"])
        checked += 1
        bad << [th["type_name"], mine, th["hash_string"]] unless mine == th["hash_string"]
      end
    end
    [checked, bad, missing]
  end

  def self.main(argv)
    include_dirs = []
    out_dir = nil
    json_dirs = []
    mode = :generate
    deps = true
    op = OptionParser.new do |o|
      o.banner = "usage: asterism_msggen.rb [-I DIR]... (-o OUT | --hash | --check-json DIR) TYPE_OR_FILE..."
      o.on("-I DIR", "where <pkg>/msg/*.msg and <pkg>/srv/*.srv are (repeatable)") { |d| include_dirs << d }
      o.on("-o OUT", "write <OUT>/<pkg>/<msg|srv>/<Name>.rb") { |d| out_dir = d }
      o.on("--no-deps", "write only the named types, not the types they use") { deps = false }
      o.on("--hash", "print the RIHS01 hashes instead of writing files") { mode = :hash }
      o.on("--check-json DIR", "compare the hashes with the ROS 2 type description JSON under DIR") do |d|
        mode = :check
        json_dirs << d
      end
    end
    args = op.parse(argv)
    abort op.banner if args.empty?
    reg = Registry.new(include_dirs)
    names = args.map do |a|
      if a.end_with?(".msg") || a.end_with?(".srv")
        reg.include_dirs << File.dirname(File.dirname(File.dirname(File.expand_path(a))))
        reg.name_of_file(a)
      else
        a
      end
    end
    hasher = Hasher.new(reg)
    case mode
    when :hash
      names.each { |n| hash_names(hasher, n).each { |h| puts "#{h} #{hasher.type_hash(h)}" } }
    when :check
      all = closure(reg, names)
      checked, bad, missing = check_json(hasher, all, json_dirs)
      bad.each { |n, mine, theirs| warn "MISMATCH #{n}: computed #{mine}, json #{theirs}" }
      missing.each { |n| warn "no JSON for #{n}" }
      puts "type hashes: #{checked} checked, #{bad.size} mismatched, #{missing.size} without JSON"
      exit(bad.empty? && missing.empty? ? 0 : 1)
    else
      abort "-o OUT is needed" unless out_dir
      em = Emitter.new(reg, hasher)
      (deps ? closure(reg, names) : names).each do |n|
        path = File.join(out_dir, Emitter.rel_path(n))
        FileUtils.mkdir_p(File.dirname(path))
        File.write(path, em.file(n))
        puts path
      end
    end
  rescue Error => e
    abort "asterism_msggen: #{e.message}"
  end
end

AsterismMsgGen.main(ARGV) if $PROGRAM_NAME == __FILE__
