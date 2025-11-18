# picoruby-shell 実装分析レポート

## 調査日
2025-01-XX

## 調査目的
picoruby-shellの実装を分析し、fmrb-shellへの移植に必要な情報を整理する。

## 1. アーキテクチャ概要

### 1.1 コンポーネント構成

```
picoruby-shell (mrbgem)
├── Shell           - シェル本体、REPLループ管理
├── Editor          - 行編集、VT100制御
│   ├── Buffer      - テキストバッファ管理
│   └── Line        - 単一行編集
├── Parser          - コマンドライン解析、AST生成
├── Job             - コマンド実行単位
├── Pipeline        - パイプライン、リダイレクト処理
└── Sandbox         - 隔離実行環境（VM task利用）
```

### 1.2 ファイル構成

**コアライブラリ** (`mrblib/`)
- `shell.rb` (371行) - Shell, Job, Pipeline クラス
- `editor.rb` (268行) - Editor, Editor::Buffer, Editor::Line クラス
- `parser.rb` (187行) - Parser クラス
- `init_shell.c` (9行) - C初期化コード

**組み込みコマンド** (`shell_executables/*.rb`)
- `cat.rb` - ファイル内容表示
- `cd.rb` - ディレクトリ移動
- `cp.rb` - ファイルコピー
- `irb.rb` - 対話的Ruby実行
- `ls.rb` - ファイル一覧
- `mv.rb` - ファイル移動
- `pwd.rb` - カレントディレクトリ表示
- `rm.rb` - ファイル削除

### 1.3 依存関係

**重要な依存mrbgem**
1. **picoruby-editor** (必須)
   - Editor, Buffer, Line クラスの実装元
   - VT100エスケープシーケンス制御
   - 行編集機能の中核

2. **picoruby-sandbox** (必須)
   - Sandbox クラスの実装元
   - mruby VM task による隔離実行
   - コマンド実行の安全性確保

3. **picoruby-io-console** (必須)
   - STDIN.read_nonblock() メソッド提供
   - ノンブロッキング入力に必須

4. **picoruby-filesystem-fat** (推奨)
   - Dir, File クラス実装
   - ls, cd, cat 等のファイル操作コマンドに必要

5. **picoruby-require** (推奨)
   - require/load メソッド提供
   - 動的ライブラリロードに使用

## 2. 主要機能の詳細

### 2.1 REPLループ実装

**Shell#run_shell メソッド** ([shell.rb:371-397](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/mrblib/shell.rb#L371-L397))

```ruby
def run_shell
  @editor.start do |editor, buffer, c|
    case c
    when 10, 13  # Enter
      command_line = buffer.dump.chomp.strip
      if !command_line.empty?
        @history.push(command_line) if @history.last != command_line
        @history.shift if @history.length > 10
      end
      editor.crlf
      parser = Parser.new(command_line)
      ast = parser.parse
      execute_ast(ast) if ast  # Job/Pipeline生成・実行
      buffer.clear
      editor.show_prompt(@prompt)
    when 3   # Ctrl-C
      editor.crlf
      buffer.clear
      editor.show_prompt(@prompt)
    when 26  # Ctrl-Z
      Machine.exit(0)
    when 4   # Ctrl-D
      editor.crlf
      Machine.exit(0) if buffer.empty?
    end
  end
end
```

**特徴:**
- Editor#start ブロックでキー入力を処理
- Enterキーでコマンドライン解析→AST生成→実行
- 履歴管理（最大10エントリ）
- Ctrl-C/Ctrl-D/Ctrl-Z のシグナル処理

### 2.2 入力処理とVT100制御

**Editor#start メソッド** ([editor.rb:208-268](../components/picoruby-esp32/picoruby/mrbgems/picoruby-editor/mrblib/editor.rb#L208-L268))

```ruby
def start
  refresh
  loop do
    sleep 0.01
    str = STDIN.read_nonblock(255)
    next unless str
    str.each_char do |c|
      case c
      when 1   # Ctrl-A (行頭移動)
      when 5   # Ctrl-E (行末移動)
      when 27  # ESC (矢印キー、特殊キーシーケンス開始)
        seq = STDIN.read_nonblock(2)
        case seq
        when '[A'  # Up arrow (履歴戻る)
        when '[B'  # Down arrow (履歴進む)
        when '[C'  # Right arrow (カーソル右)
        when '[D'  # Left arrow (カーソル左)
        end
      when 8, 127  # Backspace
        @buffer.backspace
      when 32..126  # 通常の文字
        @buffer.put c
      else
        yield self, @buffer, c.ord  # Shellにキーコード通知
      end
      refresh  # 画面再描画
    end
  rescue Errno::EAGAIN
    # ノンブロッキングなので例外は正常
  end
end
```

**VT100エスケープシーケンス例:**
- `\e[1G` - カーソルを行頭に移動
- `\e[2K` - 行全体をクリア
- `\e[#{n}C` - カーソルをn文字右に移動
- `\e[?25l` / `\e[?25h` - カーソル非表示/表示

### 2.3 コマンドライン解析

**Parser クラス** ([parser.rb](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/mrblib/parser.rb))

**解析フロー:**
```
コマンドライン文字列
  ↓
tokenize() - トークン分割（空白、パイプ、リダイレクト考慮）
  ↓
parse() - AST生成
  ↓
[
  { command: "ls", args: ["-l"], input: nil, output: nil },
  { command: "grep", args: ["foo"], input: :pipe, output: "result.txt" }
]
```

**サポートする構文:**
- パイプ: `command1 | command2`
- リダイレクト: `command > file`, `command < file`
- 複数コマンド: `;` で区切り（実装次第）

**Parser#parse 疑似コード:**
```ruby
def parse
  tokens = tokenize(@input)
  commands = []
  current = { command: nil, args: [], input: nil, output: nil }

  tokens.each do |token|
    case token
    when '|'
      commands << current
      current = { command: nil, args: [], input: :pipe, output: nil }
    when '>'
      # 次のトークンを output に設定
    when '<'
      # 次のトークンを input に設定
    else
      current[:command] ||= token
      current[:args] << token if current[:command]
    end
  end

  commands << current
  commands
end
```

### 2.4 コマンド実行機構

**Job クラス** ([shell.rb:1-180](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/mrblib/shell.rb#L1-L180))

**役割:**
- 単一コマンドの実行管理
- Sandbox による隔離実行
- 標準入出力のリダイレクト処理

**Job#run メソッドの概念:**
```ruby
def run(stdin_data = nil)
  # 1. Sandbox環境でコマンド実行
  sandbox = Sandbox.new

  # 2. 標準入力の設定
  if @input_file
    stdin_data = File.read(@input_file)
  elsif @input == :pipe
    # stdin_data を使用（前のコマンドの出力）
  end

  # 3. コマンド実行
  result = sandbox.evaluate do
    # 組み込みコマンドまたはRubyコード実行
    if builtin_command?(@command)
      execute_builtin(@command, @args)
    else
      eval(@command)
    end
  end

  # 4. 標準出力のリダイレクト
  if @output_file
    File.write(@output_file, result)
  else
    result  # 次のJobまたは画面に出力
  end
end
```

**Sandboxの重要性:**
- mruby VM task で隔離実行
- 例外発生時にシェル本体が停止しない
- メモリリークの局所化

### 2.5 パイプライン処理

**Pipeline クラス** ([shell.rb:181-250](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/mrblib/shell.rb#L181-L250))

**パイプライン実行フロー:**
```ruby
def execute
  output = nil

  @jobs.each do |job|
    output = job.run(output)  # 前のJobの出力を次のJobの入力に
  end

  puts output if output  # 最終出力を表示
end
```

**例: `ls -l | grep rb | wc -l`**
```
Job1: ls -l
  ↓ output1 (ファイル一覧テキスト)
Job2: grep rb (input=:pipe)
  ↓ output2 (rbを含む行のみ)
Job3: wc -l (input=:pipe)
  ↓ output3 (行数カウント)
表示: output3
```

### 2.6 組み込みコマンド実装例

**ls コマンド** ([shell_executables/ls.rb](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/shell_executables/ls.rb))

```ruby
class Ls
  def self.exec(args)
    path = args[0] || "."
    Dir.entries(path).each do |entry|
      next if entry == "." || entry == ".."
      puts entry
    end
  end
end
```

**cat コマンド** ([shell_executables/cat.rb](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/shell_executables/cat.rb))

```ruby
class Cat
  def self.exec(args)
    if args.empty?
      puts "Usage: cat <file>"
      return
    end

    begin
      content = File.read(args[0])
      puts content
    rescue => e
      puts "cat: #{e.message}"
    end
  end
end
```

**IRB コマンド** ([shell_executables/irb.rb](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/shell_executables/irb.rb))

```ruby
class Irb
  def self.exec(args)
    editor = Editor.new(prompt: "irb> ")
    editor.start do |editor, buffer, c|
      case c
      when 10, 13  # Enter
        code = buffer.dump.chomp
        begin
          result = eval(code)
          puts "=> #{result.inspect}"
        rescue => e
          puts "Error: #{e.message}"
        end
        buffer.clear
        editor.show_prompt("irb> ")
      when 4  # Ctrl-D
        editor.crlf
        break  # IRB終了
      end
    end
  end
end
```

## 3. fmrb-shell 移植ガイド

### 3.1 移植の段階的アプローチ

**Phase 1: 最小限REPL（必須機能のみ）**
- Editor の基本機能（行編集、Backspace、Enter）
- 単一コマンド実行（パイプ/リダイレクトなし）
- 2-3個の基本コマンド（ls, cat, pwd）

**Phase 2: 機能拡張**
- 履歴機能（Up/Down矢印キー）
- カーソル移動（Left/Right矢印キー、Ctrl-A/E）
- Ctrl-C による中断

**Phase 3: 高度な機能**
- Parser によるパイプライン解析
- リダイレクト（>, <）
- 組み込みコマンド拡充

**Phase 4: 最適化と統合**
- Sandbox 実装（mruby VM task）
- エラーハンドリング強化
- fmrbカーネルとの統合

### 3.2 依存関係の対応

| picoruby-shell 依存 | fmrb-shell 対応方針 |
|---------------------|---------------------|
| picoruby-editor | **移植必須** - Editor, Buffer, Line クラス |
| picoruby-sandbox | **簡易版実装** - Phase 4で対応、初期は直接eval |
| picoruby-io-console | **FMRB実装** - STDIN.read_nonblock をfmrb_hal経由で実装 |
| picoruby-filesystem-fat | **既存mrbgem** - Dir, File クラスは利用可能 |
| picoruby-require | **オプション** - 動的ロード不要なら省略可 |

### 3.3 FMRB固有の考慮事項

**メモリ管理:**
- Editor::Buffer のサイズ制限（ESP32メモリ制約）
- 履歴エントリ数の調整（picoruby: 10 → fmrb: 5?）
- Sandbox のメモリプール設定

**タスク統合:**
- シェルをどのmrubyタスクで実行するか（kernel? system_app?）
- USB-UART 入力との統合
- 他のタスクとの排他制御

**VT100対応:**
- fmrb_hal でのシリアル出力対応（ESP32: UART, Linux: stdout）
- エスケープシーケンス処理の動作確認

**ファイルシステム:**
- LittleFS との統合（`/flash/` 配下）
- SD カードマウント対応（将来）

### 3.4 簡易版Editorの実装例

**Phase 1向けの最小限Editor:**

```ruby
class FmrbEditor
  def initialize(prompt:)
    @prompt = prompt
    @buffer = ""
    @cursor = 0
  end

  def start
    print @prompt
    loop do
      sleep 0.01
      c = STDIN.read_nonblock(1)
      next unless c

      case c.ord
      when 10, 13  # Enter
        puts ""
        yield self, @buffer, c.ord
        @buffer = ""
        @cursor = 0
        print @prompt
      when 8, 127  # Backspace
        if @cursor > 0
          @buffer = @buffer[0...@cursor-1] + @buffer[@cursor..-1]
          @cursor -= 1
          # VT100: 1文字戻る + カーソルから行末まで削除 + 再描画
          print "\b \b"
        end
      when 32..126  # 通常文字
        @buffer.insert(@cursor, c)
        @cursor += 1
        print c
      else
        yield self, @buffer, c.ord
      end
    rescue Errno::EAGAIN
      # ノンブロッキングなので正常
    end
  end
end
```

### 3.5 簡易版Parserの実装例

**Phase 2向けの基本Parser:**

```ruby
class FmrbParser
  def initialize(input)
    @input = input
  end

  def parse
    return nil if @input.strip.empty?

    tokens = @input.split(/\s+/)
    {
      command: tokens[0],
      args: tokens[1..-1] || []
    }
  end
end
```

**Phase 3向けのパイプライン対応Parser:**

```ruby
class FmrbParser
  def parse
    commands = @input.split('|').map(&:strip)

    commands.map.with_index do |cmd_str, i|
      tokens = cmd_str.split(/\s+/)
      {
        command: tokens[0],
        args: tokens[1..-1] || [],
        input: (i > 0 ? :pipe : nil),
        output: nil  # リダイレクトは後で実装
      }
    end
  end
end
```

## 4. 実装ロードマップ

### 4.1 Phase 1: 最小限REPL（目標: 1週間）

**タスク:**
- [ ] FmrbEditor クラス実装（基本入力、Backspace、Enter）
- [ ] FmrbParser クラス実装（単一コマンド解析のみ）
- [ ] FmrbShell クラス実装（REPLループ）
- [ ] 組み込みコマンド実装（ls, pwd, cat）
- [ ] STDIN.read_nonblock のfmrb_hal実装
- [ ] 動作確認（Linux環境）

**成果物:**
```
fmrb> ls
app  etc  home
fmrb> pwd
/flash
fmrb> cat /flash/etc/config.toml
[system]
version = "0.1.0"
```

### 4.2 Phase 2: 機能拡張（目標: 1-2週間）

**タスク:**
- [ ] 履歴機能（Up/Down矢印キー）
- [ ] カーソル移動（Left/Right矢印キー、Ctrl-A/E）
- [ ] VT100エスケープシーケンス完全対応
- [ ] Ctrl-C による中断処理
- [ ] エラーハンドリング強化
- [ ] 追加コマンド（cd, mkdir, rm）

**成果物:**
- 矢印キーで履歴呼び出し
- Ctrl-C で実行中コマンド中断
- Ctrl-A/E でカーソル移動

### 4.3 Phase 3: 高度な機能（目標: 2-3週間）

**タスク:**
- [ ] Parser パイプライン解析実装
- [ ] Pipeline クラス実装
- [ ] Job クラス実装（Sandboxなし版）
- [ ] リダイレクト対応（>, <）
- [ ] 追加コマンド（cp, mv, grep, irb）
- [ ] ESP32環境での動作確認

**成果物:**
```
fmrb> ls | grep .rb
app.rb
main.rb
fmrb> ls > list.txt
fmrb> cat < input.txt
```

### 4.4 Phase 4: 最適化と統合（目標: 1-2週間）

**タスク:**
- [ ] Sandbox 実装（mruby VM task）
- [ ] メモリプール最適化
- [ ] fmrbカーネルとの統合（Window, Task管理）
- [ ] USB-UART 統合
- [ ] 性能測定・改善
- [ ] ドキュメント整備

**成果物:**
- 隔離実行による安定性向上
- カーネルからのシェル起動
- 製品レベルの品質

## 5. 参考コード抜粋

### 5.1 Shell#execute_ast の実装

```ruby
def execute_ast(ast)
  if ast.is_a?(Array) && ast.length > 1
    # パイプライン実行
    pipeline = Pipeline.new(ast)
    pipeline.execute
  elsif ast.is_a?(Hash)
    # 単一コマンド実行
    job = Job.new(ast)
    output = job.run
    puts output if output
  end
rescue => e
  puts "Error: #{e.message}"
end
```

### 5.2 Editor::Buffer の実装（簡易版）

```ruby
class Editor
  class Buffer
    def initialize
      @content = ""
      @cursor = 0
    end

    def put(c)
      @content.insert(@cursor, c)
      @cursor += 1
    end

    def backspace
      return if @cursor == 0
      @content = @content[0...@cursor-1] + @content[@cursor..-1]
      @cursor -= 1
    end

    def dump
      @content
    end

    def clear
      @content = ""
      @cursor = 0
    end

    def empty?
      @content.empty?
    end
  end
end
```

### 5.3 VT100制御関数例

```ruby
module VT100
  def self.clear_line
    print "\e[2K"
  end

  def self.move_cursor_home
    print "\e[1G"
  end

  def self.move_cursor_right(n)
    print "\e[#{n}C"
  end

  def self.move_cursor_left(n)
    print "\e[#{n}D"
  end

  def self.hide_cursor
    print "\e[?25l"
  end

  def self.show_cursor
    print "\e[?25h"
  end
end
```

## 6. テスト戦略

### 6.1 単体テスト

**Parser テスト:**
```ruby
# test_parser.rb
parser = FmrbParser.new("ls -l /flash")
ast = parser.parse
assert_equal "ls", ast[:command]
assert_equal ["-l", "/flash"], ast[:args]

parser = FmrbParser.new("ls | grep rb")
ast = parser.parse
assert_equal 2, ast.length
assert_equal :pipe, ast[1][:input]
```

**Editor::Buffer テスト:**
```ruby
# test_buffer.rb
buffer = Editor::Buffer.new
buffer.put('a')
buffer.put('b')
assert_equal "ab", buffer.dump

buffer.backspace
assert_equal "a", buffer.dump
```

### 6.2 統合テスト

**REPLフロー テスト:**
```ruby
# test_shell.rb
shell = FmrbShell.new
# シミュレーション: "ls\n" 入力
shell.feed_input("ls\n")
output = shell.get_output
assert_includes output, "app"
assert_includes output, "etc"
```

### 6.3 動作確認シナリオ

**シナリオ1: 基本操作**
```
1. シェル起動
2. "ls" 入力 → ファイル一覧表示
3. "pwd" 入力 → カレントディレクトリ表示
4. "cat /flash/etc/config.toml" 入力 → ファイル内容表示
5. Ctrl-D でシェル終了
```

**シナリオ2: 履歴機能**
```
1. "ls -l" 入力
2. "pwd" 入力
3. Up矢印 → "pwd" 再表示
4. Up矢印 → "ls -l" 再表示
5. Down矢印 → "pwd" 再表示
```

**シナリオ3: パイプライン**
```
1. "ls | grep rb" 入力 → rbを含むファイル名のみ表示
2. "ls -l | grep rb > result.txt" 入力 → ファイルに保存
3. "cat result.txt" 入力 → 保存内容確認
```

## 7. パフォーマンスとメモリ考慮

### 7.1 メモリ使用量見積もり

**Phase 1（最小限REPL）:**
- Editor::Buffer: 256 bytes（入力バッファ）
- 履歴配列: 5エントリ × 256 bytes = 1280 bytes
- Parser 一時変数: 512 bytes
- **合計: 約2KB**

**Phase 3（パイプライン対応）:**
- Pipeline データ: 1KB（最大3コマンド並列想定）
- Job 中間出力: 2KB（コマンド出力バッファ）
- **合計: 約5KB**

**Phase 4（Sandbox対応）:**
- Sandbox VM: POOL_ID_USER_APP0 利用（500KB確保済み）
- **追加メモリ: 不要（既存プール利用）**

### 7.2 パフォーマンス目標

- キー入力応答: < 50ms
- コマンド実行開始: < 100ms
- 画面再描画: < 20ms（VT100制御オーバーヘッド含む）

## 8. まとめ

### 8.1 picoruby-shell の設計思想

1. **モジュラー設計** - Editor, Parser, Job, Pipeline の明確な責務分離
2. **VT100標準** - ターミナルエミュレータとの互換性
3. **Sandbox隔離** - コマンド実行エラーからのシェル保護
4. **拡張性** - 組み込みコマンドの追加が容易

### 8.2 fmrb-shell への移植ポイント

1. **段階的実装** - Phase 1から順に機能追加
2. **依存関係の最小化** - picoruby-editor の必要部分のみ移植
3. **FMRB統合** - fmrb_hal, fmrb_mem との連携
4. **メモリ最適化** - ESP32制約に合わせたバッファサイズ調整

### 8.3 推奨される開発順序

```
Phase 1 (Linux環境) → Phase 2 (Linux環境) → Phase 3 (Linux環境)
                                                  ↓
                                          ESP32移植・最適化
                                                  ↓
                                          Phase 4 (ESP32統合)
```

**理由:**
- Linux環境での開発が高速（ビルド時間短縮）
- デバッグツールが豊富
- Phase 3完了時点でESP32移植すれば、大幅な手戻りを回避

## 9. 参考リンク

- [picoruby-shell mrbgem](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/)
- [picoruby-editor mrbgem](../components/picoruby-esp32/picoruby/mrbgems/picoruby-editor/)
- [picoruby-sandbox mrbgem](../components/picoruby-esp32/picoruby/mrbgems/picoruby-sandbox/)
- [VT100 Escape Sequences](https://vt100.net/docs/vt100-ug/chapter3.html)
- [mruby CI調査レポート](mruby_ci_realloc_investigation_ja.md)

## 10. 更新履歴

- 2025-01-XX: 初版作成（picoruby-shell v0.x.x 調査）
