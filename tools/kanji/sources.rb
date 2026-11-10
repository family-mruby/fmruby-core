# frozen_string_literal: true
#
# The source data of the kanji dictionary, pinned by version and sha256, and
# fetching it into a work directory outside the build (doc/kanji_input/ K1).
#
#   ruby tools/kanji/sources.rb [--work DIR]      # fetch what is missing
#
# Nothing fetched here goes into the repository: the default work directory
# (tools/kanji/work/) is ignored by git.
#
# Standard library only (Net::HTTP for the download, Zlib for the one file
# taken out of the Unihan zip).

require "digest"
require "fileutils"
require "net/http"
require "uri"
require "zlib"

module KanjiSources
  MOZC_COMMIT = "a4541df29db443b49f81cf54665873b68fde9efa"
  MOZC_RAW = "https://raw.githubusercontent.com/google/mozc/#{MOZC_COMMIT}/"
  UNIHAN_VERSION = "17.0.0"
  UNIHAN_URL = "https://www.unicode.org/Public/#{UNIHAN_VERSION}/ucd/Unihan.zip"
  UNIHAN_SHA256 = "f7a48b2b545acfaa77b2d607ae28747404ce02baefee16396c5d2d7a8ef34b5e"
  # The efontJA glyph set (M5GFX, the same file every build carries; see
  # doc/kanji_input/report/k0.md). Only its list of characters is used, to
  # narrow the base font to what every configuration draws; nothing of it
  # goes into the dictionary.
  EFONT_URL = "https://raw.githubusercontent.com/m5stack/M5GFX/0.2.36/src/lgfx/Fonts/efont/lgfx_efont_ja.c"
  EFONT_SHA256 = "a478967b466608d6e6d990f6d1994cceb53b394ff07da61e9d390628f007a047"
  # The Unicode License (v3). The file is not versioned (its copyright year
  # moves), so it is checked by its first line instead of a digest.
  UNICODE_LICENSE_URL = "https://www.unicode.org/license.txt"
  UNICODE_LICENSE_HEAD = "UNICODE LICENSE V3"

  # path in the mozc tree => sha256 at MOZC_COMMIT
  MOZC_FILES = {
    "LICENSE" => "44cdd923b91ea9199293abecc2762c70c87dbf1e581c027a94c416368d1a648c",
    "src/data/dictionary_oss/dictionary00.txt" => "9e07ce292932c4fff327ed8cb06274c09acca556bd8bdf7c7ef1f6173eae3b61",
    "src/data/dictionary_oss/dictionary01.txt" => "836fd5fd8399510de0d8ef7b33be913cca6bbaee7b712cc8045fba03f4c75a89",
    "src/data/dictionary_oss/dictionary02.txt" => "93943858d0cfd092b849a78def6901eaf228177e1ac5663544e865a3a1e45a9e",
    "src/data/dictionary_oss/dictionary03.txt" => "cb9f1b0b4a7550d682d86244a021cd17e0b15366328819e69135b5f183a1a24b",
    "src/data/dictionary_oss/dictionary04.txt" => "9fdc4e3948d03463de13ab5782adffbc11dd06e934cb80138c5920029cd3d2f6",
    "src/data/dictionary_oss/dictionary05.txt" => "199e3b0f050b691823ae7ba943090d437f51af6436b27c5f3dc69cafa12000e4",
    "src/data/dictionary_oss/dictionary06.txt" => "b95b14f7770ae2a824bc8e9fad8b03c1bc45c504f93a36554b8b56daa0368d2a",
    "src/data/dictionary_oss/dictionary07.txt" => "7f53649b529d4c8c495d3a8dd9c4d489dfe7696080f7423107ff01331174be48",
    "src/data/dictionary_oss/dictionary08.txt" => "50a31bb5fc1646721b00be371a5695dffab1292bda143e1e59cc70cbbdd8643f",
    "src/data/dictionary_oss/dictionary09.txt" => "4e5fcd4cbfbf11395166a37b506f547fc54d20edc0884f5184ecd3fc705f0d9a",
    "src/data/single_kanji/single_kanji.tsv" => "bfa6a4be1319a144f770ba281dfc04091a05320e334357f949b293dc5f9c3d0c",
    "src/data/symbol/symbol.tsv" => "bc68b032a195f203dccba6f95667424199ba9b185fee2be64eeaa959253c097a"
  }.freeze

  DEFAULT_WORK = File.expand_path("work", __dir__)

  module_function

  # Where each input lives under a work directory. The generator reads these
  # paths, so a hand-made set (the tests' fixture) can stand in.
  def layout(work)
    mozc = File.join(work, "src", "mozc-#{MOZC_COMMIT[0, 12]}")
    unihan = File.join(work, "src", "unihan-#{UNIHAN_VERSION}")
    {
      mozc_dir: mozc,
      mozc_license: File.join(mozc, "LICENSE"),
      dictionaries: (0..9).map { |i| File.join(mozc, format("src/data/dictionary_oss/dictionary%02d.txt", i)) },
      single_kanji: File.join(mozc, "src/data/single_kanji/single_kanji.tsv"),
      symbol: File.join(mozc, "src/data/symbol/symbol.tsv"),
      unihan_zip: File.join(unihan, "Unihan.zip"),
      unihan_readings: File.join(unihan, "Unihan_Readings.txt"),
      unicode_license: File.join(unihan, "license.txt"),
      efont: File.join(work, "src", "m5gfx-0.2.36", "lgfx_efont_ja.c")
    }
  end

  def sha256(path)
    Digest::SHA256.file(path).hexdigest
  end

  def download(url, dest, redirects = 5)
    uri = URI(url)
    FileUtils.mkdir_p(File.dirname(dest))
    tmp = "#{dest}.part"
    Net::HTTP.start(uri.host, uri.port, use_ssl: uri.scheme == "https") do |http|
      http.request(Net::HTTP::Get.new(uri)) do |res|
        case res
        when Net::HTTPRedirection
          raise "too many redirects: #{url}" if redirects.zero?

          return download(URI.join(url, res["location"]).to_s, dest, redirects - 1)
        when Net::HTTPSuccess
          File.open(tmp, "wb") { |f| res.read_body { |chunk| f.write(chunk) } }
        else
          raise "#{url}: HTTP #{res.code}"
        end
      end
    end
    File.rename(tmp, dest)
  end

  def fetch_pinned(url, dest, sha)
    return :kept if File.exist?(dest) && sha256(dest) == sha

    $stderr.puts "fetch #{url}"
    download(url, dest)
    got = sha256(dest)
    return :fetched if got == sha

    File.delete(dest)
    raise "#{url}: sha256 #{got}, expected #{sha}"
  end

  # One member of a zip file (stored or deflated), found through the
  # central directory.
  def unzip_member(zip_path, name)
    data = File.binread(zip_path)
    eocd = data.rindex("PK\x05\x06".b) or raise "#{zip_path}: no end of central directory"
    count = data.byteslice(eocd + 10, 2).unpack1("v")
    pos = data.byteslice(eocd + 16, 4).unpack1("V")
    count.times do
      raise "#{zip_path}: bad central directory" unless data.byteslice(pos, 4) == "PK\x01\x02".b

      method, = data.byteslice(pos + 10, 2).unpack("v")
      csize, = data.byteslice(pos + 20, 4).unpack("V")
      nlen, xlen, clen = data.byteslice(pos + 28, 6).unpack("vvv")
      local = data.byteslice(pos + 42, 4).unpack1("V")
      fname = data.byteslice(pos + 46, nlen)
      pos += 46 + nlen + xlen + clen
      next unless fname == name

      lnlen, lxlen = data.byteslice(local + 26, 4).unpack("vv")
      body = data.byteslice(local + 30 + lnlen + lxlen, csize)
      case method
      when 0 then return body
      when 8
        z = Zlib::Inflate.new(-Zlib::MAX_WBITS)
        out = z.inflate(body)
        z.close
        return out
      else raise "#{zip_path}: #{name} uses compression method #{method}"
      end
    end
    raise "#{zip_path}: #{name} not found"
  end

  # Fetch everything the generator needs that is not already in `work`.
  def fetch(work = DEFAULT_WORK)
    l = layout(work)
    MOZC_FILES.each do |path, sha|
      fetch_pinned(MOZC_RAW + path, File.join(l[:mozc_dir], path), sha)
    end
    fetch_pinned(UNIHAN_URL, l[:unihan_zip], UNIHAN_SHA256)
    fetch_pinned(EFONT_URL, l[:efont], EFONT_SHA256)
    unless File.exist?(l[:unihan_readings])
      File.binwrite(l[:unihan_readings], unzip_member(l[:unihan_zip], "Unihan_Readings.txt"))
    end
    unless File.exist?(l[:unicode_license])
      $stderr.puts "fetch #{UNICODE_LICENSE_URL}"
      download(UNICODE_LICENSE_URL, l[:unicode_license])
    end
    head = File.open(l[:unicode_license], &:gets).to_s.strip
    unless head == UNICODE_LICENSE_HEAD
      File.delete(l[:unicode_license])
      raise "#{UNICODE_LICENSE_URL}: first line is #{head.inspect}, expected #{UNICODE_LICENSE_HEAD.inspect}"
    end
    l
  end
end

if $PROGRAM_NAME == __FILE__
  work = KanjiSources::DEFAULT_WORK
  if (i = ARGV.index("--work"))
    work = File.expand_path(ARGV[i + 1])
  end
  l = KanjiSources.fetch(work)
  puts "sources in #{File.dirname(l[:mozc_dir])}"
end
