#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Reference lookup in a kanji dictionary file (doc/kanji_input/dict_format.md),
# done the way the board will do it: the header, character table and block
# index up front, then one block read per lookup. The board's C lookup (K2)
# must give the same answers.
#
#   ruby tools/kanji/lookup.rb [--dict FILE] よみ...
#   ruby tools/kanji/lookup.rb [--dict FILE] --info
#
# For each reading prints the candidates of the exact reading and, when the
# whole reading has none, the longest leading part that has some (いった ->
# いっ: 言っ 行っ ...; the rest is left for the next lookup).
#
# Standard library only.

require_relative "dict_format"

DEFAULT_DICT = File.expand_path("work/out/fmrb_kanji.dic", __dir__)

def show(dict, reading)
  cands = dict.lookup(reading)
  if cands.empty?
    lp = dict.longest_prefix(reading)
    if lp
      p, c = lp
      puts "#{reading}: (none) longest prefix #{p} [#{c.size}]: #{c.join(' ')} / rest #{reading[p.length..]}"
    else
      puts "#{reading}: (none)"
    end
  else
    puts "#{reading} [#{cands.size}]: #{cands.join(' ')}"
  end
end

if $PROGRAM_NAME == __FILE__
  args = ARGV.dup
  path = DEFAULT_DICT
  if (i = args.index("--dict"))
    path = args[i + 1]
    args.slice!(i, 2)
  end
  abort "usage: lookup.rb [--dict FILE] よみ... | --info" if args.empty?
  dict = KanjiDict::Reader.new(path)
  if args == ["--info"]
    dict.header.each { |k, v| puts format("%-14s %s", k, v) }
    exit
  end
  args.each { |r| show(dict, r.encode("UTF-8")) }
  dict.close
end
