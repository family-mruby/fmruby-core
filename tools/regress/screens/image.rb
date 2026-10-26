# frozen_string_literal: true
#
# image.rb - a screen capture in memory for rake regress:screens: read (the
# root tools' FmrbPng), compare with masks, write PNGs (the shot, the diff).
#
# Captures come from the sim's shared memory or the browser build's frame
# buffer, never from a camera or a JPEG, so they are compared exactly: one
# channel off by one is a difference. What moves on its own (the clock, the
# free memory) is masked by the scene instead of being compared loosely.
require "zlib"
require_relative "../../../../tools/fmrb_png"

module Regress
  module Screens
    class Image
      attr_reader :w, :h, :rows

      def self.load(path)
        w, h, rows = FmrbPng.read_rgb(path)
        new(w, h, rows)
      end

      def initialize(w, h, rows)
        @w = w
        @h = h
        @rows = rows
      end

      def pixel(x, y)
        o = x * 3
        r = @rows[y]
        [r.getbyte(o), r.getbyte(o + 1), r.getbyte(o + 2)]
      end

      # Pixels that differ outside the masks ([x, y, w, h] each). Returns
      # [count, bounding box [x0, y0, x1, y1] or nil]. A different size is
      # reported as every pixel.
      def diff(other, masks = [], rect: nil)
        return [@w * @h, [0, 0, @w - 1, @h - 1]] if other.w != @w || other.h != @h
        cover = Image.mask_rows(@w, @h, masks)
        x_lo, y_lo, x_hi, y_hi = Image.clip(rect, @w, @h)
        count = 0
        box = nil
        (y_lo...y_hi).each do |y|
          a = @rows[y]
          b = other.rows[y]
          next if a == b && rect.nil?
          skip = cover[y]
          x = x_lo
          while x < x_hi
            o = x * 3
            if (a.getbyte(o) != b.getbyte(o) || a.getbyte(o + 1) != b.getbyte(o + 1) ||
                a.getbyte(o + 2) != b.getbyte(o + 2)) && !(skip && skip[x])
              count += 1
              if box
                box[0] = x if x < box[0]
                box[2] = x if x > box[2]
                box[3] = y
              else
                box = [x, y, x, y]
              end
            end
            x += 1
          end
        end
        [count, box]
      end

      def same?(other, masks = [], rect: nil)
        diff(other, masks, rect: rect)[0].zero?
      end

      # rows[y] -> nil (nothing masked) or an Array of booleans per x.
      def self.mask_rows(w, h, masks)
        cover = Array.new(h)
        masks.each do |mx, my, mw, mh|
          ([my, 0].max...[my + mh, h].min).each do |y|
            cover[y] ||= Array.new(w, false)
            ([mx, 0].max...[mx + mw, w].min).each { |x| cover[y][x] = true }
          end
        end
        cover
      end

      def self.clip(rect, w, h)
        return [0, 0, w, h] unless rect
        x, y, rw, rh = rect
        [[x, 0].max, [y, 0].max, [x + rw, w].min, [y + rh, h].min]
      end

      # The picture a person looks at when a shot fails: the baseline dimmed
      # to grey, the differing pixels in magenta, the masked areas dark blue.
      def diff_picture(other, masks)
        cover = Image.mask_rows(@w, @h, masks)
        rows = Array.new(@h) do |y|
          out = String.new(capacity: @w * 3, encoding: Encoding::BINARY)
          a = @rows[y]
          b = other.w == @w && other.h == @h ? other.rows[y] : nil
          @w.times do |x|
            o = x * 3
            ar = a.getbyte(o)
            ag = a.getbyte(o + 1)
            ab = a.getbyte(o + 2)
            if cover[y] && cover[y][x]
              out << 0x10.chr << 0x18.chr << 0x50.chr
            elsif b.nil? || ar != b.getbyte(o) || ag != b.getbyte(o + 1) || ab != b.getbyte(o + 2)
              out << 0xFF.chr << 0x00.chr << 0xFF.chr
            else
              g = ((ar * 3 + ag * 6 + ab) / 10 / 3 + 40).clamp(0, 255)
              out << g.chr << g.chr << g.chr
            end
          end
          out
        end
        Image.new(@w, @h, rows)
      end

      # 8-bit RGB, no interlace, filter "none" on every line: what FmrbPng
      # reads back and any viewer opens.
      def write(path)
        raw = String.new(capacity: (@w * 3 + 1) * @h, encoding: Encoding::BINARY)
        @rows.each do |r|
          raw << "\x00".b
          raw << r
        end
        png = "\x89PNG\r\n\x1A\n".b
        png << chunk("IHDR", [@w, @h, 8, 2, 0, 0, 0].pack("NNCCCCC"))
        png << chunk("IDAT", Zlib::Deflate.deflate(raw, Zlib::BEST_COMPRESSION))
        png << chunk("IEND", "".b)
        File.binwrite(path, png)
      end

      private

      def chunk(type, data)
        body = type.b + data
        [data.bytesize].pack("N") + body + [Zlib.crc32(body)].pack("N")
      end
    end
  end
end
