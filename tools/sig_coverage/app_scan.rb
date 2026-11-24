# frozen_string_literal: true

# What the apps call (count 1 of tools/sig_coverage). Every .rb is parsed
# with Prism, and each call is put in one of five kinds:
#
#   const   Machine.board_millis     the receiver is a constant: a class in
#                                    sig/, a class the apps define, or neither
#   ivar    @gfx.draw_text_mixed     the receiver is an instance variable that
#                                    sig/ declares with a type (@gfx: FmrbGfx)
#   untyped s.byteslice(0, 3)        any other receiver: the tool does not work
#                                    out its type, so only a name that no class
#                                    in sig/ has is a gap
#   bare    random_int(6)            no receiver (or self): the same name test
#   ref     FmrbColors::RED          a constant that is read, not called
#
# What the apps define themselves (classes, modules, constants such as
# TUNES, methods, attr_* names, Struct members) is not a gap. Those
# definitions are pooled over every app file and the library files the apps
# load (flash/lib, flash/usr/share), so a method one app defines hides the
# same name in another: the count errs on the side of fewer gaps.

require "prism"

module SigCoverage
  Use = Struct.new(:kind, :receiver, :name, :file, :line, keyword_init: true)

  class AppScan
    # Calls that declare rather than use an API: never a gap.
    DECLARATIVE = %w[
      require require_relative load attr_reader attr_writer attr_accessor
      private public protected module_function private_constant
      private_class_method public_class_method include extend prepend
      alias_method define_method
    ].freeze

    # Calls whose target the tool cannot see (count 4 of the report).
    DYNAMIC = %w[
      send __send__ public_send method respond_to? instance_variable_get
      instance_variable_set const_get instance_eval class_eval instance_exec
      eval define_method
    ].freeze

    attr_reader :files, :uses, :defs, :dynamic, :errors

    def initialize(app_files:, support_files: [], sig:, root: Dir.pwd)
      @sig = sig
      @root = root
      @files = app_files.sort
      @support_files = (support_files - app_files).sort
      @defs = { classes: {}, constants: {}, methods: {} }
      @uses = []
      @dynamic = Hash.new { |h, k| h[k] = Hash.new(0) }
      @errors = []
      @ivar_types = {}
      sig.classes.each_value do |klass|
        klass.ivars.each { |ivar, type| @ivar_types[ivar] = type.delete_prefix("::") }
      end
      parsed = {}
      (@files + @support_files).each do |path|
        result = Prism.parse(File.read(path, encoding: "UTF-8").scrub)
        @errors << "#{rel(path)}: #{result.errors.first.message}" unless result.errors.empty?
        parsed[path] = result.value
        DefCollector.new(@defs, @files.include?(path) ? :app : :support).visit(result.value)
      end
      @files.each do |path|
        UseCollector.new(self, rel(path)).visit(parsed[path])
      end
    end

    def rel(path) = path.delete_prefix("#{@root}/")

    def add_use(**kw) = @uses << Use.new(**kw)

    def note_dynamic(name, file) = @dynamic[name][file] += 1

    def ivar_type(name) = @ivar_types[name]

    # Where the apps' class of this name comes from: :app (an app file) or
    # :support (a library under flash/lib or flash/usr/share the apps load).
    def class_origin(name)
      table = @defs[:classes]
      return table[name] if table.key?(name)
      return nil unless name.include?("::")

      table.find { |k, _| k.end_with?("::#{name}") }&.last
    end

    def app_constant?(name) = @defs[:constants].key?(name) || @defs[:constants].key?(name.split("::").last)

    def app_method?(name) = @defs[:methods].key?(name)

    # Collects what the app files define.
    class DefCollector < Prism::Visitor
      def initialize(defs, origin)
        super()
        @defs = defs
        @origin = origin
        @namespace = []
      end

      def visit_class_node(node) = enter(node) { super }
      def visit_module_node(node) = enter(node) { super }

      def visit_constant_write_node(node)
        add_constant(node.name.to_s)
        super
      end

      def visit_constant_or_write_node(node)
        add_constant(node.name.to_s)
        super
      end

      def visit_constant_path_write_node(node)
        name = SigCoverage.const_name(node.target)
        add_constant(name) if name
        super
      end

      def visit_def_node(node)
        @defs[:methods][node.name.to_s] = true
        super
      end

      def visit_alias_method_node(node)
        name = node.new_name.respond_to?(:unescaped) ? node.new_name.unescaped : nil
        @defs[:methods][name] = true if name
        super
      end

      def visit_call_node(node)
        if node.receiver.nil? || node.receiver.is_a?(Prism::SelfNode)
          case node.name.to_s
          when "attr_reader" then symbols(node).each { |s| @defs[:methods][s] = true }
          when "attr_writer" then symbols(node).each { |s| @defs[:methods]["#{s}="] = true }
          when "attr_accessor"
            symbols(node).each { |s| @defs[:methods][s] = @defs[:methods]["#{s}="] = true }
          when "alias_method", "define_method"
            symbols(node).first(1).each { |s| @defs[:methods][s] = true }
          end
        elsif node.name == :new && SigCoverage.const_name(node.receiver) == "Struct"
          symbols(node).each { |s| @defs[:methods][s] = @defs[:methods]["#{s}="] = true }
        end
        super
      end

      private

      def enter(node)
        name = SigCoverage.const_name(node.constant_path) || "?"
        @namespace.push(name)
        full = @namespace.join("::")
        # :app wins: a class an app defines is its own even if a library has it too.
        # A library class counts by its full name only (a ROS message type Time is not ::Time).
        keys = @origin == :app ? [full, name.split("::").last] : [full]
        keys.each do |key|
          @defs[:classes][key] = @origin unless @defs[:classes][key] == :app
        end
        yield
      ensure
        @namespace.pop
      end

      def add_constant(name)
        @defs[:constants][name] = true
        @defs[:constants][([*@namespace, name]).join("::")] = true
      end

      def symbols(node)
        (node.arguments&.arguments || []).filter_map do |arg|
          arg.unescaped if arg.is_a?(Prism::SymbolNode) || arg.is_a?(Prism::StringNode)
        end
      end
    end

    # Records every call and constant read of one app file.
    class UseCollector < Prism::Visitor
      def initialize(scan, file)
        super()
        @scan = scan
        @file = file
      end

      def visit_call_node(node)
        name = node.name.to_s
        receiver = node.receiver
        line = node.location.start_line
        @scan.note_dynamic(name, @file) if DYNAMIC.include?(name)
        case receiver
        when nil, Prism::SelfNode
          unless DECLARATIVE.include?(name)
            @scan.add_use(kind: :bare, receiver: nil, name:, file: @file, line:)
          end
          visit_child_nodes(node)
        when Prism::ConstantReadNode, Prism::ConstantPathNode
          const = SigCoverage.const_name(receiver)
          if const
            @scan.add_use(kind: :const, receiver: const, name:, file: @file, line:)
            visit(node.arguments) if node.arguments
            visit(node.block) if node.block
          else
            @scan.add_use(kind: :untyped, receiver: nil, name:, file: @file, line:)
            visit_child_nodes(node)
          end
        when Prism::InstanceVariableReadNode
          type = @scan.ivar_type(receiver.name.to_s)
          kind = type ? :ivar : :untyped
          @scan.add_use(kind:, receiver: type, name:, file: @file, line:)
          visit_child_nodes(node)
        else
          @scan.add_use(kind: :untyped, receiver: nil, name:, file: @file, line:)
          visit_child_nodes(node)
        end
      end

      def visit_def_node(node)
        @scan.note_dynamic("method_missing", @file) if node.name == :method_missing
        super
      end

      def visit_constant_read_node(node)
        @scan.add_use(kind: :ref, receiver: node.name.to_s, name: nil, file: @file,
                      line: node.location.start_line)
      end

      def visit_constant_path_node(node)
        name = SigCoverage.const_name(node)
        if name
          @scan.add_use(kind: :ref, receiver: name, name: nil, file: @file,
                        line: node.location.start_line)
        else
          super
        end
      end
    end
  end

  # "A::B" for a constant read or path written with constants only; nil
  # for a path with a dynamic part (self.class::X).
  def self.const_name(node)
    case node
    when Prism::ConstantReadNode then node.name.to_s
    when Prism::ConstantPathNode, Prism::ConstantPathTargetNode
      node.full_name.delete_prefix("::")
    end
  rescue Prism::ConstantPathNode::DynamicPartsInConstantPathError,
         Prism::ConstantPathNode::MissingNodesInConstantPathError
    nil
  end
end
