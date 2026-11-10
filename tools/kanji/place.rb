#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Put the generated kanji dictionary where a machine looks for it, checked
# (doc/kanji_input/ K5). The machine reads /sd/dict/fmrb_kanji.dic, so DEST
# is the root of what becomes /sd: a mounted SD card, a directory to attach
# to a release from, or a test directory whose dict/ the configuration's
# kanji_dict points at.
#
#   ruby tools/kanji/place.rb [--from DIR] DEST
#       copies DIR/fmrb_kanji.dic and DIR/LICENSE (rake kanji:dict's output,
#       tools/kanji/work/out by default) to DEST/dict/fmrb_kanji.dic and
#       DEST/dict/fmrb_kanji_LICENSE.txt, checking the file before and the
#       copy after (header, size, crc32)
#   ruby tools/kanji/place.rb --check FILE...
#       checks dictionary files only (one taken from a release, or the one on
#       a card)
#
# Exit status 0 when every file is a whole dictionary of the format this tree
# reads. Standard library only.

require "fileutils"
require "digest"
require "zlib"
require_relative "dict_format"

module KanjiPlace
  DEFAULT_FROM = File.expand_path("work/out", __dir__)
  DIC_NAME = "fmrb_kanji.dic"
  LICENSE_NAME = "fmrb_kanji_LICENSE.txt"

  module_function

  # Raises unless path is a whole dictionary; returns its header.
  def check(path)
    reader = KanjiDict::Reader.new(path)
    h = reader.header
    reader.close
    rest = File.binread(path, nil, KanjiDict::HEADER_SIZE)
    raise "#{path}: crc32 mismatch (a broken or partial copy)" unless Zlib.crc32(rest) == h[:crc32]

    h
  end

  def describe(path, h)
    format("%s: ok, %d bytes, %d words, %d readings, sha256 %s",
           path, h[:file_size], h[:word_count], h[:reading_count],
           Digest::SHA256.file(path).hexdigest)
  end

  def place(from, dest)
    src = File.join(from, DIC_NAME)
    lic = File.join(from, "LICENSE")
    raise "#{src}: not there (rake kanji:dict makes it)" unless File.file?(src)
    raise "#{lic}: not there (rake kanji:dict writes it next to the dictionary)" unless File.file?(lic)

    check(src)
    dir = File.join(dest, "dict")
    FileUtils.mkdir_p(dir)
    out = File.join(dir, DIC_NAME)
    FileUtils.cp(src, out)
    FileUtils.cp(lic, File.join(dir, LICENSE_NAME))
    h = check(out)
    puts describe(out, h)
    puts "#{File.join(dir, LICENSE_NAME)}: copied"
  end
end

if $PROGRAM_NAME == __FILE__
  args = ARGV.dup
  usage = "usage: place.rb [--from DIR] DEST | place.rb --check FILE..."
  begin
    if args.first == "--check"
      files = args.drop(1)
      abort usage if files.empty?
      files.each { |f| puts KanjiPlace.describe(f, KanjiPlace.check(f)) }
    else
      from = KanjiPlace::DEFAULT_FROM
      if (i = args.index("--from"))
        from = args[i + 1] or abort usage
        args.slice!(i, 2)
      end
      abort usage unless args.size == 1
      KanjiPlace.place(from, args[0])
    end
  rescue RuntimeError, SystemCallError => e
    abort e.message
  end
end
