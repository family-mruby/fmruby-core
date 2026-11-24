# frozen_string_literal: true

# The signatures in sig/*.rbs, read the way the editor's database generator
# reads them (vendor/picoruby-ti/tidbgen): the rbs gem's parser, nested
# declarations named by their namespace, public methods only, `initialize`
# standing for `new`, the first line of the visible comment as the summary.
#
# Lookups follow the generator's ancestor rules (database_builder.rb):
# instance methods come from the class, its superclass chain and the modules
# it includes; class methods from the class, its superclass chain and the
# modules it extends; both end with Object's table.

require "rbs"

module SigCoverage
  SigMethod = Struct.new(:class_name, :name, :singleton, :comment, :file, :line,
                         :annotations, :alias_of, keyword_init: true) do
    def key = "#{class_name}#{singleton ? '.' : '#'}#{name}"
  end

  SigClass = Struct.new(:name, :kind, :files, :ancestors, :instance_methods,
                        :static_methods, :constants, :ivars, keyword_init: true)

  class SigIndex
    attr_reader :classes, :constants, :files, :errors

    def initialize(paths)
      @classes = {}
      @constants = {} # "A::B" => type text (constants that are values)
      @files = paths.sort
      @errors = []
      @files.each do |path|
        declarations = RBS::Parser.parse_signature(File.read(path)).last
        collect(declarations, "", path)
      rescue RBS::ParsingError => e
        @errors << "#{path}: #{e.message}"
      end
    end

    def self.from_dir(dir)
      new(Dir[File.join(dir, "*.rbs")])
    end

    def class?(name) = @classes.key?(name)

    # Every method of the class itself, in the order it was declared.
    def own_methods(name)
      klass = @classes[name] or return []
      klass.instance_methods.values + klass.static_methods.values
    end

    def all_methods = @classes.values.flat_map { |c| c.instance_methods.values + c.static_methods.values }

    def method_names
      @method_names ||= all_methods.map(&:name).to_h { |n| [n, true] }
    end

    def instance_method(class_name, name)
      lookup(class_name, name, static: false)
    end

    def static_method(class_name, name)
      lookup(class_name, name, static: true)
    end

    private

    def lookup(class_name, name, static:, seen: {})
      return nil if seen[class_name]
      seen[class_name] = true
      klass = @classes[class_name] or return nil
      table = static ? klass.static_methods : klass.instance_methods
      return table[name] if table.key?(name)
      klass.ancestors.each do |kind, ancestor|
        next if static ? kind == :include : kind == :extend
        found = lookup(ancestor, name, static:, seen:)
        return found if found
      end
      return nil if class_name == "Object"
      lookup("Object", name, static:, seen:)
    end

    def full_name(name, namespace)
      text = name.to_s
      return text.delete_prefix("::") if text.start_with?("::")
      namespace.empty? ? text : "#{namespace}::#{text}"
    end

    # A superclass or module written inside a namespace may be relative to it
    # (`class RemoteError < Error` in `module Asterism`); take the nested name
    # when the signatures have it.
    def resolve_ref(name, namespace)
      text = name.to_s
      return text.delete_prefix("::") if text.start_with?("::")
      parts = namespace.split("::")
      until parts.empty?
        candidate = (parts + [text]).join("::")
        return candidate if @classes.key?(candidate)
        parts.pop
      end
      text
    end

    def collect(declarations, namespace, path)
      declarations.each do |decl|
        case decl
        when RBS::AST::Declarations::Class, RBS::AST::Declarations::Module
          collect_class(decl, namespace, path)
        when RBS::AST::Declarations::Constant
          @constants[full_name(decl.name, namespace)] = decl.type.to_s
        end
      end
    end

    def collect_class(decl, namespace, path)
      name = full_name(decl.name, namespace)
      kind = decl.is_a?(RBS::AST::Declarations::Class) ? :class : :module
      klass = (@classes[name] ||= SigClass.new(name:, kind:, files: [], ancestors: [],
                                                instance_methods: {}, static_methods: {},
                                                constants: {}, ivars: {}))
      klass.files << path unless klass.files.include?(path)
      if decl.respond_to?(:super_class) && decl.super_class
        klass.ancestors << [:super, resolve_ref(decl.super_class.name, namespace)]
      end

      visibility = :public
      decl.members.each do |member|
        case member
        when RBS::AST::Members::Public then visibility = :public
        when RBS::AST::Members::Private then visibility = :private
        when RBS::AST::Members::Include then klass.ancestors << [:include, resolve_ref(member.name, name)]
        when RBS::AST::Members::Extend then klass.ancestors << [:extend, resolve_ref(member.name, name)]
        when RBS::AST::Members::Prepend then klass.ancestors << [:prepend, resolve_ref(member.name, name)]
        when RBS::AST::Members::InstanceVariable
          klass.ivars[member.name.to_s] = member.type.to_s
        when RBS::AST::Declarations::Constant
          klass.constants[member.name.to_s] = member.type.to_s
          @constants["#{name}::#{member.name}"] = member.type.to_s
        when RBS::AST::Members::MethodDefinition
          next if visibility == :private || member.visibility == :private
          add_method(klass, member, path)
        when RBS::AST::Members::Alias
          add_alias(klass, member, path)
        end
      end

      nested = decl.members.grep(RBS::AST::Declarations::Base)
      collect(nested, name, path)
    end

    def add_method(klass, member, path)
      comment = visible_comment(member.comment)
      annotations = member.annotations.map(&:string)
      line = member.location&.start_line
      build = lambda do |method_name, singleton|
        SigMethod.new(class_name: klass.name, name: method_name, singleton:, comment:,
                      file: path, line:, annotations:)
      end
      if member.name == :initialize
        klass.static_methods["new"] ||= build.call("new", true)
        return
      end
      name = member.name.to_s
      case member.kind
      when :instance then klass.instance_methods[name] ||= build.call(name, false)
      when :singleton then klass.static_methods[name] ||= build.call(name, true)
      when :singleton_instance
        klass.instance_methods[name] ||= build.call(name, false)
        klass.static_methods[name] ||= build.call(name, true)
      end
    end

    def add_alias(klass, member, path)
      singleton = member.kind == :singleton
      table = singleton ? klass.static_methods : klass.instance_methods
      source = table[member.old_name.to_s] or return
      table[member.new_name.to_s] ||= source.dup.tap do |m|
        m.name = member.new_name.to_s
        m.alias_of = source.name
        m.file = path
        m.line = member.location&.start_line
      end
    end

    # tidbgen's build_visible_comment: lines holding an HTML comment are
    # dropped, then the text is stripped.
    def visible_comment(comment)
      comment&.string.to_s.lines.reject { |line| line.match?(/<!--.*-->/) }.join.strip
    end
  end
end
