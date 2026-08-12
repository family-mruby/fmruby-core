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
# usage: gen_help.rb --sig-dir sig --out flash/help

require "optparse"
require "fileutils"
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

  def self.generate(sig_dir:, out_dir:)
    paths = Dir[File.join(sig_dir, "*.rbs")].sort
    raise "no signatures in #{sig_dir}" if paths.empty?

    FileUtils.rm_rf(out_dir)
    FileUtils.mkdir_p(out_dir)

    index = []
    written = 0

    paths.each do |path|
      declarations = RBS::Parser.parse_signature(File.read(path)).last

      each_class(declarations) do |class_name, declaration|
        dir = File.join(out_dir, class_name.tr(":", "_"))

        class_long = long_form(declaration.comment)
        if class_long
          FileUtils.mkdir_p(dir)
          File.write(File.join(dir, "index.md"),
                     "# #{class_name}\n\n#{class_long}\n")
          index << [class_name, "#{class_name.tr(":", "_")}/index.md"]
          written += 1
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
          FileUtils.mkdir_p(dir)
          File.write(File.join(dir, "#{file}.md"), <<~HELP)
            # #{receiver}#{name}

            #{render_signature(name, member)}

            #{summary(member.comment)}

            #{long}
          HELP

          index << [name, "#{class_name.tr(":", "_")}/#{file}.md"]
          written += 1
        end
      end
    end

    # One line per entry, name first: the editor reads it top to bottom and
    # takes the first match, so a name in two classes lists both and the
    # editor reports the choice it made.
    File.write(File.join(out_dir, "index.txt"),
               index.map { |name, path| "#{name}\t#{path}\n" }.join)

    [written, index.size]
  end
end

if $PROGRAM_NAME == __FILE__
  options = { sig_dir: "sig", out_dir: "flash/help" }

  OptionParser.new do |parser|
    parser.on("--sig-dir DIRECTORY") { |v| options[:sig_dir] = v }
    parser.on("--out DIRECTORY") { |v| options[:out_dir] = v }
  end.parse!(ARGV)

  written, entries = TiHelp.generate(sig_dir: options[:sig_dir],
                                     out_dir: options[:out_dir])
  puts "help: #{written} file(s), #{entries} index entries -> #{options[:out_dir]}"
end
