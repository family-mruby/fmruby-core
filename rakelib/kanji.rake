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

  # Small made-up inputs in a temporary directory; nothing is fetched.
  desc "Kanji dictionary generator, reader and C lookup tests (host Ruby + cc, no fetch)"
  task :test do
    ruby "tools/kanji/test/run.rb"
    # The board's C lookup (components/fmrb_kanji, K2a) against the Ruby
    # reader, host cc; the generated dictionary too when it is there.
    ruby "tools/kanji/test/c_lookup.rb"
  end
end
