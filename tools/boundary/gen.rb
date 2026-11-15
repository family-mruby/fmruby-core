#!/usr/bin/env ruby
# frozen_string_literal: true

# Boundary generator (doc/app_boundary, first half of W4a: one function).
#
# Reads sig/*.rbs with the rbs gem, picks the methods marked with the
# annotation %a{fmrb.boundary}, turns each into one flat C function by fixed
# rules, and writes the glue of every engine from that one table:
#
#   in the firmware, made on every build, not committed
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary.h      flat declarations
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_mrb.inc mruby gem methods
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_lua.inc Lua methods
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_mp.inc  MicroPython functions
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_wasm.inc WAMR raw natives
#     main/ruby/spinel/fmrb_gen/fmrb_boundary_ffi.rb                Spinel ffi_func
#   the SDK, committed (checked by --check)
#     sdk/c/fmrb_boundary.h                                         C header (reference)
#
#   ruby tools/boundary/gen.rb            # write what changed
#   ruby tools/boundary/gen.rb --check    # fail when the committed SDK is stale
#
# The rules are the minimum the first function needs (Integer arguments, a
# receiver number, a method returning self). The rest of the rule table in
# doc/app_boundary/plan.md comes in the second half of W4a.

require 'rbs'
require 'erb'
require 'digest'
require 'fileutils'

ROOT = File.expand_path('../..', __dir__)
SIG_DIR = File.join(ROOT, 'sig')
TEMPLATE_DIR = File.join(__dir__, 'templates')
MARK = 'fmrb.boundary'

FW_DIR = File.join(ROOT, 'components/fmrb_common/include/fmrb_gen')
OUTPUTS = {
  'fmrb_boundary.h.erb' => File.join(FW_DIR, 'fmrb_boundary.h'),
  'fmrb_boundary_mrb.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_mrb.inc'),
  'fmrb_boundary_lua.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_lua.inc'),
  'fmrb_boundary_mp.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_mp.inc'),
  'fmrb_boundary_wasm.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_wasm.inc'),
  'fmrb_boundary_ffi.rb.erb' => File.join(ROOT, 'main/ruby/spinel/fmrb_gen/fmrb_boundary_ffi.rb')
}.freeze
SDK_OUTPUTS = {
  'sdk_c.h.erb' => File.join(ROOT, 'sdk/c/fmrb_boundary.h')
}.freeze

# ---------------------------------------------------------------- the table

Param = Struct.new(:name, :ctype, keyword_init: true)
Func = Struct.new(:klass, :method, :flat, :import_name, :recv, :params,
                  :returns_self, :doc_ja, :doc_en, keyword_init: true) do
  # receiver first, then the method's own arguments
  def all_params
    (recv ? [Param.new(name: recv, ctype: 'int32_t')] : []) + params
  end

  def c_args
    all_params.map { |p| "#{p.ctype} #{p.name}" }.join(', ')
  end
end

# FmrbGfx -> gfx: the receiver number's name, and the prefix of the flat name.
def short_name(klass)
  klass.sub(/\AFmrb/, '').gsub(/([a-z0-9])([A-Z])/, '\1_\2').downcase
end

def ctype_of(type, where)
  case type.to_s
  when 'Integer' then 'int32_t'
  else abort "#{where}: type #{type} is not handled by the boundary generator yet"
  end
end

# "ja <<en>> en" -> [ja, en]; one language only -> the same text twice.
def split_summary(comment)
  first = comment.to_s.lines.first.to_s.strip
  ja, en = first.split('<<en>>', 2).map(&:strip)
  [ja, en || ja]
end

def collect
  funcs = []
  Dir[File.join(SIG_DIR, '*.rbs')].sort.each do |path|
    _, _, decls = RBS::Parser.parse_signature(RBS::Buffer.new(name: path, content: File.read(path)))
    decls.each do |decl|
      next unless decl.respond_to?(:members)

      klass = decl.name.to_s
      decl.members.each do |m|
        next unless m.is_a?(RBS::AST::Members::MethodDefinition)
        next unless m.annotations.any? { |a| a.string == MARK }

        where = "#{File.basename(path)}: #{klass}##{m.name}"
        abort "#{where}: one signature only (no overloads)" unless m.overloads.size == 1
        fn = m.overloads.first.method_type.type
        unless fn.optional_positionals.empty? && fn.rest_positionals.nil? &&
               fn.required_keywords.empty? && fn.optional_keywords.empty?
          abort "#{where}: a boundary function takes required positional arguments only"
        end
        instance = m.kind == :instance
        returns_self = fn.return_type.to_s == klass
        abort "#{where}: return type #{fn.return_type} is not handled yet" unless returns_self

        prefix = short_name(klass)
        ja, en = split_summary(m.comment&.string)
        funcs << Func.new(
          klass: klass, method: m.name.to_s,
          flat: "fmrb_#{prefix}_#{m.name}", import_name: "#{prefix}_#{m.name}",
          recv: instance ? prefix : nil,
          params: fn.required_positionals.map do |p|
            abort "#{where}: every argument needs a name" unless p.name
            Param.new(name: p.name.to_s, ctype: ctype_of(p.type, where))
          end,
          returns_self: returns_self, doc_ja: ja, doc_en: en
        )
      end
    end
  end
  abort "no method in #{SIG_DIR} carries %a{#{MARK}}" if funcs.empty?
  funcs
end

# ---------------------------------------------------------------- output

def render(template, funcs)
  abi_hash = Digest::SHA256.hexdigest(funcs.map { |f| [f.flat, f.c_args].join(':') }.join("\n"))[0, 8]
  ERB.new(File.read(File.join(TEMPLATE_DIR, template)), trim_mode: '-')
     .result_with_hash(funcs: funcs, abi_hash: abi_hash)
end

# Only a changed file is rewritten, so a build that regenerates nothing new
# recompiles nothing.
def write_if_changed(path, text)
  return false if File.exist?(path) && File.read(path) == text

  FileUtils.mkdir_p(File.dirname(path))
  File.write(path, text)
  true
end

funcs = collect
if ARGV.include?('--check')
  stale = SDK_OUTPUTS.reject { |t, path| File.exist?(path) && File.read(path) == render(t, funcs) }
  unless stale.empty?
    abort "boundary SDK out of date with sig/: #{stale.values.map { |p| p.sub("#{ROOT}/", '') }.join(', ')}" \
          ' (run ruby tools/boundary/gen.rb and commit)'
  end
  puts "boundary: SDK matches sig/ (#{funcs.size} function#{'s' unless funcs.size == 1})"
  exit
end

changed = OUTPUTS.merge(SDK_OUTPUTS).select { |t, path| write_if_changed(path, render(t, funcs)) }
puts "boundary: #{funcs.size} function#{'s' unless funcs.size == 1}, " \
     "#{changed.empty? ? 'nothing changed' : "wrote #{changed.values.map { |p| p.sub("#{ROOT}/", '') }.join(', ')}"}"
