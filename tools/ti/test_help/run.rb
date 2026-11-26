#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Tests of the F1 help files (tools/ti/gen_help.rb) and of the editor's way of
# reading them (main/ruby/default_app/editor/ti_ui.rb), under host Ruby.
#
#   - The generator writes five files: ja.md, en.md, their tables ja.idx and
#     en.idx, and index.txt. Every class has a line in each table, the pages
#     lie end to end and cover the .md exactly, and each page is the class's
#     text as the generator made it (TiHelp.class_pages), in its language.
#   - The editor's half, the real ti_ui.rb over stand-ins: a name is looked up
#     in index.txt, the class's line in <lang>.idx, and the bytes it names are
#     what EditorCore.load_range would load. Checked for every class in both
#     languages, and for the cases F1 meets: an ASCII class, a class with ::
#     in its name, a method name, an operator, a name with no page.
#
#   ruby tools/ti/test_help/run.rb        (or: rake ti:help:test)

require "tmpdir"

ROOT = File.expand_path("../../..", __dir__)
require File.join(ROOT, "tools/ti/gen_help")

$failures = 0
$count = 0

def check(name, got, want)
  $count += 1
  return if got == want

  $failures += 1
  g = got.is_a?(String) && got.bytesize > 80 ? "#{got.byteslice(0, 80).inspect}..." : got.inspect
  w = want.is_a?(String) && want.bytesize > 80 ? "#{want.byteslice(0, 80).inspect}..." : want.inspect
  puts "FAIL #{name}: got #{g}, want #{w}"
end

SIG = File.join(ROOT, "sig")

Dir.mktmpdir("ti_help") do |out|
  # ---- the generator ---------------------------------------------------------

  written, entries = TiHelp.generate(sig_dir: SIG, out_dir: out)
  check("files written", written, 5)
  check("files on disk", Dir.children(out).sort, %w[en.idx en.md index.txt ja.idx ja.md])
  check("index entries", entries > 0, true)

  by_lang, = TiHelp.class_pages(sig_dir: SIG)
  classes = by_lang["ja"].map(&:first)
  check("same classes in both languages", by_lang["en"].map(&:first), classes)

  tables = {}
  %w[ja en].each do |lang|
    body = File.binread(File.join(out, "#{lang}.md"))
    rows = File.read(File.join(out, "#{lang}.idx")).lines.map { |l| l.chomp.split("\t") }
    tables[lang] = rows
    check("#{lang}.idx: one line per class", rows.map(&:first), classes)
    check("#{lang}.idx: three columns", rows.all? { |r| r.size == 3 }, true)

    pos = 0
    rows.each_with_index do |(cls, off, len), i|
      check("#{lang} #{cls}: pages end to end", off.to_i, pos)
      page = body.byteslice(off.to_i, len.to_i)
      check("#{lang} #{cls}: page text", page, by_lang[lang][i][1].b)
      check("#{lang} #{cls}: starts with its heading",
            page.start_with?("# #{cls.gsub("__", "::")}"), true)
      pos = off.to_i + len.to_i
    end
    check("#{lang}.md: pages cover the file", pos, body.bytesize)
  end
  check("the languages differ", File.binread(File.join(out, "ja.md")) ==
                                File.binread(File.join(out, "en.md")), false)

  index_classes = File.read(File.join(out, "index.txt")).lines.map { |l| l.chomp.split("\t")[1] }.uniq
  check("every class in index.txt has a page", (index_classes - classes), [])

  # ---- the editor's half -----------------------------------------------------

  # Stand-ins for what ti_ui.rb touches when it is loaded and when F1 reads.
  module EditorConst; end
  module FmrbGfx
    def self.rgb_to_332(_r, _g, _b) = 0
  end
  module FmrbI18n
    class << self
      attr_accessor :lang
    end
  end
  module Log
    def self.info(_msg) = nil
  end
  module EditorCore; end

  load File.join(ROOT, "main/ruby/default_app/editor/ti_ui.rb")
  EditorTiUi.send(:remove_const, :HELP_INDEX)
  EditorTiUi.const_set(:HELP_INDEX, File.join(out, "index.txt"))
  EditorTiUi.send(:remove_const, :HELP_DIR)
  EditorTiUi.const_set(:HELP_DIR, "#{out}/")

  editor = Class.new do
    include EditorTiUi
    attr_reader :loaded, :loaded_name

    # What file_run.rb's load_section hands to EditorCore.load_range: the
    # bytes of src from offset, length long.
    def load_section(src, offset, length, name)
      @loaded = File.binread(src).byteslice(offset, length)
      @loaded_name = name
      true
    end

    # No receiver on the line: the first class the index lists wins.
    def help_receiver_class = nil
  end.new

  page_of = ->(lang, cls) { by_lang[lang].find { |c, _| c == cls }&.last&.b }

  %w[ja en].each do |lang|
    FmrbI18n.lang = lang
    classes.each do |cls|
      sec = editor.help_section(cls)
      check("#{lang} #{cls}: found in the table", sec.nil?, false)
      next if sec.nil?

      check("#{lang} #{cls}: loads", editor.load_help_page(sec, editor.help_file_for(cls)), true)
      check("#{lang} #{cls}: loaded page", editor.loaded, page_of.(lang, cls))
    end

    # The way F1 gets there: a name to a class, the class to its page.
    [
      ["Array", "Array"],                        # an ASCII class name
      ["Asterism::Proxy", "Asterism__Proxy"],    # a class with ::, as the index spells it
    ].each do |shown, cls|
      sec = editor.help_section(cls)
      check("#{lang} #{shown}: section", sec.nil?, false)
      next if sec.nil?

      editor.load_help_page(sec, editor.help_file_for(cls))
      check("#{lang} #{shown}: page", editor.loaded, page_of.(lang, cls))
      check("#{lang} #{shown}: shown name", editor.loaded_name, "#{out}/#{cls}.#{lang}.md")
      check("#{lang} #{shown}: heading", editor.loaded.start_with?("# #{shown}"), true)
    end

    %w[draw_text read <<].each do |name|
      cls = editor.help_path_for(name)
      check("#{lang} #{name}: in the index", cls.nil?, false)
      next if cls.nil?

      editor.load_help_page(editor.help_section(cls), editor.help_file_for(cls))
      check("#{lang} #{name}: page of #{cls}", editor.loaded, page_of.(lang, cls))
      has = editor.loaded.include?("# #{cls.gsub("__", "::")}##{name}") ||
            editor.loaded.include?("# #{cls.gsub("__", "::")}.#{name}")
      check("#{lang} #{name}: the page has its section", has, true)
    end

    # A section heading names a nested class with "::" while the index has
    # "__": the jump takes the heading's form.
    check("#{lang} class name for headings", editor.help_class_name("Asterism__Proxy"), "Asterism::Proxy")
    check("#{lang} class name, plain", editor.help_class_name("Array"), "Array")

    # A method called index has its own section; the class's comment is not
    # lost to it (its page once took the same file name).
    string = page_of.(lang, "String").force_encoding("UTF-8")
    check("#{lang} String: class text first", string.start_with?("# String\n\n") &&
                                               !string.start_with?("# String\n\n# "), true)
    idx = string.index("# String#index\n")
    check("#{lang} String#index: has a section", idx.nil?, false)
    if idx
      sec = string[idx, 400].split("\n# ", 2).first
      check("#{lang} String#index: one language", sec.include?("<<en>>"), false)
    end

    check("#{lang} no such name", editor.help_path_for("no_such_method_anywhere"), nil)
    check("#{lang} no such class", editor.help_section("NoSuchClass"), nil)
    # A class whose name ends another one's is not taken for it.
    check("#{lang} suffix is not a class", editor.help_section("Proxy"), nil)
  end

  # The fields of a table line, as the editor walks them.
  check("field 0", editor.help_section_field("123\t45", 0), 123)
  check("field 1", editor.help_section_field("123\t45", 1), 45)
  check("field 1 missing", editor.help_section_field("123", 1), -1)
  check("field not a number", editor.help_section_field("x\t1", 0), -1)
end

puts "ti help: #{$count - $failures}/#{$count} passed"
exit($failures.zero? ? 0 : 1)
