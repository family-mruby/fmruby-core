# rakelib/kanji.rake
# The kanji dictionary (doc/kanji_input/): fetch the pinned source data,
# generate the dictionary file, test the generator. Host Ruby only; nothing
# here builds firmware. Sources and output stay in tools/kanji/work/
# (git-ignored), never in the repository.

namespace :kanji do
  desc "Generate the kanji dictionary (SIZE=1M by default, OUT=dir, WORK=dir; fetches the pinned sources first)"
  task :dict do
    args = []
    args += ["--size", ENV["SIZE"]] if ENV["SIZE"]
    args += ["--out", ENV["OUT"]] if ENV["OUT"]
    args += ["--work", ENV["WORK"]] if ENV["WORK"]
    ruby "tools/kanji/make_dict.rb", *args
  end

  desc "Fetch the kanji dictionary's pinned source data (WORK=dir)"
  task :fetch do
    args = ENV["WORK"] ? ["--work", ENV["WORK"]] : []
    ruby "tools/kanji/sources.rb", *args
  end

  # DEST is the root of what becomes /sd on the machine (a mounted card, a
  # directory to attach to a release from, a test directory): the files land
  # in DEST/dict/. Checked before and after the copy.
  desc "Copy the generated kanji dictionary and its LICENSE to DEST/dict/, checked (FROM=dir)"
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
  desc "Kanji dictionary generator, reader and C lookup tests (host Ruby + cc, no fetch)"
  task :test do
    ruby "tools/kanji/test/run.rb"
    # The board's C lookup (components/fmrb_kanji, K2a) against the Ruby
    # reader, host cc; the generated dictionary too when it is there.
    ruby "tools/kanji/test/c_lookup.rb"
  end
end
