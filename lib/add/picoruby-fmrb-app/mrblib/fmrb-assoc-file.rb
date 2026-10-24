# FmrbAssoc's table reader for mruby (fmrb-assoc.rb has the rest; the Spinel
# desktop reads with main/prebuild_scripts/spinel/fmrb_assoc_spinel.rb).
#
# Through FmrbToml: a table file with a TOML error is not read at all (one
# log line), and with no user table the system one alone answers. Keys are
# taken in lower case; a value is used when it is a non-empty String (an
# unquoted word such as `rb = run` is one).
module FmrbAssoc
  def self.merge_file(table, path)
    conf = FmrbToml.load(path)
    return nil unless conf
    conf.each do |key, val|
      next unless val.is_a?(String)
      k = key.strip.downcase
      next if k.empty? || val.empty?
      table[k] = val
    end
    nil
  end
end
