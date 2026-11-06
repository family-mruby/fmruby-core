# UI string localization helpers shared by all Family mruby apps.
#
# Loaded into every picoruby VM that pulls in picoruby-fmrb-app, so any
# kernel script or user app can call FmrbI18n.t(:key) and the pixel-width
# helpers needed for mixed ASCII/UTF-8 rendering via draw_text(..., mixed: true).
#
# Strings are registered per-VM via FmrbI18n.add({"en" => {...}, "ja" => {...}});
# the system_desktop and config apps each register their own subset on startup.
#
# Active language is read from FmrbConst::LANGUAGE (populated by const.c from
# system_conf.toml `language = "ja"|"en"`) the first time `lang` is called and
# then cached. Restart picks up any change. An app restored from a checkpoint
# image forgets it (forget), so it speaks the language of the machine where
# it arrived, not of the one that wrote the image.

module FmrbI18n
  DEFAULT_LANG = "en"

  # The words of this VM, by language: { "en" => {key => text}, "ja" => ... }.
  # Made the first time they are asked for, not when the VM opens, and kept in
  # the module's instance variable. A checkpoint image carries what was made
  # after the VM opened, and a change to an object that was there before only
  # when that object has a name of its own: a table made at the start (the
  # constant this used to be, whose inner tables and keys have no name) lost
  # the words an app added when the app was moved, or could not be written
  # once a new inner table was put in (doc/checkpoint,
  # report/lang_bg_restore.md).
  def self.strings
    s = @strings
    if s.nil?
      s = { "en" => {}, "ja" => {} }
      @strings = s
    end
    s
  end

  def self.add(table)
    all = strings
    table.each do |lang, kv|
      bucket = all[lang] ||= {}
      kv.each { |k, v| bucket[k] = v }
    end
  end

  # Active language, resolved once per VM. Asked of FmrbApp rather than read
  # from FmrbConst directly: a Spinel-compiled app has its constants baked in at
  # compile time, so only FmrbApp.language sees what the user actually chose.
  def self.lang
    @lang ||= begin
      l = FmrbApp.language.to_s
      strings.key?(l) ? l : DEFAULT_LANG
    rescue
      DEFAULT_LANG
    end
  end

  # Forget the language resolved so far; the next lang reads it again. The
  # cache lives on the module, which a checkpoint image carries, so a restored
  # app calls this (FmrbApp#_checkpoint_restored, doc/checkpoint): the words
  # come from the machine where the app arrived, not the one that wrote it.
  def self.forget
    @lang = nil
    nil
  end

  def self.t(key)
    all = strings
    tbl = all[lang] || all[DEFAULT_LANG]
    tbl[key] || all[DEFAULT_LANG][key] || key.to_s
  end

  # Pixel width of a UTF-8 string when rendered with draw_text(..., mixed: true).
  # ASCII bytes use Font0 (6 px); UTF-8 multi-byte sequences use misaki_8, whose
  # cell is 8 px for full-width glyphs and 4 px for half-width katakana.
  # Walks bytes directly to avoid String#bytes allocation.
  def self.text_width(str)
    return 0 unless str
    w = 0
    i = 0
    n = str.bytesize
    while i < n
      b = str.getbyte(i)
      if b < 0x80
        w += 6
        i += 1
      elsif b < 0xC0
        i += 1
      elsif b < 0xE0
        w += 8
        i += 2
      elsif b < 0xF0
        w += half_kana?(str, i, n) ? 4 : 8
        i += 3
      else
        w += 8
        i += 4
      end
    end
    w
  end

  # True when the 3-byte sequence at i is half-width katakana (U+FF61-U+FF9F,
  # encoded EF BD A1 .. EF BE 9F). misaki draws these in a half cell.
  def self.half_kana?(str, i, n)
    return false unless str.getbyte(i) == 0xEF
    return false if i + 2 >= n
    b1 = str.getbyte(i + 1)
    b2 = str.getbyte(i + 2)
    (b1 == 0xBD && b2 >= 0xA1) || (b1 == 0xBE && b2 <= 0x9F)
  end

  # Truncate so the mixed-mode pixel width is <= max_px. Appends ellipsis
  # (default "..", 12 px) when truncation occurs. Pass "" to suppress it.
  def self.truncate_to(str, max_px, ellipsis = "..")
    return str if text_width(str) <= max_px
    ell_w = text_width(ellipsis)
    budget = max_px - ell_w
    budget = 0 if budget < 0
    out = ""
    out_w = 0
    i = 0
    n = str.bytesize
    while i < n
      b = str.getbyte(i)
      if b < 0x80
        cw = 6
        step = 1
      elsif b < 0xC0
        i += 1
        next
      elsif b < 0xE0
        cw = 8
        step = 2
      elsif b < 0xF0
        cw = half_kana?(str, i, n) ? 4 : 8
        step = 3
      else
        cw = 8
        step = 4
      end
      break if out_w + cw > budget
      out += str.byteslice(i, step)
      out_w += cw
      i += step
    end
    out + ellipsis
  end
end
