#!/usr/bin/env ruby
# frozen_string_literal: true
#
# ima_adpcm.rb - turn a 16-bit mono PCM WAV into a 4-bit IMA-ADPCM WAV
# (format 0x11), the form the bundled voices ship in (doc/flash_layout/report/f3.md).
#
#   ruby tools/ima_adpcm.rb FILE.wav...              # converts in place (PCM files only)
#   ruby tools/ima_adpcm.rb --out DIR FILE.wav...    # writes DIR/<name>.wav
#   ruby tools/ima_adpcm.rb --check FILE.wav...      # prints format, length and size
#
# The bytes are those of `ffmpeg -i in.wav -c:a adpcm_ima_wav out.wav`
# (ffmpeg 4.4, no trellis): 1024-byte blocks of 2041 samples, each block a
# 4-byte header (the block's first sample, the step index, 0) and then the
# other 2040 samples as nibbles, low nibble first. Every block's step index
# carries on from the one before, and the last block is filled with silence.
# Two things differ from ffmpeg's file, neither of which the players read:
# the fact chunk counts the samples the clip really has (ffmpeg counts the
# silence that fills the last block too, so a clip played to its fact count
# would grow by up to 0.13 s), and there is no LIST chunk naming the encoder.
#
# 1/4 of the PCM's size, rate unchanged. FmrbAudio#play_wav decodes it a
# block at a time while it plays (components/fmrb_audio/fmrb_wav.c and its
# copy in fmruby-graphics-audio). Standard library only.

module ImaAdpcm
  BLOCK_ALIGN = 1024
  SAMPLES_PER_BLOCK = (BLOCK_ALIGN - 4) * 2 + 1 # 2041
  STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
          73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
          449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
          2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
          9493, 10_442, 11_487, 12_635, 13_899, 15_289, 16_818, 18_500, 20_350, 22_385, 24_623,
          27_086, 29_794, 32_767].freeze
  INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8].freeze
  # ffmpeg's encoder rebuilds its own prediction with these (step * d / 8).
  DIFF = [1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15].freeze

  class Error < StandardError; end

  module_function

  def clamp(v, lo, hi) = v < lo ? lo : (v > hi ? hi : v)

  # {format:, channels:, rate:, bits:, block_align:, data_off:, data_len:, fact:}
  def info(wav)
    wav = wav.b
    raise Error, "not a RIFF/WAVE file" unless wav.bytesize >= 12 && wav[0, 4] == "RIFF" && wav[8, 4] == "WAVE"
    out = {}
    pos = 12
    while pos + 8 <= wav.bytesize
      id = wav[pos, 4]
      size = wav[pos + 4, 4].unpack1("V")
      body = pos + 8
      case id
      when "fmt "
        out[:format], out[:channels], out[:rate], _, out[:block_align], out[:bits] = wav[body, 16].unpack("vvVVvv")
      when "fact"
        out[:fact] = wav[body, 4].unpack1("V")
      when "data"
        out[:data_off] = body
        out[:data_len] = [size, wav.bytesize - body].min
        return out
      end
      pos = body + size + (size & 1)
    end
    raise Error, "no data chunk"
  end

  def pcm_samples(wav)
    i = info(wav)
    raise Error, "not 16-bit mono PCM (format #{i[:format]}, #{i[:channels]} ch, #{i[:bits]} bit)" unless
      i[:format] == 1 && i[:channels] == 1 && i[:bits] == 16
    [wav.b[i[:data_off], i[:data_len] & ~1].unpack("s<*"), i[:rate]]
  end

  # One sample into a nibble; state is [prediction, step index].
  def compress(state, sample)
    pred, idx = state
    delta = sample - pred
    nib = [7, delta.abs * 4 / STEP[idx]].min + (delta.negative? ? 8 : 0)
    pred = clamp(pred + c_div(STEP[idx] * DIFF[nib], 8), -32_768, 32_767)
    state[0] = pred
    state[1] = clamp(idx + INDEX[nib], 0, 88)
    nib
  end

  # C's integer division (towards zero), which the prediction above uses.
  def c_div(a, b) = (a.abs / b) * (a.negative? ? -1 : 1)

  def encode(samples, rate)
    raise Error, "no samples" if samples.empty?
    blocks = (samples.size + SAMPLES_PER_BLOCK - 1) / SAMPLES_PER_BLOCK
    padded = samples + [0] * (blocks * SAMPLES_PER_BLOCK - samples.size)
    state = [0, 0]
    data = +"".b
    blocks.times do |b|
      blk = padded[b * SAMPLES_PER_BLOCK, SAMPLES_PER_BLOCK]
      state[0] = blk[0]
      data << [blk[0], state[1], 0].pack("s<CC")
      i = 1
      while i < SAMPLES_PER_BLOCK
        lo = compress(state, blk[i])
        hi = compress(state, blk[i + 1])
        data << ((hi << 4) | lo).chr
        i += 2
      end
    end
    fmt = [0x11, 1, rate, rate * BLOCK_ALIGN / SAMPLES_PER_BLOCK, BLOCK_ALIGN, 4, 2, SAMPLES_PER_BLOCK]
          .pack("vvVVvvvv")
    body = "WAVE".b + chunk("fmt ", fmt) + chunk("fact", [samples.size].pack("V")) + chunk("data", data)
    "RIFF".b + [body.bytesize].pack("V") + body
  end

  def chunk(id, body) = id.b + [body.bytesize].pack("V") + body + (body.bytesize.odd? ? "\0".b : "".b)

  # The decoder the firmware has, for the tests: ffmpeg's ((2n + 1) * step) / 8,
  # the same sum the encoder predicted with, so the two never drift apart
  # (the textbook decoder adds step / 8, / 4, / 2 bit by bit and lands a few
  # steps off).
  def decode(wav)
    i = info(wav)
    raise Error, "not IMA-ADPCM mono" unless i[:format] == 0x11 && i[:channels] == 1
    data = wav.b[i[:data_off], i[:data_len]]
    ba = i[:block_align]
    out = []
    idx = 0
    off = 0
    while off + 4 <= data.bytesize
      pred, idx = data[off, 4].unpack("s<C")
      idx = clamp(idx, 0, 88)
      out << pred
      data[off + 4, [ba, data.bytesize - off].min - 4].each_byte do |byte|
        [byte & 15, byte >> 4].each do |nib|
          diff = ((2 * (nib & 7) + 1) * STEP[idx]) >> 3
          pred = clamp(nib & 8 != 0 ? pred - diff : pred + diff, -32_768, 32_767)
          idx = clamp(idx + INDEX[nib], 0, 88)
          out << pred
        end
      end
      off += ba
    end
    i[:fact] ? out.first(i[:fact]) : out
  end

  def convert(wav)
    samples, rate = pcm_samples(wav)
    encode(samples, rate)
  end
end

if $PROGRAM_NAME == __FILE__
  out_dir = nil
  check = false
  files = []
  args = ARGV.dup
  until args.empty?
    a = args.shift
    case a
    when "--out" then out_dir = File.expand_path(args.shift)
    when "--check" then check = true
    when "-h", "--help"
      puts File.read(__FILE__)[/^# ima_adpcm.rb.*?(?=^$)/m].gsub(/^# ?/, "")
      exit 0
    else files << a
    end
  end
  if files.empty?
    warn "usage: ruby tools/ima_adpcm.rb [--out DIR | --check] FILE.wav..."
    exit 2
  end
  begin
    files.each do |f|
      wav = File.binread(f)
      i = ImaAdpcm.info(wav)
      if check
        n = i[:fact] || (i[:format] == 1 ? i[:data_len] / 2 : 0)
        puts format("%-40s format 0x%02x %5d Hz %6d samples %.2f s %7d bytes", f, i[:format], i[:rate], n,
                    n.to_f / i[:rate], wav.bytesize)
        next
      end
      if i[:format] == 0x11 && out_dir.nil?
        puts "#{f}: already IMA-ADPCM"
        next
      end
      adpcm = ImaAdpcm.convert(wav)
      dest = out_dir ? File.join(out_dir, File.basename(f)) : f
      File.binwrite(dest, adpcm)
      puts format("%s: %d -> %d bytes", dest, wav.bytesize, adpcm.bytesize)
    end
  rescue ImaAdpcm::Error => e
    warn "ima_adpcm: #{e.message}"
    exit 1
  end
end
