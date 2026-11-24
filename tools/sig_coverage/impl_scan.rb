# frozen_string_literal: true

# What the implementation defines (count 2 of tools/sig_coverage).
#
# RubyImplScan reads mrblib files with Prism and keeps the public methods:
# a def after a bare `private`/`protected`, a `private def`, a name given to
# `private :name`, and the instance half of `module_function` are left out,
# as are names starting with `_` (sig/README.md: internal) and the hooks
# Ruby calls itself (method_missing and friends). attr_* names and aliases
# count as methods. `initialize` counts as the class method `new`, the way
# the editor's database turns a signature's initialize into `new`.
#
# CImplScan reads C sources for mrb_define_method / mrb_define_class_method /
# mrb_define_module_function / mrb_define_singleton_method (and their _id
# forms). The class each call targets is followed through the variables the
# file assigns from mrb_define_class / mrb_define_module / mrb_class_get ...
# (and their _under / _id forms) and the mrb->*_class fields. A target the
# file does not assign (a function parameter, a global set elsewhere) is
# listed as unresolved rather than guessed.

require "prism"

module SigCoverage
  ImplMethod = Struct.new(:class_name, :name, :singleton, :file, :line, :source, :api_private,
                          keyword_init: true) do
    def key = "#{class_name}#{singleton ? '.' : '#'}#{name}"
  end

  RUBY_HOOKS = %w[method_missing respond_to_missing? initialize_copy inherited
                  included extended method_added].freeze

  class RubyImplScan
    attr_reader :methods, :errors, :toplevel

    def initialize(files, root: Dir.pwd)
      @methods = []
      @errors = []
      @toplevel = []
      files.sort.each do |path|
        rel = path.delete_prefix("#{root}/")
        result = Prism.parse(File.read(path, encoding: "UTF-8").scrub)
        @errors << "#{rel}: #{result.errors.first.message}" unless result.errors.empty?
        comments = result.comments.to_h { |c| [c.location.start_line, c.location.slice] }
        visitor = Visitor.new(rel, comments)
        visitor.visit(result.value)
        @methods.concat(visitor.finish)
        @toplevel.concat(visitor.toplevel)
      end
    end

    Frame = Struct.new(:name, :visibility, :module_function, :singleton, :private_names,
                       :function_names, :private_class_names, keyword_init: true)

    class Visitor < Prism::Visitor
      VISIBILITY = %w[private protected public].freeze

      attr_reader :toplevel

      def initialize(file, comments = {})
        super()
        @file = file
        @comments = comments
        @frames = []
        @found = []
        @toplevel = []
        @force_visibility = nil
      end

      def finish
        @found.reject! do |m|
          frame = m.delete(:frame)
          (m[:singleton] ? frame.private_class_names : frame.private_names).include?(m[:name])
        end
        @found.map { |m| ImplMethod.new(**m, source: :ruby) }
      end

      def visit_class_node(node) = enter(node.constant_path) { super }
      def visit_module_node(node) = enter(node.constant_path) { super }

      def visit_singleton_class_node(node)
        frame = @frames.last
        return super unless frame && node.expression.is_a?(Prism::SelfNode)

        @frames.push(Frame.new(name: frame.name, visibility: :public, module_function: false,
                               singleton: true, private_names: frame.private_names,
                               function_names: frame.function_names,
                               private_class_names: frame.private_class_names))
        super
      ensure
        @frames.pop if frame && node.expression.is_a?(Prism::SelfNode)
      end

      def visit_def_node(node)
        frame = @frames.last
        name = node.name.to_s
        if frame.nil?
          @toplevel << "#{@file}:#{node.location.start_line} #{name}"
          return
        end
        visibility = @force_visibility || frame.visibility
        on_self = node.receiver.is_a?(Prism::SelfNode)
        singleton = frame.singleton || on_self
        # `private` does not reach `def self.x`; inside `class << self` it does.
        visible =
          if on_self && !frame.singleton
            @force_visibility.nil? || @force_visibility == :public
          else
            visibility == :public
          end
        if name == "initialize"
          add(frame, "new", true, node)
        elsif !singleton && frame.module_function
          add(frame, name, true, node)
        elsif visible
          add(frame, name, singleton, node)
        end
        # A def inside a def body is not a method of the class.
      end

      def visit_call_node(node)
        frame = @frames.last
        unless frame && (node.receiver.nil? || node.receiver.is_a?(Prism::SelfNode))
          return super
        end

        name = node.name.to_s
        args = node.arguments&.arguments || []
        case name
        when *VISIBILITY
          if args.empty?
            frame.visibility = name.to_sym
            frame.module_function = false
          else
            args.each do |arg|
              if arg.is_a?(Prism::DefNode)
                @force_visibility = name.to_sym
                visit(arg)
                @force_visibility = nil
              elsif name != "public"
                symbol_names([arg]).each { |s| frame.private_names << s }
              end
            end
          end
          return
        when "module_function"
          if args.empty?
            frame.module_function = true
          else
            symbol_names(args).each do |s|
              frame.private_names << s
              add(frame, s, true, node)
            end
          end
          return
        when "private_class_method"
          symbol_names(args).each { |s| frame.private_class_names << s }
          return
        when "attr_reader", "attr_writer", "attr_accessor"
          return if frame.visibility != :public
          symbol_names(args).each do |s|
            add(frame, s, frame.singleton, node) unless name == "attr_writer"
            add(frame, "#{s}=", frame.singleton, node) unless name == "attr_reader"
          end
          return
        when "alias_method"
          new_name = symbol_names(args).first
          add(frame, new_name, frame.singleton, node) if new_name && frame.visibility == :public
          return
        end
        super
      end

      def visit_alias_method_node(node)
        frame = @frames.last
        return unless frame && frame.visibility == :public

        name = node.new_name.respond_to?(:unescaped) ? node.new_name.unescaped : nil
        add(frame, name, frame.singleton, node) if name
      end

      private

      def enter(path)
        name = SigCoverage.const_name(path) || "?"
        outer = @frames.last
        absolute = path.is_a?(Prism::ConstantPathNode) && path.parent.nil? # class ::Foo
        full = outer && !absolute ? "#{outer.name}::#{name}" : name
        @frames.push(Frame.new(name: full, visibility: :public, module_function: false,
                               singleton: false, private_names: [], function_names: [],
                               private_class_names: []))
        yield
      ensure
        @frames.pop
      end

      def add(frame, name, singleton, node)
        return if name.nil? || name.start_with?("_") || RUBY_HOOKS.include?(name)

        line = node.location.start_line
        @found << { class_name: frame.name, name:, singleton:, file: @file, line:, frame:,
                    api_private: api_private?(line) }
      end

      # The comment block right above the line says `@api private` (the
      # Asterism gem marks its internal methods so).
      def api_private?(line)
        line -= 1
        while (text = @comments[line])
          return true if text.include?("@api private")
          line -= 1
        end
        false
      end

      def symbol_names(args)
        args.filter_map do |arg|
          arg.unescaped if arg.is_a?(Prism::SymbolNode) || arg.is_a?(Prism::StringNode)
        end
      end
    end
  end

  class CImplScan
    DEFINE = /\bmrb_define_(method|class_method|module_function|singleton_method)(_id)?\s*\(/
    GETTER = /\bmrb_(define_class|define_module|class_get|module_get|define_class_under|
               define_module_under|class_get_under|module_get_under)(_id)?\s*\(/x
    CORE_FIELDS = {
      "kernel_module" => "Kernel", "object_class" => "Object", "string_class" => "String",
      "integer_class" => "Integer", "float_class" => "Float", "array_class" => "Array",
      "hash_class" => "Hash", "module_class" => "Module", "class_class" => "Class",
      "proc_class" => "Proc", "symbol_class" => "Symbol", "nil_class" => "NilClass",
      "true_class" => "TrueClass", "false_class" => "FalseClass", "range_class" => "Range",
      "exc_class" => "Exception", "eStandardError_class" => "StandardError",
    }.freeze
    OPSYM = {
      "add" => "+", "sub" => "-", "mul" => "*", "div" => "/", "mod" => "%", "pow" => "**",
      "eq" => "==", "neq" => "!=", "lt" => "<", "le" => "<=", "gt" => ">", "ge" => ">=",
      "cmp" => "<=>", "eqq" => "===", "aref" => "[]", "aset" => "[]=", "lshift" => "<<",
      "rshift" => ">>", "and" => "&", "or" => "|", "xor" => "^", "neg" => "~", "not" => "!",
      "minus" => "-@", "plus" => "+@", "match" => "=~", "nmatch" => "!~",
    }.freeze

    attr_reader :methods, :unresolved

    # classes: when given, only methods of these classes (and the classes
    # nested in them) are kept -- for the upstream gems, of which only the
    # classes the apps see are counted.
    def initialize(files, root: Dir.pwd, classes: nil)
      @methods = []
      @unresolved = []
      files.sort.each do |path|
        rel = path.delete_prefix("#{root}/")
        scan(strip_comments(File.read(path, encoding: "UTF-8").scrub), rel, classes)
      end
    end

    private

    def strip_comments(text)
      text.gsub(%r{/\*.*?\*/}m) { |c| "\n" * c.count("\n") }
          .gsub(%r{//[^\n]*}, "")
    end

    def scan(text, file, classes)
      vars = {}
      # `struct RClass *k = mrb->kernel_module;` -- a core class kept in a variable.
      text.scan(/(\w+)\s*=\s*\w+->(\w+)\s*;/) do |var, field|
        vars[var] = CORE_FIELDS[field] if CORE_FIELDS.key?(field)
      end
      pos = 0
      while (m = text.match(/#{DEFINE}|#{GETTER}/o, pos))
        open = m.end(0) - 1
        args, close = split_args(text, open)
        pos = close || m.end(0)
        line = text[0, m.begin(0)].count("\n") + 1
        if m[0].match?(DEFINE)
          kind = m[0][DEFINE, 1]
          record(vars, args, kind, file, line, classes)
        else
          name = evaluate("#{m[0]}#{args.join(',')})", vars)
          before = text[0, m.begin(0)]
          if name && (var = before[/(\w+)\s*=\s*(?:\(\s*struct\s+RClass\s*\*\s*\)\s*)?\z/, 1])
            vars[var] = name
          end
        end
      end
    end

    def record(vars, args, kind, file, line, classes)
      return if args.size < 3

      target = evaluate(args[1], vars)
      name = method_name(args[2])
      if target.nil? || name.nil?
        @unresolved << "#{file}:#{line} #{args[1].strip} #{args[2].strip}"
        return
      end
      return if classes && !classes.any? { |c| target == c || target.start_with?("#{c}::") }
      return if name.start_with?("_")

      # A module function is also a private instance method; only the class
      # method is what an app calls.
      singleton = kind != "method"
      name, singleton = "new", true if name == "initialize" && !singleton
      @methods << ImplMethod.new(class_name: target, name:, singleton:, file:, line:, source: :c)
    end

    def method_name(arg)
      arg = arg.strip
      if (s = arg[/\A"((?:[^"\\]|\\.)*)"\z/, 1]) then s
      elsif (s = arg[/\AMRB_SYM\((\w+)\)\z/, 1]) then s
      elsif (s = arg[/\AMRB_SYM_Q\((\w+)\)\z/, 1]) then "#{s}?"
      elsif (s = arg[/\AMRB_SYM_B\((\w+)\)\z/, 1]) then "#{s}!"
      elsif (s = arg[/\AMRB_SYM_E\((\w+)\)\z/, 1]) then "#{s}="
      elsif (s = arg[/\AMRB_OPSYM\((\w+)\)\z/, 1]) then OPSYM[s]
      elsif (s = arg[/\Amrb_intern_(?:lit|cstr)\(\s*\w+\s*,\s*"([^"]*)"\s*\)\z/, 1]) then s
      end
    end

    # The class an expression names, or nil.
    def evaluate(expr, vars)
      expr = expr.strip.sub(/\A\(\s*struct\s+R\w+\s*\*\s*\)\s*/, "").strip
      return vars[expr] if vars.key?(expr)
      if (field = expr[/\A\w+->(\w+)\z/, 1])
        return CORE_FIELDS[field]
      end
      m = expr.match(/\A#{GETTER}/o) or return nil
      args, = split_args(expr, m.end(0) - 1)
      under = m[1].end_with?("_under")
      if under
        outer = evaluate(args[1].to_s, vars) or return nil
        leaf = const_name(args[2]) or return nil
        "#{outer}::#{leaf}"
      else
        const_name(args[1])
      end
    end

    def const_name(arg)
      return nil unless arg
      arg = arg.strip
      arg[/\A"([A-Za-z_]\w*)"\z/, 1] || arg[/\AMRB_SYM\((\w+)\)\z/, 1]
    end

    # The top-level arguments of the call whose "(" is at open, and the
    # index after its ")".
    def split_args(text, open)
      depth = 0
      args = []
      current = +""
      i = open
      in_string = false
      while i < text.size
        c = text[i]
        if in_string
          current << c
          if c == "\\"
            current << text[i + 1].to_s
            i += 1
          elsif c == '"'
            in_string = false
          end
        elsif c == '"'
          in_string = true
          current << c
        elsif c == "("
          depth += 1
          current << c unless depth == 1
        elsif c == ")"
          depth -= 1
          if depth.zero?
            args << current
            return [args, i + 1]
          end
          current << c
        elsif c == "," && depth == 1
          args << current
          current = +""
        else
          current << c
        end
        i += 1
      end
      [args, nil]
    end
  end
end
