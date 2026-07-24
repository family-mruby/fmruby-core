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
# then cached. Restart picks up any change.

module FmrbI18n
  DEFAULT_LANG = "en"
  STRINGS = { "en" => {}, "ja" => {} }

  def self.add(table)
    table.each do |lang, kv|
      bucket = STRINGS[lang] ||= {}
      kv.each { |k, v| bucket[k] = v }
    end
  end

  def self.lang
    @lang ||= begin
      l = FmrbConst::LANGUAGE.to_s
      STRINGS.key?(l) ? l : DEFAULT_LANG
    rescue
      DEFAULT_LANG
    end
  end

  def self.t(key)
    tbl = STRINGS[lang] || STRINGS[DEFAULT_LANG]
    tbl[key] || STRINGS[DEFAULT_LANG][key] || key.to_s
  end

  # Pixel width of a UTF-8 string when rendered with draw_text(..., mixed: true).
  # ASCII bytes use Font0 (6 px); UTF-8 multi-byte sequences use misaki_8 (8 px).
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
        w += 8
        i += 3
      else
        w += 8
        i += 4
      end
    end
    w
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
        cw = 8
        step = 3
      else
        cw = 8
        step = 4
      end
      break if out_w + cw > budget
      out += str.byteslice(i, step).to_s  # byteslice is nilable; pin to String (Spinel :str)
      out_w += cw
      i += step
    end
    out + ellipsis
  end
end
