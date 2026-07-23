# Coverage: i18n.rb-style string tables (Hash constant + symbol keys),
# including UTF-8 (Japanese) values. Checks Spinel's UTF-8 handling matches
# CRuby via output equality.

STRINGS = {
  "en" => {
    shell: "Shell",
    editor: "Editor",
    save: "Save",
    cancel: "Cancel",
    reboot_confirm: "Reboot now?",
  },
  "ja" => {
    shell: "シェル",
    editor: "エディタ",
    save: "保存",
    cancel: "キャンセル",
    reboot_confirm: "再起動しますか?",
  },
}

def t(lang, key)
  tbl = STRINGS[lang] || STRINGS["en"]
  tbl[key] || "?#{key}?"
end

[:shell, :editor, :save, :cancel, :reboot_confirm].each do |k|
  en = t("en", k)
  ja = t("ja", k)
  # byte length differs (UTF-8); print both length and bytesize
  puts "#{k}: en=#{en} (#{en.length}/#{en.bytesize}) ja=#{ja} (#{ja.length}/#{ja.bytesize})"
end

# fallback path
puts "missing: #{t("ja", :nonexistent)}"
puts "unknown lang: #{t("fr", :shell)}"
