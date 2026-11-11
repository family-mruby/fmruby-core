# rakelib/kanji.rake
# The kanji dictionary (doc/kanji_input/): fetch the pinned source data,
# generate the dictionary file, test the generator. Host Ruby only; nothing
# here builds firmware. Sources and output stay in tools/kanji/work/
# (git-ignored), never in the repository.

namespace :kanji do
  desc "Generate the kanji dictionary (SIZE=1M by default, NAME=file, OUT=dir, WORK=dir; fetches the pinned sources first)"
  task :dict do
    args = []
    args += ["--size", ENV["SIZE"]] if ENV["SIZE"]
    args += ["--name", ENV["NAME"]] if ENV["NAME"]
    args += ["--out", ENV["OUT"]] if ENV["OUT"]
    args += ["--work", ENV["WORK"]] if ENV["WORK"]
    ruby "tools/kanji/make_dict.rb", *args
  end

  # The two the release carries: fmrb_kanji.dic (1 MB, the standard one)
  # and fmrb_kanji_4m.dic (4 MB, for an SD card), with the one LICENSE.
  desc "Generate both release dictionaries, 1 MB and 4 MB (OUT=dir, WORK=dir)"
  task :dict_release do
    common = []
    common += ["--out", ENV["OUT"]] if ENV["OUT"]
    common += ["--work", ENV["WORK"]] if ENV["WORK"]
    [["fmrb_kanji.dic", "1M"], ["fmrb_kanji_4m.dic", "4M"]].each do |name, size|
      ruby "tools/kanji/make_dict.rb", "--size", size, "--name", name, *common
    end
  end

  desc "Fetch the kanji dictionary's pinned source data (WORK=dir)"
  task :fetch do
    args = ENV["WORK"] ? ["--work", ENV["WORK"]] : []
    ruby "tools/kanji/sources.rb", *args
  end

  # DEST is the root of what becomes /sd on the machine (a mounted card, a
  # directory to attach to a release from, a test directory): the files land
  # in DEST/dict/. Checked before and after the copy.
  desc "Copy the generated kanji dictionaries (1 MB, and 4 MB when made) and the LICENSE to DEST/dict/, checked (FROM=dir)"
  task :place do
    abort "usage: rake kanji:place DEST=<the card's root or a directory>" unless ENV["DEST"]
    args = ENV["FROM"] ? ["--from", ENV["FROM"]] : []
    ruby "tools/kanji/place.rb", *args, ENV["DEST"]
  end

  desc "Check a kanji dictionary file (FILE=path; header, size, crc32)"
  task :check do
    abort "usage: rake kanji:check FILE=<fmrb_kanji.dic>" unless ENV["FILE"]
    ruby "tools/kanji/place.rb", "--check", ENV["FILE"]
  end

  # Small made-up inputs in a temporary directory; nothing is fetched.
  desc "Kanji dictionary generator, reader, C lookup and kana composer tests (host Ruby + cc, no fetch)"
  task :test do
    ruby "tools/kanji/test/run.rb"
    # The board's C lookup (components/fmrb_kanji, K2a) against the Ruby
    # reader, host cc; the generated dictionary too when it is there.
    ruby "tools/kanji/test/c_lookup.rb"
    # The kana composer (main/drivers/usb/fmrb_kana.c, K6) and the
    # dictionary's idea of what it can type.
    ruby "tools/kanji/test/c_kana.rb"
  end
end
