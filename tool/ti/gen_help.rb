#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Turn the long half of the doc comments in sig/ into the help files the
# editor opens with F1.
#
# The convention (sig/README.md): the first line of a doc comment is the
# summary, and it is the only line the type database keeps -- it has to fit a
# status line. Everything below it is the long form: an explanation, and an
# example. That part has no reason to sit in flash as a string table, so it is
# written out as ordinary files under flash/help instead:
#
#   flash/help/<Class>/<method>.md   the signature, then the long text
#   flash/help/<Class>/index.md      a class comment, when it has one
#   flash/help/index.txt             "<name>\t<path>" per entry, for lookups
#
# Generated at build time (rake setup) and not committed, like the type
# database itself.
#
# The WebConsole shows the same pages in the browser (P7), where a directory of
# files is awkward to fetch one by one -- so --json writes the identical set as
# a single object instead: {"pages": {path: markdown}, "index": [[name, path]]}.
# Same input, same text, one request.
#
# usage: gen_help.rb --sig-dir sig --out flash/help
#        gen_help.rb --sig-dir sig --json tool/web/wasm/help.json

require "optparse"
require "fileutils"
require "json"
require "rbs"

module TiHelp
  # A method name is not always a file name: walkable? and idle_gc= have to
  # land somewhere sane, and operators (+, <<) have no readable spelling at
  # all -- those get left out, since nobody writes a long help for them.
  def self.file_name_for(method_name)
    name = method_name.to_s
    return nil unless name.match?(/\A[A-Za-z_][A-Za-z0-9_]*[?!=]?\z/)

    name.sub(/\?\z/, "_p").sub(/!\z/, "_bang").sub(/=\z/, "_set")
  end

  def self.long_form(comment)
    return nil if comment.nil?

    lines = comment.string.to_s.lines
    return nil if lines.size < 2

    body = lines[1..].join.strip
    body.empty? ? nil : body
  end

  def self.summary(comment)
    comment&.string.to_s.lines.first.to_s.strip
  end

  # "draw_text: (Integer x, ...) -> FmrbGfx", the same shape the editor shows.
  def self.render_signature(name, member)
    types = member.overloads.map { |overload| overload.method_type.to_s }
    "#{name}#{types.first}"
  end

  def self.class_name_of(declaration, namespace)
    full = "#{namespace}#{declaration.name.relative!}"
    full.sub(/\A::/, "")
  end

  def self.each_class(declarations, namespace = "", &block)
    declarations.each do |declaration|
      case declaration
      when RBS::AST::Declarations::Class, RBS::AST::Declarations::Module
        name = class_name_of(declaration, namespace)
        block.call(name, declaration)
        each_class(declaration.members, "#{name}::", &block)
      end
    end
  end

  # Read the signatures and render every page that has a long form. Returns
  # the pages keyed by their relative path, and the index in the order they
  # were read (name first, so a name in two classes lists both).
  def self.build(sig_dir:)
    paths = Dir[File.join(sig_dir, "*.rbs")].sort
    raise "no signatures in #{sig_dir}" if paths.empty?

    pages = {}
    index = []

    paths.each do |path|
      declarations = RBS::Parser.parse_signature(File.read(path)).last

      each_class(declarations) do |class_name, declaration|
        dir = class_name.tr(":", "_")

        class_long = long_form(declaration.comment)
        if class_long
          pages["#{dir}/index.md"] = "# #{class_name}\n\n#{class_long}\n"
          index << [class_name, "#{dir}/index.md"]
        end

        declaration.members.each do |member|
          next unless member.is_a?(RBS::AST::Members::MethodDefinition)

          long = long_form(member.comment)
          next unless long

          name = member.name.to_s
          file = file_name_for(name)
          unless file
            warn "gen_help: skipping #{class_name}##{name} (no usable file name)"
            next
          end

          receiver = member.singleton? ? "#{class_name}." : "#{class_name}#"
          pages["#{dir}/#{file}.md"] = <<~HELP
            # #{receiver}#{name}

            #{render_signature(name, member)}

            #{summary(member.comment)}

            #{long}
          HELP

          index << [name, "#{dir}/#{file}.md"]
        end
      end
    end

    [pages, index]
  end

  LANG_MARK = "<<en>>"

  # One language out of a page that holds both. The summary carries the marker
  # inline; the long text has a line that is nothing but the marker. Same rules
  # the editor used to apply at open time -- doing it here means it does not
  # have to.
  # head_lines is how many lines the page spends before its long text: the
  # heading, the signature and the summary, which are common to both readers
  # (the summary carries its marker inline). Below them the long text has a
  # line that is nothing but the marker, and the two halves live on either
  # side of it -- so the English page has to drop what comes BEFORE it, which
  # is the part the first attempt at this got wrong.
  def self.pick_lang(body, lang, head_lines)
    lines = body.lines
    head = lines[0, head_lines] || []
    rest = lines[head_lines..-1] || []

    head = head.map do |line|
      i = line.index(LANG_MARK)
      next line unless i
      (lang == "en" ? line[(i + LANG_MARK.length)..-1].to_s.strip
                    : line[0, i].to_s.rstrip) + "\n"
    end

    mark = rest.index { |l| l.strip == LANG_MARK }
    if mark.nil?
      # One language for both readers: what there is, is what they get.
      body_rest = rest
    elsif lang == "en"
      body_rest = rest[(mark + 1)..-1] || []
    else
      body_rest = rest[0, mark] || []
    end

    (head + body_rest).join
  end

  # Class files, not one file per method.
  #
  # littlefs gives every file a 4KB block, so 573 pages of a few hundred bytes
  # each cost 2.3MB of an 8MB partition to carry 270KB of text. Grouped by
  # class it is twenty-odd files and the block waste goes with them. The pages
  # are already headed "# Class#method", so a class file is its methods
  # concatenated, and the index says which heading to jump to.
  #
  # The languages are split here as well, into <Class>.ja.md and <Class>.en.md.
  # The editor then picks a file instead of deleting half the lines of the one
  # it opened -- with many methods in a file, that filtering would have had to
  # learn about sections.
  def self.generate(sig_dir:, out_dir:)
    pages, index = build(sig_dir: sig_dir)

    FileUtils.rm_rf(out_dir)
    FileUtils.mkdir_p(out_dir)

    # path is "<Class>/<method>.md" (or "<Class>/index.md" for the class
    # comment), and the class comment sorts first inside its class.
    by_class = {}
    pages.each do |path, body|
      cls, file = path.split("/", 2)
      (by_class[cls] ||= []) << [file, body]
    end

    written = 0
    by_class.each do |cls, entries|
      entries.sort_by! { |file, _| [file == "index.md" ? 0 : 1, file] }
      %w[ja en].each do |lang|
        text = entries.map { |file, body|
          # A class page is "# Class" and a blank line; a method page adds the
          # signature and the summary with their blanks.
          pick_lang(body, lang, file == "index.md" ? 2 : 6)
        }.join("\n")
        File.write(File.join(out_dir, "#{cls}.#{lang}.md"), text)
        written += 1
      end
    end

    # name and the class file it lives in -- two columns, as before. The
    # heading to scroll to is derived from the two: a section starts with
    # "# Class#method" or "# Class.method", which the editor can build itself.
    # Carrying it as a third column meant splitting a line at a tab in the
    # editor, and that is one string operation more than Spinel could be
    # trusted with here.
    lines = index.map do |name, path|
      cls, _ = path.split("/", 2)
      "#{name}\t#{cls}\n"
    end
    File.write(File.join(out_dir, "index.txt"), lines.join)

    [written, index.size]
  end

  # The same pages as one file, for the browser (tool/web/js/ti.js fetches it).
  def self.generate_json(sig_dir:, out_file:)
    pages, index = build(sig_dir: sig_dir)
    FileUtils.mkdir_p(File.dirname(out_file))
    File.write(out_file, JSON.generate("pages" => pages, "index" => index))
    [pages.size, index.size]
  end
end

if $PROGRAM_NAME == __FILE__
  options = { sig_dir: "sig", out_dir: nil, json: nil }

  OptionParser.new do |parser|
    parser.on("--sig-dir DIRECTORY") { |v| options[:sig_dir] = v }
    parser.on("--out DIRECTORY") { |v| options[:out_dir] = v }
    parser.on("--json FILE", "write one JSON file instead of a directory") do |v|
      options[:json] = v
    end
  end.parse!(ARGV)

  if options[:json]
    written, entries = TiHelp.generate_json(sig_dir: options[:sig_dir],
                                            out_file: options[:json])
    puts "help: #{written} page(s), #{entries} index entries -> #{options[:json]}"
  else
    out_dir = options[:out_dir] || "flash/help"
    written, entries = TiHelp.generate(sig_dir: options[:sig_dir], out_dir: out_dir)
    puts "help: #{written} file(s), #{entries} index entries -> #{out_dir}"
  end
end
