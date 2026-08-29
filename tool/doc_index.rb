#!/usr/bin/env ruby
# frozen_string_literal: true
#
# doc/README.md の索引部 (INDEX:BEGIN/END の間) を doc/ の実態から再生成する。
# 使い方: rake docs:index  (または ruby tool/doc_index.rb)
#
# 拾う情報:
# - タイトル: 各ファイル先頭の "# " 行
# - 状態行: 先頭 8 行以内の "> 状態: <状態> | 更新: YYYY-MM-DD | <一行要約>"
#   (無ければ状態は "-" と表示する。書式は doc/README.md の規約を参照)

ROOT = File.expand_path("../..", __FILE__)
DOC = File.join(ROOT, "doc")
README = File.join(DOC, "README.md")
BEGIN_MARK = "<!-- INDEX:BEGIN (rake docs:index で生成。手で編集しない) -->"
END_MARK = "<!-- INDEX:END -->"
ENTRY_CANDIDATES = %w[README.md plan.md design.md ideas.md]

def head_lines(path, n = 8)
  lines = []
  File.foreach(path) do |l|
    lines << l
    break if lines.size >= n
  end
  lines
rescue StandardError
  []
end

def title_of(path)
  head_lines(path).each { |l| return Regexp.last_match(1).strip if l =~ /^#\s+(.+)/ }
  File.basename(path)
end

def status_of(path)
  head_lines(path).each do |l|
    next unless l =~ /^>\s*状態:\s*([^|]+?)\s*\|\s*更新:\s*([0-9-]+)\s*(?:\|\s*(.+))?$/

    return { state: Regexp.last_match(1), date: Regexp.last_match(2),
             note: (Regexp.last_match(3) || "").strip }
  end
  nil
end

def entry_file(dir)
  ENTRY_CANDIDATES.each do |c|
    p = File.join(dir, c)
    return p if File.exist?(p)
  end
  Dir[File.join(dir, "*.md")].min || Dir[File.join(dir, "**", "*.md")].min
end

def rel(path)
  path.sub("#{DOC}/", "")
end

out = []

stray = Dir[File.join(DOC, "*.md")].reject { |f| File.basename(f) == "README.md" }
unless stray.empty?
  out << "> **注意: doc 直下に README.md 以外のファイルがある (規約違反。reference/ かテーマへ移すこと):** " \
         "#{stray.map { |f| File.basename(f) }.join(", ")}"
  out << ""
end

out << "## 参照資料 (doc/reference/)"
out << ""
Dir[File.join(DOC, "reference", "*.md")].sort.each do |f|
  st = status_of(f)
  line = "- [#{title_of(f)}](#{rel(f)})"
  line += " — **#{st[:state]}** (#{st[:date]}) #{st[:note]}".rstrip if st
  out << line
end
out << ""

out << "## テーマ別"
out << ""
Dir[File.join(DOC, "*/")].sort.each do |d|
  name = File.basename(d)
  next if name == "archive" || name == "reference"

  entry = entry_file(d)
  next unless entry

  st = status_of(entry)
  n_md = Dir[File.join(d, "**", "*.md")].size
  state = st ? "**#{st[:state]}** (#{st[:date]})" : "-"
  note = st ? st[:note] : ""
  out << "- `#{name}/` [#{title_of(entry)}](#{rel(entry)}) — #{state} #{note} 〔#{n_md} files〕".squeeze(" ").rstrip
end
out << ""

out << "## アーカイブ (完結したテーマ)"
out << ""
Dir[File.join(DOC, "archive", "*/")].sort.each do |d|
  name = File.basename(d)
  entry = entry_file(d)
  title = entry ? title_of(entry) : name
  link = entry ? rel(entry) : "archive/#{name}/"
  out << "- `archive/#{name}/` [#{title}](#{link})"
end

content = File.read(README)
unless content.include?(BEGIN_MARK) && content.include?(END_MARK)
  abort "doc/README.md に索引マーカーが見つからない"
end

new_content = content.sub(
  /#{Regexp.escape(BEGIN_MARK)}.*#{Regexp.escape(END_MARK)}/m,
  "#{BEGIN_MARK}\n\n#{out.join("\n")}\n\n#{END_MARK}"
)
File.write(README, new_content)
puts "doc/README.md updated (#{out.size} lines)"
