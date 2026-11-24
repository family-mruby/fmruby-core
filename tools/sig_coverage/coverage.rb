#!/usr/bin/env ruby
# frozen_string_literal: true

# sig coverage (doc/sig_coverage S0): what sig/*.rbs is missing, counted the
# same way every time, so the order of the work and "is this range done"
# both come from one number.
#
#   1. used by the apps but not in sig/ (flash/app without flash/app/test,
#      flash/lib/templates, flash/usr/share/samples; app_scan.rb)
#   2. implemented but not in sig/ (mrblib of the picoruby-fmrb-* gems and
#      Asterism, the C definitions of lib/gems, and the upstream gems of the
#      classes an app sees: Machine, Math, GC, Dir, File, IO, Time, JSON,
#      MessagePack, I2C, GPIO; impl_scan.rb)
#   3. doc gaps in Family mruby's own signatures (fmrb_*.rbs, enumerable.rbs,
#      gpio.rbs): no summary, a summary over 111 bytes, no <<en>>, no long
#      form, no ```ruby example
#
# ignore.yml lists what is left out on purpose (internal, with a reason);
# it is removed from all three counts. baseline.json holds the last
# accepted result; every run prints the difference from it.
#
#   ruby tools/sig_coverage/coverage.rb                 # summary + tmp/sig_coverage/report.md
#   ruby tools/sig_coverage/coverage.rb --update        # also rewrite baseline.json
#   ruby tools/sig_coverage/coverage.rb --root DIR      # another tree (the tool's tests)
#
# Report only: the exit status is 0 whatever the counts (decision 3).

require "fileutils"
require "json"
require "optparse"
require "set"
require "yaml"
require_relative "sig_index"
require_relative "app_scan"
require_relative "impl_scan"

module SigCoverage
  # The editor copies a summary into a 112-byte box (ET_DOC_MAX) with its
  # terminating NUL, so 111 bytes of text are what it shows.
  SUMMARY_MAX = 111

  # Where things are, relative to the root. The tool's tests build a small
  # tree with the same shape.
  class Layout
    PICORUBY = "components/picoruby-esp32/picoruby/mrbgems"
    MRUBY = "#{PICORUBY}/picoruby-mruby/lib/mruby"

    # Upstream classes an app sees (instruction 2b).
    UPSTREAM_CLASSES = %w[Machine Math GC Dir File IO Time JSON MessagePack I2C GPIO].freeze

    attr_reader :root

    def initialize(root)
      @root = File.expand_path(root)
    end

    def path(rel) = File.join(@root, rel)

    def glob(*patterns) = patterns.flat_map { |p| Dir[path(p)] }.uniq.sort

    def sig_dir = path("sig")

    def app_files
      glob("flash/app/**/*.rb", "flash/lib/templates/**/*.rb", "flash/usr/share/samples/**/*.rb")
        .reject { |f| f.include?("/flash/app/test/") }
    end

    # Library code the apps load: what it defines is the apps' own.
    def support_files
      glob("flash/lib/*.rb", "flash/usr/share/**/*.rb") - app_files
    end

    def ruby_impl_files
      glob("lib/gems/picoruby-fmrb-*/mrblib/**/*.rb", "vendor/asterism/mrblib/**/*.rb")
    end

    def upstream_ruby_files
      glob("#{PICORUBY}/picoruby-machine/mrblib/*.rb", "#{PICORUBY}/picoruby-json/mrblib/*.rb",
           "#{PICORUBY}/picoruby-gpio/mrblib/*.rb", "#{PICORUBY}/picoruby-i2c/mrblib/*.rb",
           "#{MRUBY}/mrbgems/mruby-time/mrblib/*.rb", "#{MRUBY}/mrbgems/mruby-math/mrblib/*.rb",
           "#{MRUBY}/mrbgems/mruby-dir/mrblib/*.rb")
    end

    def c_impl_files
      glob("lib/gems/*/ports/**/*.c", "lib/gems/*/src/**/*.c")
    end

    def upstream_c_files
      glob("#{PICORUBY}/picoruby-machine/src/**/*.c", "#{PICORUBY}/picoruby-gpio/src/**/*.c",
           "#{PICORUBY}/picoruby-i2c/src/**/*.c", "#{MRUBY}/mrbgems/mruby-math/src/*.c",
           "#{MRUBY}/mrbgems/mruby-time/src/*.c", "#{MRUBY}/mrbgems/mruby-dir/src/*.c",
           "#{MRUBY}/src/gc.c")
    end

    # Which of the trees above are there (vendor/ and the submodule are
    # fetched, not committed).
    def missing_trees
      [["vendor/asterism/mrblib", "rake asterism:setup"],
       [PICORUBY, "git submodule update --init"]].reject { |dir, _| Dir.exist?(path(dir)) }
    end

    # The fifteen files copied from picoruby-ti's example/rbs. Their own
    # declarations carry English-only one-line summaries, so the doc checks
    # (count 3) leave them out; every other file in sig/ is Family mruby's.
    UPSTREAM_SIG = %w[array class false_class float hash integer kernel nil_class object proc
                      range string symbol true_class untyped].freeze

    def ours?(sig_file) = !UPSTREAM_SIG.include?(File.basename(sig_file, ".rbs"))
  end

  # tools/sig_coverage/ignore.yml: a list of { name:, reason: } or
  # { file:, reason: }. A name is a class ("Recorder", "GfxBlock::Recorder")
  # -- that class and its methods, not the classes nested in it -- or one
  # method ("FmrbApp#wheel_rows", "FmrbMidi.device"), or "#name" for a name
  # called on an unknown receiver or without one (count 1 only). A file
  # (relative to the root) is left out of the apps that count 1 reads.
  class Ignore
    Entry = Struct.new(:name, :file, :reason, :hits, keyword_init: true)

    attr_reader :entries

    def initialize(path)
      list = File.exist?(path) ? (YAML.safe_load_file(path) || []) : []
      raise "#{path}: expected a list" unless list.is_a?(Array)

      @entries = list.map do |item|
        name = item["name"].to_s
        file = item["file"].to_s
        reason = item["reason"].to_s
        if (name.empty? && file.empty?) || reason.empty?
          raise "#{path}: entry without a name (or file) or a reason: #{item.inspect}"
        end

        Entry.new(name: name.empty? ? nil : name, file: file.empty? ? nil : file, reason:, hits: 0)
      end
      @by_name = @entries.select(&:name).to_h { |e| [e.name, e] }
      @by_file = @entries.select(&:file).to_h { |e| [e.file, e] }
    end

    # Is the app file (relative to the root) left out?
    def file?(rel)
      entry = @by_file[rel]
      entry.hits += 1 if entry
      !entry.nil?
    end

    # Is the class, or the method key ("A#m" / "A.m"), ignored?
    def ignored?(key)
      klass = key.split(/[#.](?=[^:]*\z)/).first
      entry = @by_name[key] || @by_name[klass]
      entry.hits += 1 if entry
      !entry.nil?
    end

    def unused = @entries.select { |e| e.hits.zero? }
  end

  # A gap and where it was seen.
  class Tally
    attr_reader :items

    def initialize
      @items = Hash.new { |h, k| h[k] = { count: 0, files: Hash.new(0) } }
    end

    def add(key, file)
      @items[key][:count] += 1
      @items[key][:files][file] += 1 if file
    end

    def keys = @items.keys.sort
    def size = @items.size
    def uses = @items.values.sum { |v| v[:count] }

    def sorted = @items.sort_by { |k, v| [-v[:count], k] }

    def files_of(key, limit = 3)
      @items[key][:files].sort_by { |f, n| [-n, f] }.first(limit).map(&:first)
    end
  end

  class Analysis
    attr_reader :layout, :sig, :apps, :ignore, :result, :relocated, :ignored_files

    def initialize(layout, ignore_path:)
      @layout = layout
      @ignore = Ignore.new(ignore_path)
      @sig = SigIndex.from_dir(layout.sig_dir)
      @ignored = Hash.new(0)
      @ignored_files = 0
      @result = {}
      count_apps
      count_impl
      count_docs
    end

    def ignored_counts = @ignored

    # Every gap by category, sorted: what baseline.json keeps.
    def items
      {
        "app_class" => @result[:app_class].keys,
        "app_constant" => @result[:app_constant].keys,
        "app_method" => @result[:app_method].keys,
        "app_name" => @result[:app_name].keys,
        "app_bare" => @result[:app_bare].keys,
        "impl_ruby" => @result[:impl_ruby].map(&:key).uniq.sort,
        "impl_c" => @result[:impl_c].map(&:key).uniq.sort,
        "doc_no_summary" => @result[:doc][:no_summary].sort,
        "doc_long_summary" => @result[:doc][:long_summary].sort,
        "doc_no_en" => @result[:doc][:no_en].sort,
        "doc_no_long" => @result[:doc][:no_long].sort,
        "doc_no_example" => @result[:doc][:no_example].sort,
        "doc_bold_across" => @result[:doc][:bold_across].sort,
      }
    end

    private

    def skip?(section, key)
      return false unless @ignore.ignored?(key)

      @ignored[section] += 1
      true
    end

    # ---- 1. used by the apps --------------------------------------------

    def count_apps
      files = @layout.app_files.reject do |f|
        @ignore.file?(f.delete_prefix("#{@layout.root}/")).tap { |left_out| @ignored_files += 1 if left_out }
      end
      @apps = AppScan.new(app_files: files, support_files: @layout.support_files,
                          sig: @sig, root: @layout.root)
      r = @result
      r[:app_class] = Tally.new     # constants with no class in sig/ (and not the apps')
      r[:app_constant] = Tally.new  # A::FOO where A is in sig/ and FOO is not
      r[:app_method] = Tally.new    # Machine.board_millis, FmrbGfx#draw_text_mixed
      r[:app_name] = Tally.new      # .byteslice on a receiver of unknown type
      r[:app_bare] = Tally.new      # random_int(6)
      r[:app_kind] = Tally.new      # called on the class, declared on the instance
      r[:app_support] = Tally.new   # classes of the libraries the apps load (not gaps)
      r[:app_known] = Hash.new(0)   # uses that sig/ covers, by kind

      @apps.uses.each do |use|
        case use.kind
        when :const then const_call(use)
        when :ivar then typed_call(use)
        when :untyped, :bare then name_call(use)
        when :ref then const_ref(use)
        end
      end
    end

    def resolve(name)
      return :sig_class if @sig.class?(name)
      return :sig_const if @sig.constants.key?(name)
      # A bare constant inside an app class may be its superclass's
      # (TRANSPARENT_COLOR in a FmrbApp); the tool does not follow the
      # lexical scope, so any class's constant of that name counts.
      return :sig_const if !name.include?("::") && sig_constant_leaves.key?(name)
      return :app_class if app_defined?(@apps.defs[:classes], name)
      return :app_const if app_defined?(@apps.defs[:constants], name)

      :missing
    end

    # A class from a library the apps load (flash/usr/share/transfer ...):
    # not a gap by the rules, listed for the decision whether sig/ should
    # carry it.
    # Net::HTTP.get_response is a class; Math::PI / 2 and BOARD.to_s are values.
    # A leaf without an underscore is a value only when what is called is an
    # operator or a conversion.
    def value_call?(use)
      use.receiver.split("::").last.include?("_") || !use.name.match?(/\A[a-z_]/) ||
        %w[to_s to_i to_f to_sym inspect freeze].include?(use.name)
    end
    
    def note_support(name, file)
      @result[:app_support].add(name, file) if @apps.class_origin(name) == :support
    end

    def sig_constant_leaves
      @sig_constant_leaves ||= @sig.constants.keys.to_h { |k| [k.split("::").last, true] }
    end

    def app_defined?(table, name)
      return table.key?(name) unless name.include?("::")

      table.key?(name) || table.each_key.any? { |k| k.end_with?("::#{name}") }
    end

    def const_call(use)
      case resolve(use.receiver)
      when :sig_class
        key = "#{use.receiver}.#{use.name}"
        if @sig.static_method(use.receiver, use.name) || @apps.app_method?(use.name)
          @result[:app_known][:const] += 1
        elsif skip?(1, key)
          nil
        elsif @sig.instance_method(use.receiver, use.name)
          @result[:app_kind].add(key, use.file)
        else
          @result[:app_method].add(key, use.file)
        end
      when :app_class
        note_support(use.receiver, use.file)
      when :sig_const, :app_const
        name_call(Use.new(**use.to_h, kind: :untyped))
      else
        # Math::PI / 2, FmrbConst::PLATFORM == "esp32": a value constant the
        # signatures do not have; the call is on its value.
        if value_like?(use.receiver) && value_call?(use)
          const_ref(use)
          return name_call(Use.new(**use.to_h, kind: :untyped))
        end
        return if skip?(1, use.receiver)

        @result[:app_class].add(use.receiver, use.file)
        @result[:app_method].add("#{use.receiver}.#{use.name}", use.file)
      end
    end

    def typed_call(use)
      key = "#{use.receiver}##{use.name}"
      if @sig.instance_method(use.receiver, use.name) || @apps.app_method?(use.name)
        @result[:app_known][:ivar] += 1
      elsif !skip?(1, key)
        @result[:app_method].add(key, use.file)
      end
    end

    def name_call(use)
      if @sig.method_names.key?(use.name) || @apps.app_method?(use.name)
        @result[:app_known][use.kind] += 1
      elsif skip?(1, "##{use.name}")
        nil
      else
        @result[use.kind == :bare ? :app_bare : :app_name].add(use.name, use.file)
      end
    end

    def const_ref(use)
      name = use.receiver
      kind = resolve(name)
      note_support(name, use.file) if kind == :app_class
      return @result[:app_known][:ref] += 1 unless kind == :missing

      parent = name.split("::")[0..-2].join("::")
      if !value_like?(name)
        @result[:app_class].add(name, use.file) unless skip?(1, name)
      elsif parent.empty? || resolve(parent) != :missing
        @result[:app_constant].add(name, use.file) unless skip?(1, name)
      elsif !skip?(1, parent)
        # FmrbConst::KEY_UP with no FmrbConst in sig/: the class is the gap,
        # and the constant is listed under it.
        @result[:app_class].add(parent, use.file)
        @result[:app_constant].add(name, use.file)
      end
    end

    # Written like a value, not a class: upper case with an underscore
    # (KEY_UP, TRANSPARENT_COLOR), or upper case without digits under another
    # constant (Math::PI, FmrbConst::BOARD). Upper-case class names stay
    # classes: I2C, GPIO, RNG at the top, Digest::SHA256 with its digits.
    def value_like?(name)
      parts = name.split("::")
      leaf = parts.last
      return false unless leaf.match?(/\A[A-Z][A-Z0-9_]*\z/)

      leaf.include?("_") || (parts.size > 1 && !leaf.match?(/\d/))
    end

    # ---- 2. implemented ------------------------------------------------

    def count_impl
      @relocated = {}
      ruby = RubyImplScan.new(@layout.ruby_impl_files, root: @layout.root)
      upstream_ruby = RubyImplScan.new(@layout.upstream_ruby_files, root: @layout.root)
      upstream_set = Layout::UPSTREAM_CLASSES
      upstream_methods = upstream_ruby.methods.select do |m|
        upstream_set.any? { |c| m.class_name == c || m.class_name.start_with?("#{c}::") }
      end
      c = CImplScan.new(@layout.c_impl_files, root: @layout.root)
      upstream_c = CImplScan.new(@layout.upstream_c_files, root: @layout.root, classes: upstream_set)

      @result[:impl_ruby_all] = dedupe(ruby.methods + upstream_methods)
      @result[:impl_c_all] = dedupe(c.methods + upstream_c.methods)
      @result[:impl_unresolved] = c.unresolved + upstream_c.unresolved
      @result[:impl_errors] = ruby.errors + upstream_ruby.errors
      @result[:impl_toplevel] = ruby.toplevel
      @api_private_out = []
      @result[:impl_ruby], @result[:impl_ruby_kind] = missing_impl(@result[:impl_ruby_all])
      @result[:impl_c], @result[:impl_c_kind] = missing_impl(@result[:impl_c_all])
      @result[:impl_api_private] = @api_private_out
    end


    def dedupe(methods)
      methods.group_by(&:key).map { |_, list| list.first }
    end

    # The sig/ class an implementation class is written as: the same name,
    # or the one class in sig/ with the same last part (MmlPlayer in
    # FmrbMidi::MmlPlayer). Noted, since the editor would not find it.
    def sig_class_for(name)
      return name if @sig.class?(name)

      candidates = @sig.classes.keys.select do |k|
        k.end_with?("::#{name}") || name.end_with?("::#{k}")
      end
      return nil unless candidates.size == 1

      @relocated[name] = candidates.first
      candidates.first
    end

    def missing_impl(methods)
      missing = []
      kind = []
      methods.each do |m|
        target = sig_class_for(m.class_name)
        names = [m.class_name, target].compact.uniq
        found = names.any? { |n| sig_has?(n, m.name, m.singleton) }
        next if found
        next if skip?(2, m.key) || (target && target != m.class_name && skip?(2, m.key.sub(m.class_name, target)))
        # A method its own gem marks `@api private` is left out without a
        # line in ignore.yml (doc/sig_coverage, decision 9): the mark is the
        # gem's, and a list of 150 names would only drift from it.
        if m.api_private
          @api_private_out << m
          next
        end

        if names.any? { |n| sig_has?(n, m.name, !m.singleton) }
          kind << m
        else
          missing << m
        end
      end
      [missing, kind]
    end

    # Kernel's methods are written on Object in sig/ (object.rbs).
    def sig_has?(class_name, name, singleton)
      classes = class_name == "Kernel" ? %w[Kernel Object] : [class_name]
      classes.any? do |c|
        singleton ? @sig.static_method(c, name) : @sig.instance_method(c, name)
      end
    end

    # ---- 3. doc gaps ---------------------------------------------------

    def count_docs
      doc = { methods: 0, no_summary: [], long_summary: [], no_en: [], no_long: [],
              no_example: [], no_long_en: [], bold_across: [] }
      @sig.classes.each_value do |klass|
        @sig.own_methods(klass.name).each do |m|
          next if m.alias_of || !@layout.ours?(m.file)
          next if skip?(3, m.key)

          doc[:methods] += 1
          lines = m.comment.lines
          summary = lines.first.to_s.strip
          long = lines.drop(1).join.strip
          doc[:no_summary] << m.key if summary.empty?
          doc[:long_summary] << m.key if summary.bytesize > SUMMARY_MAX
          doc[:no_en] << m.key unless summary.include?("<<en>>")
          doc[:no_long] << m.key if long.empty?
          doc[:no_example] << m.key unless long.include?("```ruby")
          # Prose (not only an example) with no <<en>> line: only the Japanese
          # half has a long form. Informational; not one of the five counts.
          prose = long.gsub(/^```.*?^```/m, "").strip
          doc[:no_long_en] << m.key if !prose.empty? && !long.match?(/^<<en>>\s*$/)
        end
      end
      doc[:files] = @sig.files.count { |f| @layout.ours?(f) }
      # A ** opened on one comment line and closed on another: the F1 help
      # colours by the line, so the emphasis bleeds (sig/README.md). Looked
      # for in the files themselves, class comments included.
      doc[:bold_across] = []
      @sig.files.select { |f| @layout.ours?(f) }.sort.each do |f|
        fence = false
        File.readlines(f, chomp: true).each_with_index do |line, i|
          text = line.strip
          next unless text.start_with?("#")
          fence = !fence if text.match?(/\A#\s*```/)
          next if fence
          # not the ** of an inline code span (a glob written as `**`)
          prose = text.gsub(/`[^`]*`/, "")
          doc[:bold_across] << "#{File.basename(f)}:#{i + 1}" if prose.scan("**").size.odd?
        end
      end
      @result[:doc] = doc
    end
  end

  # The summary on stdout and the details in tmp/sig_coverage/report.md.
  class Report
    def initialize(analysis, baseline: nil)
      @a = analysis
      @r = analysis.result
      @baseline = baseline
    end

    def diff
      return nil unless @baseline

      old = @baseline["items"] || {}
      @a.items.to_h do |category, keys|
        before = old.fetch(category, [])
        [category, { added: keys - before, removed: before - keys }]
      end
    end

    def counts
      r = @r
      doc = r[:doc]
      {
        "app_files" => @a.apps.files.size,
        "sig_classes" => @a.sig.classes.size,
        "sig_methods" => @a.sig.all_methods.size,
        "app_class" => r[:app_class].size, "app_class_uses" => r[:app_class].uses,
        "app_constant" => r[:app_constant].size,
        "app_method" => r[:app_method].size, "app_method_uses" => r[:app_method].uses,
        "app_name" => r[:app_name].size, "app_name_uses" => r[:app_name].uses,
        "app_bare" => r[:app_bare].size, "app_bare_uses" => r[:app_bare].uses,
        "impl_ruby" => r[:impl_ruby_all].size, "impl_ruby_missing" => r[:impl_ruby].size,
        "impl_ruby_api_private" => r[:impl_api_private].size,
        "impl_c" => r[:impl_c_all].size, "impl_c_missing" => r[:impl_c].size,
        "impl_c_unresolved" => r[:impl_unresolved].size,
        "doc_methods" => doc[:methods], "doc_no_summary" => doc[:no_summary].size,
        "doc_long_summary" => doc[:long_summary].size, "doc_no_en" => doc[:no_en].size,
        "doc_no_long" => doc[:no_long].size, "doc_no_example" => doc[:no_example].size,
        "doc_bold_across" => doc[:bold_across].size,
      }
    end

    def summary(report_path)
      c = counts
      r = @r
      doc = r[:doc]
      ig = @a.ignored_counts
      out = []
      out << "sig coverage: #{c['app_files']} app files; sig/ #{c['sig_classes']} classes, #{c['sig_methods']} methods"
      out << "1. used by apps, not in sig/"
      out << "   classes #{c['app_class']} (#{c['app_class_uses']} uses), constants #{c['app_constant']}, " \
             "methods on a known receiver #{c['app_method']} (#{c['app_method_uses']} uses)"
      out << "   names on an untyped receiver #{c['app_name']} (#{c['app_name_uses']} uses), " \
             "without a receiver #{c['app_bare']} (#{c['app_bare_uses']} uses)"
      out << "   top: #{top(r[:app_method], 6)}"
      out << "   top names: #{top(r[:app_name], 6)}"
      out << "2. implemented, not in sig/"
      out << "   Ruby #{c['impl_ruby_missing']} of #{c['impl_ruby']} methods in #{classes_of(r[:impl_ruby])} classes; " \
             "C #{c['impl_c_missing']} of #{c['impl_c']} in #{classes_of(r[:impl_c])} classes" \
             " (#{c['impl_c_unresolved']} targets unresolved)"
      out << "   left out, not counted: #{c['impl_ruby_api_private']} marked @api private in their gem"
      out << "   most: #{top_classes(r[:impl_ruby] + r[:impl_c], 6)}"
      out << "3. doc gaps in Family mruby's sig/ (#{doc[:files]} files, #{doc[:methods]} methods)"
      out << "   no summary #{c['doc_no_summary']}, summary > #{SUMMARY_MAX} bytes #{c['doc_long_summary']}, " \
             "no <<en>> #{c['doc_no_en']}, no long form #{c['doc_no_long']}, no example #{c['doc_no_example']}, " \
             "** across lines #{c['doc_bold_across']}"
      out << "ignored (ignore.yml, #{@a.ignore.entries.size} entries): 1: #{ig[1]} + #{@a.ignored_files} files, " \
             "2: #{ig[2]}, 3: #{ig[3]}"
      if (d = diff)
        changed = d.select { |_, v| !v[:added].empty? || !v[:removed].empty? }
        if changed.empty?
          out << "vs baseline: no change"
        else
          out << "vs baseline: " + changed.map { |k, v| "#{k} +#{v[:added].size}/-#{v[:removed].size}" }.join(", ")
          changed.first(3).each do |k, v|
            out << "   #{k} new: #{v[:added].first(4).join(' ')}" unless v[:added].empty?
          end
        end
      else
        out << "vs baseline: none recorded (rake sig:coverage:update)"
      end
      @a.layout.missing_trees.each { |dir, cmd| out << "warning: #{dir} is missing (#{cmd}); its counts are 0" }
      out << "details: #{report_path}"
      out.join("\n")
    end

    def details
      r = @r
      md = []
      md << "# sig coverage (generated by tools/sig_coverage, not committed)"
      md << ""
      md << "```"
      md << summary("(this file)")
      md << "```"
      md << ""
      md << "## 1. Used by the apps, not in sig/"
      md << ""
      md << "### Classes (constants that are neither in sig/ nor the apps' own)"
      md << table(r[:app_class])
      md << "### Methods on a known receiver (a class in sig/, a missing class, or @gfx)"
      md << table(r[:app_method])
      md << "### Constants under a class in sig/, or top-level"
      md << table(r[:app_constant])
      md << "### Names on a receiver of unknown type that no class in sig/ has"
      md << table(r[:app_name])
      md << "### Names called without a receiver that no class in sig/ has"
      md << table(r[:app_bare])
      md << "### Called on the class, declared on the instance (or the other way)"
      md << table(r[:app_kind])
      md << "### Classes from the libraries the apps load (flash/lib, flash/usr/share; not counted)"
      md << table(r[:app_support])
      md << "### Dynamic calls (the target is not visible to the tool)"
      md << ""
      dyn = @a.apps.dynamic.sort_by { |k, v| [-v.values.sum, k] }
      md << "| name | uses | files |"
      md << "|---|---|---|"
      dyn.each { |k, v| md << "| `#{k}` | #{v.values.sum} | #{v.keys.sort.first(3).join(', ')} |" }
      md << ""
      unless @a.apps.errors.empty?
        md << "Parse errors (Prism, scanned anyway): #{@a.apps.errors.size}"
        @a.apps.errors.each { |e| md << "- #{e}" }
        md << ""
      end
      md << "## 2. Implemented, not in sig/"
      md << ""
      md << "### (a) Ruby (mrblib)"
      md << ""
      md << "Methods their gem marks `@api private` are left out of this table and of the counts " \
            "(#{r[:impl_api_private].size} of them)."
      md << impl_table(r[:impl_ruby_all], r[:impl_ruby], r[:impl_ruby_kind])
      md << "### (b) C (mrb_define_*)"
      md << impl_table(r[:impl_c_all], r[:impl_c], r[:impl_c_kind])
      unless @a.relocated.empty?
        md << "### Implementation classes found in sig/ under another namespace"
        md << ""
        @a.relocated.sort.each { |impl, sig| md << "- `#{impl}` is written as `#{sig}`" }
        md << ""
      end
      unless r[:impl_unresolved].empty?
        md << "### C definitions whose class the tool could not follow"
        md << ""
        r[:impl_unresolved].each { |u| md << "- `#{u}`" }
        md << ""
      end
      unless r[:impl_toplevel].empty?
        md << "### Top-level defs in mrblib (private methods of Object; not counted)"
        md << ""
        r[:impl_toplevel].each { |u| md << "- `#{u}`" }
        md << ""
      end
      md << "## 3. Doc gaps in Family mruby's sig/"
      md << ""
      doc = r[:doc]
      { no_summary: "No summary", long_summary: "Summary over #{SUMMARY_MAX} bytes",
        no_en: "No <<en>> in the summary", no_long: "No long form",
        no_example: "No ```ruby example", bold_across: "A ** that is not closed on its line (file:line)", no_long_en: "Long form with prose but no <<en>> line (not in the counts)" }
        .each do |k, title|
          md << "### #{title} (#{doc[k].size})"
          md << ""
          by_file = doc[k].group_by { |key| sig_file_of(key) }
          by_file.sort.each { |f, keys| md << "- #{f} (#{keys.size}): #{keys.sort.map { "`#{_1}`" }.join(' ')}" }
          md << ""
        end
      md << "## Ignored on purpose (tools/sig_coverage/ignore.yml)"
      md << ""
      @a.ignore.entries.each { |e| md << "- `#{e.name || e.file}` (#{e.hits} hits): #{e.reason}" }
      md << ""
      if (d = diff)
        md << "## Against baseline.json"
        md << ""
        d.each do |k, v|
          next if v[:added].empty? && v[:removed].empty?

          md << "- #{k}: new #{v[:added].map { "`#{_1}`" }.join(' ')}; gone #{v[:removed].map { "`#{_1}`" }.join(' ')}"
        end
        md << ""
      end
      md.join("\n")
    end

    private

    def top(tally, n)
      tally.sorted.first(n).map { |k, v| "#{k} #{v[:count]}" }.join(", ")
    end

    def classes_of(methods) = methods.map(&:class_name).uniq.size

    def top_classes(methods, n)
      methods.group_by(&:class_name).sort_by { |k, v| [-v.size, k] }.first(n)
             .map { |k, v| "#{k} #{v.size}" }.join(", ")
    end

    def table(tally)
      rows = ["", "| | uses | files (up to 3) |", "|---|---|---|"]
      tally.sorted.each do |key, v|
        rows << "| `#{key.gsub('|', '\\|')}` | #{v[:count]} | #{tally.files_of(key).join(', ')} |"
      end
      rows << "| (none) | | |" if tally.size.zero?
      rows << ""
      rows.join("\n")
    end

    def impl_table(all, missing, kind)
      rows = ["", "| class | implemented | in sig/ | missing | names missing | kind differs |",
              "|---|---|---|---|---|---|"]
      by_class = all.group_by(&:class_name)
      gaps = missing.group_by(&:class_name)
      kinds = kind.group_by(&:class_name)
      by_class.keys.sort_by { |k| [-(gaps[k]&.size || 0), k] }.each do |k|
        miss = gaps[k] || []
        kd = kinds[k] || []
        next if miss.empty? && kd.empty?

        have = by_class[k].size - miss.size - kd.size
        names = miss.map { |m| "#{m.singleton ? '.' : ''}#{m.name}#{m.api_private ? '*' : ''}" }.sort.join(" ")
        rows << "| `#{k}` | #{by_class[k].size} | #{have} | #{miss.size} | #{names.gsub('|', '\\|')} | " \
                "#{kd.map { |m| m.singleton ? ".#{m.name}" : m.name }.sort.join(' ').gsub('|', '\\|')} |"
      end
      rows << ""
      rows.join("\n")
    end

    def sig_file_of(key)
      klass, name = key.split(/[#.](?=[^:]*\z)/)
      singleton = key.include?(".#{name}") && !key.end_with?("##{name}")
      sig = @a.sig
      m = singleton ? sig.classes[klass]&.static_methods&.[](name) : sig.classes[klass]&.instance_methods&.[](name)
      m ? File.basename(m.file) : "?"
    end
  end

  def self.main(argv)
    here = __dir__
    options = { root: File.expand_path("../..", here), update: false,
                ignore: File.join(here, "ignore.yml"), baseline: File.join(here, "baseline.json"),
                report: nil }
    OptionParser.new do |o|
      o.banner = "usage: coverage.rb [--update] [--root DIR] [--report FILE]"
      o.on("--update", "rewrite the baseline with this result") { options[:update] = true }
      o.on("--root DIR", "the fmruby-core tree to read") { |v| options[:root] = File.expand_path(v) }
      o.on("--ignore FILE", "the list of what is left out on purpose") { |v| options[:ignore] = v }
      o.on("--baseline FILE", "the recorded result to compare with") { |v| options[:baseline] = v }
      o.on("--report FILE", "where the details go (default ROOT/tmp/sig_coverage/report.md)") do |v|
        options[:report] = v
      end
    end.parse!(argv)

    layout = Layout.new(options[:root])
    analysis = Analysis.new(layout, ignore_path: options[:ignore])
    unless analysis.sig.errors.empty?
      warn analysis.sig.errors.join("\n")
      return 1
    end
    baseline = File.exist?(options[:baseline]) ? JSON.parse(File.read(options[:baseline])) : nil
    report = Report.new(analysis, baseline:)
    report_path = options[:report] || layout.path("tmp/sig_coverage/report.md")
    FileUtils.mkdir_p(File.dirname(report_path))
    File.write(report_path, report.details + "\n")
    puts report.summary(report_path.delete_prefix("#{layout.root}/"))
    unused = analysis.ignore.unused
    puts "ignore.yml entries that match nothing: #{unused.map { |e| e.name || e.file }.join(' ')}" unless unused.empty?
    if options[:update]
      data = { "counts" => report.counts, "items" => analysis.items }
      File.write(options[:baseline], JSON.pretty_generate(data) + "\n")
      puts "baseline written: #{options[:baseline].delete_prefix("#{layout.root}/")}"
    end
    0
  end
end

if $PROGRAM_NAME == __FILE__
  exit SigCoverage.main(ARGV)
end
