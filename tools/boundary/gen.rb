#!/usr/bin/env ruby
# frozen_string_literal: true

# Boundary generator (doc/app_boundary W4a).
#
# Reads sig/*.rbs with the rbs gem. A class or module marked %a{fmrb.boundary}
# puts every method of its own on the app boundary, except the ones marked
# %a{fmrb.internal} (written in Ruby, or state an engine's object keeps) and
# the hooks marked %a{fmrb.hook} (what the app exports; listed, not called).
# Each boundary method becomes one flat C function by the fixed rules below,
# and the glue of every engine is written from that one table. The firmware
# calls it fmrb_bnd_<class>_<method> (its body and every engine's glue); a C
# app calls it fmrb_<class>_<method> (the SDK header, the wasm import
# "<class>_<method>"):
#
#   in the firmware, made on every build, not committed
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary.h       flat declarations, records
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_mrb.inc  mruby gem methods
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_lua.inc  Lua methods
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_mp.inc   MicroPython _fmrb functions
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_py.h     MicroPython classes (Python source)
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_wasm.inc WAMR raw natives
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_spx.inc  Spinel adaptors (results as bytes)
#     components/fmrb_common/include/fmrb_gen/fmrb_boundary_stub.inc stand-in bodies (wamr_poc host)
#     main/ruby/spinel/fmrb_gen/fmrb_boundary_ffi.rb                 Spinel ffi_func
#   the SDK, committed (checked by --check)
#     sdk/c/fmrb_boundary.h                                          C header (the reference)
#
#   ruby tools/boundary/gen.rb            # write what changed
#   ruby tools/boundary/gen.rb --check    # fail when a committed product is stale
#
# The rules (doc/app_boundary/plan.md, "作るもの 2" and report/w4a.md 3):
#
#   receiver           instance method -> int32_t <class> first; singleton -> none
#   Integer / intN...  int32_t, or the alias's width (sig/fmrb_types.rbs)
#   bool               int32_t 0 / 1
#   Float / floatN     double / float
#   String / bin       pointer and length (const char * / const uint8_t *, int32_t)
#   :a | :b            int32_t, the literal's index (FMRB_<CLASS>_<METHOD>_<ARG>_<A>)
#   record alias       const pointer to a packed struct (fmrb_<class>_<name>_t)
#   Array[record|int]  pointer and count
#   returns self       int32_t status; the glue gives back the receiver
#   returns Integer    int32_t, 0 or more; negative is an error
#   returns bool       int32_t 1 / 0; negative is an error
#   returns record(?)  a pointer to the caller's struct; 1 filled, 0 none (nil)
#   returns String/bin the caller's buffer and its size; returns the full
#                      length (as snprintf does), negative on error
#   errors             -1 failed, -2 bad argument (ArgumentError / ValueError)
#
# A method with optional, rest or keyword arguments is not a boundary function:
# its wrapper is marked internal and the simple form is written next to it
# with "_raw" after the name. A wrapper is each engine's Ruby, with one
# exception the tool need not know: mruby's FmrbGfx#present is a C method of
# the gem (gfx.c), called every frame. A boundary method needs its C body: the tool
# looks for the definition of every flat function (fmrb_bnd_...) in main/app/fmrb_boundary_*.c
# and stops, naming the method, when one is missing (a method written in Ruby
# must be marked internal instead).

require 'rbs'
require 'erb'
require 'digest'
require 'fileutils'

ROOT = File.expand_path('../..', __dir__)
SIG_DIR = File.join(ROOT, 'sig')
TEMPLATE_DIR = File.join(__dir__, 'templates')
BODY_GLOB = File.join(ROOT, 'main/app/fmrb_boundary_*.c')
MARK_CLASS = 'fmrb.boundary'
MARK_INTERNAL = 'fmrb.internal'
MARK_HOOK = 'fmrb.hook'

FW_DIR = File.join(ROOT, 'components/fmrb_common/include/fmrb_gen')
OUTPUTS = {
  'fmrb_boundary.h.erb' => File.join(FW_DIR, 'fmrb_boundary.h'),
  'fmrb_boundary_mrb.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_mrb.inc'),
  'fmrb_boundary_lua.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_lua.inc'),
  'fmrb_boundary_mp.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_mp.inc'),
  'fmrb_boundary_py.h.erb' => File.join(FW_DIR, 'fmrb_boundary_py.h'),
  'fmrb_boundary_wasm.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_wasm.inc'),
  'fmrb_boundary_spx.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_spx.inc'),
  'fmrb_boundary_stub.inc.erb' => File.join(FW_DIR, 'fmrb_boundary_stub.inc'),
  'fmrb_boundary_ffi.rb.erb' => File.join(ROOT, 'main/ruby/spinel/fmrb_gen/fmrb_boundary_ffi.rb')
}.freeze
SDK_OUTPUTS = {
  'sdk_c.h.erb' => File.join(ROOT, 'sdk/c/fmrb_boundary.h')
}.freeze

# MicroPython's qstr table is a committed product of rake micropython:gen
# (components/micropython/mp_embed). A name the generated module uses and the
# table lacks compiles to nothing that works, so --check reads it.
MP_QSTR_TABLE = File.join(ROOT, 'components/micropython/mp_embed/genhdr/qstrdefs.generated.h')

# ---------------------------------------------------------------- types

# The width aliases of sig/fmrb_types.rbs, told apart by name (decision 10).
INT_WIDTHS = {
  'int8' => ['int8_t', -0x80, 0x7f], 'int16' => ['int16_t', -0x8000, 0x7fff],
  'int32' => ['int32_t', -0x80000000, 0x7fffffff],
  'int64' => ['int64_t', -0x8000000000000000, 0x7fffffffffffffff],
  'uint8' => ['uint8_t', 0, 0xff], 'uint16' => ['uint16_t', 0, 0xffff],
  'uint32' => ['uint32_t', 0, 0xffffffff], 'uint64' => ['uint64_t', 0, 0xffffffffffffffff]
}.freeze
FLOAT_WIDTHS = { 'float32' => 'float', 'float64' => 'double' }.freeze
ALIAS_BASE = INT_WIDTHS.keys.to_h { |k| [k, 'Integer'] }
                       .merge(FLOAT_WIDTHS.keys.to_h { |k| [k, 'Float'] }, 'bin' => 'String').freeze

# kind: :int :bool :float :str :bin :choice :record :array
Ty = Struct.new(:kind, :ctype, :min, :max, :values, :record, :elem, :optional, keyword_init: true) do
  def int? = kind == :int
  def wide? = int? && %w[int64_t uint64_t uint32_t].include?(ctype)
  def to_s = (kind == :record ? record.name : kind.to_s)
end
Field = Struct.new(:name, :ty)
# cname is the firmware's own name of the record (fmrb_bnd_<class>_<name>_t),
# api_cname what a C app sees (fmrb_<class>_<name>_t, the SDK header).
Rec = Struct.new(:name, :klass, :cname, :api_cname, :fields) do
  def size = fields.sum { |f| FIELD_SIZE.fetch(f.ty.ctype) }
end
FIELD_SIZE = { 'int8_t' => 1, 'uint8_t' => 1, 'int16_t' => 2, 'uint16_t' => 2, 'int32_t' => 4,
               'uint32_t' => 4, 'int64_t' => 8, 'uint64_t' => 8, 'float' => 4, 'double' => 8 }.freeze

Param = Struct.new(:name, :ty)
# flat is the firmware's own name of the function (fmrb_bnd_<class>_<method>:
# the body in main/app/fmrb_boundary_*.c and every engine's glue), api the
# name a C app calls it by (fmrb_<class>_<method>: the SDK header, and the
# wasm import "<class>_<method>"). A C app is linked apart from the firmware
# (inside WAMR), so only the firmware's names share a namespace with the
# firmware's own API (decided 2026-11-15, doc/app_boundary/plan.md).
Func = Struct.new(:klass, :method, :prefix, :flat, :api, :import_name, :recv, :params, :ret,
                  :doc_ja, :doc_en, :singleton, keyword_init: true) do
  # ret: :self, :void, or a Ty (:int, :bool, :choice, :record, :str, :bin)
  def returns_self? = ret == :self
  def returns_record? = ret.is_a?(Ty) && ret.kind == :record
  def returns_bytes? = ret.is_a?(Ty) && %i[str bin].include?(ret.kind)
  def raw? = method.end_with?('_raw')
  def recv_param = recv ? [['int32_t', recv]] : []

  # The flat function's C parameters, in order: [[ctype, name], ...]; with
  # api: true, the record types by the names a C app sees.
  def c_params(api: false)
    ps = recv_param.dup
    params.each { |p| ps.concat(C.params_of(p.ty, p.name, api: api)) }
    if returns_record?
      ps << ["#{api ? ret.record.api_cname : ret.record.cname} *", 'out']
    elsif returns_bytes?
      ps << [ret.kind == :bin ? 'uint8_t *' : 'char *', 'out'] << %w[int32_t out_size]
    end
    ps
  end

  def c_args(api: false) = c_params(api: api).map { |t, n| t.end_with?('*') ? "#{t}#{n}" : "#{t} #{n}" }.join(', ')

  # The number of one Symbol choice of an argument: FMRB_BND_GFX_SET_FONT_RAW_FAMILY_JA
  # in the firmware, FMRB_GFX_SET_FONT_RAW_FAMILY_JA for a C app.
  def choice_const(param, value, api: false) = "#{api ? self.api : flat}_#{param.name}_#{value}".upcase
  def ruby_sig = "#{klass}#{singleton ? '.' : '#'}#{method}(#{params.map(&:name).join(', ')})"
end
Hook = Struct.new(:klass, :method, :doc_ja, :doc_en, keyword_init: true)

module C
  module_function

  def params_of(ty, name, api: false)
    rec = ->(r) { api ? r.api_cname : r.cname }
    case ty.kind
    when :int, :choice then [[ty.ctype, name]]
    when :bool then [['int32_t', name]]
    when :float then [[ty.ctype, name]]
    when :str then [['const char *', name], ['int32_t', "#{name}_len"]]
    when :bin then [['const uint8_t *', name], ['int32_t', "#{name}_len"]]
    when :record then [["const #{rec.(ty.record)} *", name]]
    when :array then [["const #{ty.elem.kind == :record ? rec.(ty.elem.record) : ty.elem.ctype} *", name],
                      ['int32_t', "#{name}_count"]]
    end
  end
end

# The class's short name: FmrbGfx -> gfx, the prefix of every flat name.
def short_name(klass)
  klass.sub(/\AFmrb/, '').gsub(/([a-z0-9])([A-Z])/, '\1_\2').downcase
end

# "ja <<en>> en" -> [ja, en]; one language only -> the same text twice.
def split_summary(comment)
  first = comment.to_s.lines.first.to_s.strip
  ja, en = first.split('<<en>>', 2).map(&:strip)
  [ja, en || ja]
end

class Collector
  attr_reader :funcs, :hooks, :records

  def initialize
    @funcs = []
    @hooks = []
    @records = {}   # "Klass::name" / "name" -> Rec (filled lazily)
    @aliases = {}   # same keys -> RBS type
  end

  def run
    decls_by_file = Dir[File.join(SIG_DIR, '*.rbs')].sort.map do |path|
      _, _, decls = RBS::Parser.parse_signature(RBS::Buffer.new(name: path, content: File.read(path)))
      [path, decls]
    end
    decls_by_file.each { |_, decls| gather_aliases(decls, nil) }
    check_width_aliases
    decls_by_file.each do |path, decls|
      decls.each { |d| visit(path, d) if d.respond_to?(:members) }
    end
    abort "no class in #{SIG_DIR} carries %a{#{MARK_CLASS}}" if @funcs.empty?
    check_bodies
    check_raw_names
    self
  end

  private

  def gather_aliases(decls, klass)
    decls.each do |d|
      case d
      when RBS::AST::Declarations::TypeAlias
        @aliases[[klass, d.name.to_s].compact.join('::')] = d.type
      when RBS::AST::Declarations::Class, RBS::AST::Declarations::Module
        gather_aliases(d.members, d.name.to_s)
      end
    end
  end

  def check_width_aliases
    ALIAS_BASE.each do |name, base|
      t = @aliases[name]
      abort "sig: the width alias #{name} (= #{base}) is missing (sig/fmrb_types.rbs)" unless t
      abort "sig: #{name} must be #{base}, not #{t}" unless t.to_s == base
    end
  end

  def marks(node) = node.annotations.map(&:string)

  def visit(path, decl)
    klass = decl.name.to_s
    on_class = marks(decl).include?(MARK_CLASS)
    decl.members.each do |m|
      next unless m.is_a?(RBS::AST::Members::MethodDefinition)

      where = "#{File.basename(path)}: #{klass}#{m.kind == :singleton ? '.' : '#'}#{m.name}"
      ms = marks(m)
      if ms.include?(MARK_CLASS)
        abort "#{where}: %a{#{MARK_CLASS}} goes on the class, not on a method " \
              '(doc/app_boundary/plan.md, W4a decisions)'
      end
      next unless on_class
      next if ms.include?(MARK_INTERNAL)

      ja, en = split_summary(m.comment&.string)
      if ms.include?(MARK_HOOK)
        @hooks << Hook.new(klass: klass, method: m.name.to_s, doc_ja: ja, doc_en: en)
        next
      end
      @funcs << build_func(where, klass, m, ja, en)
    end
  end

  def build_func(where, klass, m, ja, en)
    abort "#{where}: one signature only (no overloads)" unless m.overloads.size == 1
    fn = m.overloads.first.method_type.type
    unless fn.optional_positionals.empty? && fn.rest_positionals.nil? && fn.trailing_positionals.empty? &&
           fn.required_keywords.empty? && fn.optional_keywords.empty? && fn.rest_keywords.nil?
      abort "#{where}: a boundary function takes required positional arguments only; " \
            "mark this one #{MARK_INTERNAL} and write its simple form as #{m.name}_raw"
    end
    abort "#{where}: a block is not a boundary argument" if m.overloads.first.method_type.block
    singleton = m.kind == :singleton
    prefix = short_name(klass)
    params = fn.required_positionals.map do |p|
      abort "#{where}: every argument needs a name" unless p.name
      Param.new(p.name.to_s, arg_type(p.type, klass, "#{where} (#{p.name})"))
    end
    names = params.map(&:name)
    abort "#{where}: an argument may not be called #{prefix}, out or out_size" if (names & [prefix, 'out', 'out_size']).any?
    Func.new(klass: klass, method: m.name.to_s, prefix: prefix,
             flat: "fmrb_bnd_#{prefix}_#{m.name}", api: "fmrb_#{prefix}_#{m.name}",
             import_name: "#{prefix}_#{m.name}",
             recv: singleton ? nil : prefix, params: params,
             ret: ret_type(fn.return_type, klass, where), doc_ja: ja, doc_en: en, singleton: singleton)
  end

  def lookup_alias(name, klass)
    key = [klass, name].compact.join('::')
    return [key, @aliases[key]] if @aliases.key?(key)
    return [name, @aliases[name]] if @aliases.key?(name)

    [nil, nil]
  end

  # The type of one value: an argument, a record field, an array element.
  def value_type(t, klass, where, in_record: false)
    case t
    when RBS::Types::Bases::Bool
      Ty.new(kind: :bool, ctype: in_record ? 'uint8_t' : 'int32_t')
    when RBS::Types::ClassInstance
      case t.name.to_s.delete_prefix('::')
      when 'Integer' then Ty.new(kind: :int, ctype: 'int32_t', min: -0x80000000, max: 0x7fffffff)
      when 'Float' then Ty.new(kind: :float, ctype: 'double')
      when 'String'
        abort "#{where}: a String inside a record has no fixed width" if in_record
        Ty.new(kind: :str)
      when 'Array'
        abort "#{where}: an array inside a record has no fixed width" if in_record
        elem_t = t.args.first or abort "#{where}: write the element type (Array[...])"
        elem = value_type(elem_t, klass, where, in_record: true)
        abort "#{where}: Array[#{elem_t}] is not handled (records or fixed-width integers)" unless %i[record int].include?(elem.kind)
        Ty.new(kind: :array, elem: elem)
      else
        abort "#{where}: type #{t} is not handled by the boundary rules"
      end
    when RBS::Types::Alias
      name = t.name.to_s.delete_prefix('::')
      if INT_WIDTHS.key?(name)
        c, lo, hi = INT_WIDTHS[name]
        return Ty.new(kind: :int, ctype: c, min: lo, max: hi)
      end
      return Ty.new(kind: :float, ctype: FLOAT_WIDTHS[name]) if FLOAT_WIDTHS.key?(name)
      if name == 'bin'
        abort "#{where}: bin inside a record has no fixed width" if in_record
        return Ty.new(kind: :bin)
      end
      key, target = lookup_alias(name, klass)
      abort "#{where}: type alias #{name} is not defined" unless target
      return Ty.new(kind: :record, record: record_of(key, target, klass, where)) if target.is_a?(RBS::Types::Record)

      value_type(target, klass, where, in_record: in_record)
    when RBS::Types::Union
      vals = t.types.map { |x| x.is_a?(RBS::Types::Literal) && x.literal.is_a?(Symbol) ? x.literal : nil }
      abort "#{where}: a union is handled only as a choice of Symbols (:a | :b)" if vals.include?(nil)
      Ty.new(kind: :choice, ctype: 'int32_t', values: vals.map(&:to_s))
    else
      abort "#{where}: type #{t} is not handled by the boundary rules"
    end
  end

  def record_of(key, rtype, klass, where)
    @records[key] ||= begin
      owner = key.include?('::') ? key.split('::').first : klass
      name = key.split('::').last
      if rtype.respond_to?(:optional_fields) && !rtype.optional_fields.empty?
        abort "#{where}: record #{name} has optional fields; a record has every field"
      end
      fields = rtype.fields.map do |fname, ft|
        ty = value_type(ft, owner, "#{where} (#{name}.#{fname})", in_record: true)
        abort "#{where}: record #{name}.#{fname}: #{ft} is not a fixed-width value" unless %i[int bool float].include?(ty.kind)
        Field.new(fname.to_s, ty)
      end
      Rec.new(name, owner, "fmrb_bnd_#{short_name(owner)}_#{name}_t", "fmrb_#{short_name(owner)}_#{name}_t", fields)
    end
  end

  def arg_type(t, klass, where)
    abort "#{where}: an argument cannot be nil (#{t}); use a value that means none" if t.is_a?(RBS::Types::Optional)
    value_type(t, klass, where)
  end

  def ret_type(t, klass, where)
    return :self if t.to_s == klass || t.is_a?(RBS::Types::Bases::Self) || t.is_a?(RBS::Types::Bases::Instance)
    return :void if t.is_a?(RBS::Types::Bases::Void) || t.is_a?(RBS::Types::Bases::Nil)

    optional = t.is_a?(RBS::Types::Optional)
    ty = value_type(optional ? t.type : t, klass, where)
    case ty.kind
    when :record then ty.optional = optional
    when :int, :bool, :choice, :str, :bin
      abort "#{where}: only a record may be returned as optional (#{t})" if optional
      abort "#{where}: an Integer result is int32 (#{t})" if ty.int? && ty.ctype != 'int32_t'
    else
      abort "#{where}: result type #{t} is not handled by the boundary rules"
    end
    ty
  end

  # Every flat function needs its C body in main/app/fmrb_boundary_*.c.
  def check_bodies
    src = Dir[BODY_GLOB].sort.map { |p| File.read(p) }.join("\n")
    missing = @funcs.reject { |f| src.match?(/^int32_t\s+#{f.flat}\s*\(/) }
    return if missing.empty?

    abort "boundary: no C body for #{missing.map(&:ruby_sig).join(', ')} in main/app/fmrb_boundary_*.c. " \
          "A method written in Ruby (or state the object keeps) is marked %a{#{MARK_INTERNAL}}."
  end

  # A _raw method is the simple form of a wrapper: the wrapper is in the same
  # class (and not on the boundary itself).
  def check_raw_names
    by_class = @funcs.group_by(&:klass)
    @funcs.select(&:raw?).each do |f|
      base = f.method.delete_suffix('_raw')
      if by_class[f.klass].any? { |g| g.method == base }
        abort "#{f.ruby_sig}: #{base} is on the boundary too; a _raw method stands for a wrapper " \
              "(mark #{base} #{MARK_INTERNAL})"
      end
    end
  end
end

# ---------------------------------------------------------------- output

# From what a C app sees (its names, not the firmware's own), so renaming the
# firmware's side leaves it as it is.
def abi_hash(table)
  lines = table.funcs.map { |f| [f.api, f.c_args(api: true)].join(':') }
  lines += table.records.values.map { |r| ([r.api_cname] + r.fields.map { |x| "#{x.ty.ctype} #{x.name}" }).join(':') }
  Digest::SHA256.hexdigest(lines.join("\n"))[0, 8]
end

def render(template, table)
  ERB.new(File.read(File.join(TEMPLATE_DIR, template)), trim_mode: '-')
     .result_with_hash(funcs: table.funcs, hooks: table.hooks, records: table.records.values,
                       abi_hash: abi_hash(table))
end

# Only a changed file is rewritten, so a build that regenerates nothing new
# recompiles nothing.
def write_if_changed(path, text)
  return false if File.exist?(path) && File.read(path) == text

  FileUtils.mkdir_p(File.dirname(path))
  File.write(path, text)
  true
end

# Every qstr the generated MicroPython module names must be in the committed
# table; otherwise rake micropython:gen has to run again and its output be
# committed (report/w4a.md 1.5).
def check_qstrs(table)
  return [] unless File.exist?(MP_QSTR_TABLE)

  have = File.read(MP_QSTR_TABLE).scan(/^QDEF\d?\(MP_QSTR_(\w+),/).flatten.to_h { |q| [q, true] }
  text = render('fmrb_boundary_mp.inc.erb', table)
  text.scan(/MP_QSTR_(\w+)/).flatten.uniq.reject { |q| have[q] }
end

table = Collector.new.run
if ARGV.include?('--check')
  stale = SDK_OUTPUTS.reject { |t, path| File.exist?(path) && File.read(path) == render(t, table) }
  unless stale.empty?
    abort "boundary SDK out of date with sig/: #{stale.values.map { |p| p.sub("#{ROOT}/", '') }.join(', ')}" \
          ' (run ruby tools/boundary/gen.rb and commit)'
  end
  missing = check_qstrs(table)
  unless missing.empty?
    abort "boundary: MicroPython's qstr table lacks #{missing.join(', ')} " \
          '(run rake micropython:gen and commit components/micropython/mp_embed)'
  end
  puts "boundary: SDK and MicroPython qstrs match sig/ (#{table.funcs.size} functions, " \
       "#{table.records.size} records, #{table.hooks.size} hooks)"
  exit
end

changed = OUTPUTS.merge(SDK_OUTPUTS).select { |t, path| write_if_changed(path, render(t, table)) }
puts "boundary: #{table.funcs.size} functions, #{table.records.size} records, " \
     "#{changed.empty? ? 'nothing changed' : "wrote #{changed.values.map { |p| p.sub("#{ROOT}/", '') }.join(', ')}"}"
