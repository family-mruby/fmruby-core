# frozen_string_literal: true
#
# mini_toml.rb - FmrbToml's C half (tomlc99 + toml_walk.c) for the stand-in:
# FmrbToml._parse(text) -> Hash, or the error's text. Enough TOML for the
# files the apps read (system_conf.toml, .app.toml, the apps' settings):
# tables, arrays of tables, strings (basic and literal), integers (hex too),
# floats, booleans, arrays and inline tables. A date or a bare word comes
# back as its text, as FmrbToml says.
module Regress
  module MiniToml
    module_function

    def parse(text)
      root = {}
      cur = root
      text.each_line.with_index(1) do |raw, no|
        line = strip_comment(raw).strip
        next if line.empty?
        if line.start_with?("[[")
          name = line[2..].sub(/\]\]\z/, "").strip
          parent, last = dig_parent(root, name)
          parent[last] ||= []
          return "line #{no}: #{last} is not an array of tables" unless parent[last].is_a?(Array)
          cur = {}
          parent[last] << cur
        elsif line.start_with?("[")
          name = line[1..].sub(/\]\z/, "").strip
          parent, last = dig_parent(root, name)
          parent[last] ||= {}
          cur = parent[last]
        else
          k, v = line.split("=", 2)
          return "line #{no}: expected key = value" if v.nil?
          val, rest = value(v.strip)
          return "line #{no}: bad value" if val.equal?(BAD) || !rest.strip.empty?
          keys = k.strip.split(".").map { |x| unquote(x.strip) }
          h = cur
          keys[0..-2].each { |kk| h = (h[kk] ||= {}) }
          h[keys[-1]] = val
        end
      end
      root
    rescue StandardError => e
      e.message
    end

    BAD = Object.new

    def unquote(s) = s.start_with?('"') || s.start_with?("'") ? s[1..-2] : s

    def dig_parent(root, name)
      parts = name.split(".").map { |x| unquote(x.strip) }
      h = root
      parts[0..-2].each do |p|
        h[p] ||= {}
        h = h[p].is_a?(Array) ? h[p].last : h[p]
      end
      [h, parts[-1]]
    end

    def strip_comment(line)
      out = +""
      q = nil
      line.each_char do |ch|
        if q
          q = nil if ch == q
        elsif ch == '"' || ch == "'"
          q = ch
        elsif ch == "#"
          break
        end
        out << ch
      end
      out
    end

    # [value, rest of the text]
    def value(s)
      case s
      when /\A"((?:[^"\\]|\\.)*)"/
        body = Regexp.last_match(1)
        rest = $'
        [body.gsub(/\\(u[0-9a-fA-F]{4}|.)/) { esc(Regexp.last_match(1)) }, rest]
      when /\A'([^']*)'/ then [Regexp.last_match(1), $']
      when /\Atrue\b/ then [true, $']
      when /\Afalse\b/ then [false, $']
      when /\A0x([0-9a-fA-F_]+)/ then [Regexp.last_match(1).delete("_").to_i(16), $']
      when /\A[+-]?\d[\d_]*\.\d[\d_]*(?:[eE][+-]?\d+)?/, /\A[+-]?\d[\d_]*[eE][+-]?\d+/ then [Regexp.last_match(0).delete("_").to_f, $']
      when /\A\d{4}-\d\d-\d\d[^\s,\]}]*/ then [Regexp.last_match(0), $']
      when /\A[+-]?\d[\d_]*/ then [Regexp.last_match(0).delete("_").to_i, $']
      when /\A\[/ then array(s[1..])
      when /\A\{/ then inline(s[1..])
      when /\A[A-Za-z_][\w-]*/ then [Regexp.last_match(0), $']
      else [BAD, s]
      end
    end

    def esc(e)
      case e
      when "n" then "\n"
      when "t" then "\t"
      when "\\" then "\\"
      when '"' then '"'
      else e.start_with?("u") ? [e[1..].to_i(16)].pack("U") : e
      end
    end

    def array(s)
      out = []
      s = s.lstrip
      loop do
        s = s.sub(/\A(\s|#[^\n]*\n)+/, "")
        return [out, s[1..]] if s.start_with?("]")
        v, s = value(s)
        return [BAD, s] if v.equal?(BAD)
        out << v
        s = s.sub(/\A(\s|#[^\n]*\n)+/, "")
        s = s[1..] if s.start_with?(",")
      end
    end

    def inline(s)
      out = {}
      s = s.lstrip
      loop do
        s = s.lstrip
        return [out, s[1..]] if s.start_with?("}")
        k, s = s.split("=", 2)
        v, s = value(s.lstrip)
        return [BAD, s] if v.equal?(BAD)
        out[unquote(k.strip)] = v
        s = s.lstrip
        s = s[1..] if s.start_with?(",")
      end
    end
  end
end

module FmrbToml
  def self._parse(text) = Regress::MiniToml.parse(text)
end
