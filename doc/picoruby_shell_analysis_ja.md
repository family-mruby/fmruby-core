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

### 3.1 fmrb固有の設計と実装戦略

fmrb-shellは、picoruby-shellとは異なる以下の設計方針を持ちます。

#### 3.1.1 VM分離アーキテクチャ

**シェルの実行環境:**
- シェル自体は **USER_APP** として動作（POOL_ID_USER_APP0-2のいずれか）
- 独立したmruby VMインスタンスを持つ
- メモリプール: 500KB（FMRB_MEM_POOL_SIZE_USER_APP）

**アプリケーション起動:**
```ruby
# picoruby-shell: 同一VM内でSandbox実行
sandbox = Sandbox.new
sandbox.evaluate { load("app.rb") }

# fmrb-shell: 別VMタスクとして起動
FmrbKernel.launch_app("/flash/app/game.rb",
                      pool_id: POOL_ID_USER_APP1,
                      blocking: true)
```

**違い:**
- picoruby-shell: Sandboxで隔離するが同一VM内
- fmrb-shell: 完全に別VMタスク（別メモリプール、別タスク優先度）

#### 3.1.2 ブロッキング/ノンブロッキング実行

**ブロッキング実行（デフォルト）:**
```ruby
# コマンド例: run /flash/app/game.rb
FmrbKernel.launch_app(path, blocking: true)
# アプリ終了までシェルは待機
```

**ノンブロッキング実行（&記法）:**
```ruby
# コマンド例: run /flash/app/music_player.rb &
FmrbKernel.launch_app(path, blocking: false)
# シェルにすぐ戻る、アプリはバックグラウンド実行
```

**実装例:**
```ruby
def execute(line)
  # &記法チェック
  background = line.end_with?(' &')
  line = line.chomp(' &').strip if background

  tokens = line.split(/\s+/)
  cmd = tokens[0]
  args = tokens[1..-1]

  case cmd
  when "run"
    FmrbKernel.launch_app(args[0], blocking: !background)
  end
end
```

#### 3.1.3 標準出力とVM間通信

**標準出力の設定:**
```ruby
# アプリ起動時に標準出力先を指定
FmrbKernel.launch_app(path,
                      blocking: true,
                      stdout: :shell)  # シェルに出力を送信
```

**fmrb_msg経由の出力転送:**
```ruby
# アプリ側（USER_APP1）
puts "Hello from app"  # 内部でfmrb_msgにメッセージ送信

# シェル側（USER_APP0）でメッセージ受信
msg = FmrbMsg.receive(from: TASK_ID_USER_APP1)
if msg[:type] == :stdout
  print msg[:data]  # シェルのコンソールに出力
end
```

**メッセージ構造例:**
```ruby
{
  type: :stdout,
  task_id: 5,  # USER_APP1のタスクID
  data: "Hello from app\n"
}
```

#### 3.1.4 Pipeline（将来実装）

**VM間パイプライン:**
```
fmrb> list_files.rb | filter.rb | counter.rb
```

**実装方針:**
1. 各アプリを別VMタスクとして起動
2. 標準出力をfmrb_msg経由で次のアプリの標準入力に転送
3. 最終出力をシェルに表示

**疑似コード:**
```ruby
def execute_pipeline(commands)
  vms = []

  # 各コマンドをVMとして起動
  commands.each_with_index do |cmd, i|
    vm_id = FmrbKernel.launch_app(cmd[:path],
                                   blocking: false,
                                   pool_id: POOL_ID_USER_APP0 + i)
    vms << vm_id
  end

  # VM間でfmrb_msg転送
  vms.each_cons(2) do |src_vm, dst_vm|
    loop do
      msg = FmrbMsg.receive(from: src_vm, type: :stdout)
      break if msg.nil?
      FmrbMsg.send(to: dst_vm, type: :stdin, data: msg[:data])
    end
  end

  # 最終出力をシェルに表示
  final_output = FmrbMsg.receive(from: vms.last, type: :stdout)
  puts final_output[:data]
end
```

**課題:**
- VM数の制限（USER_APP0-2で最大3個）
- バッファリング戦略
- エラー処理（途中のVMが失敗した場合）

#### 3.1.5 Sandbox互換層

picoruby-shellとの互換性のため、Sandboxインタフェースを維持しつつ、内部でFmrbKernelを呼び出す。

**互換層実装例:**
```ruby
class Sandbox
  def initialize
    @vm_id = nil
  end

  def evaluate(&block)
    # ブロック内のコードを一時ファイルに保存
    tmp_file = "/flash/tmp/sandbox_#{Time.now.to_i}.rb"
    File.write(tmp_file, block.source)

    # 別VMで実行
    result = FmrbKernel.launch_app(tmp_file,
                                    blocking: true,
                                    stdout: :return)

    # 一時ファイル削除
    File.delete(tmp_file)

    result
  end
end
```

**利点:**
- picoruby-shellのコードをそのまま移植可能
- 段階的な移行が可能
- テストコードの互換性

**制約:**
- ブロック内のコードをファイル化する必要がある
- クロージャの変数キャプチャは不可
- パフォーマンスオーバーヘッド（ファイルI/O）

#### 3.1.6 実装の優先順位

**Phase 1（最小構成）:**
- ✓ 基本REPL（cat, write, run）
- ✓ &記法によるバックグラウンド実行
- ✗ Pipeline（後回し）
- ✗ Sandbox互換層（後回し）
- ✗ fmrb_msg統合（後回し、初期はFmrbKernelのみ）

**Phase 2（VM間通信）:**
- ✓ fmrb_msg経由の標準出力受信
- ✓ アプリ出力をシェルに表示
- ✓ ps/kill コマンド（実行中アプリ管理）

**Phase 3（高度な機能）:**
- ✓ VM間Pipeline
- ✓ Sandbox互換層
- ✓ リダイレクト（ファイル ⇔ VM）

### 3.2 移植の段階的アプローチ（従来のpicoruby-shell参考）

picoruby-shellの段階的実装アプローチ（参考）。fmrb-shellでは3.1の設計方針を優先。

**Phase 1: 最小限REPL（必須機能のみ）**
- Editor の基本機能（行編集、Backspace、Enter）
- 単一コマンド実行（パイプ/リダイレクトなし）
- 基本コマンド（cat, write, run）
- **fmrb追加**: &記法によるバックグラウンド実行

**Phase 2: 機能拡張**
- 履歴機能（Up/Down矢印キー）
- カーソル移動（Left/Right矢印キー、Ctrl-A/E）
- Ctrl-C による中断
- **fmrb追加**: fmrb_msg経由の標準出力受信
- **fmrb追加**: ps/kill コマンド

**Phase 3: 高度な機能**
- Parser によるパイプライン解析
- **fmrb変更**: VM間パイプライン（fmrb_msg経由）
- リダイレクト（ファイル ⇔ VM）
- 組み込みコマンド拡充

**Phase 4: 最適化と統合**
- **fmrb変更**: Sandbox互換層実装（FmrbKernel経由）
- エラーハンドリング強化
- fmrbカーネルとの統合

### 3.3 依存関係の対応

| picoruby-shell 依存 | fmrb-shell 対応方針 |
|---------------------|---------------------|
| picoruby-editor | **移植必須** - Editor, Buffer, Line クラス |
| picoruby-sandbox | **互換層実装** - Phase 4で対応、内部でFmrbKernel使用（3.1.5参照） |
| picoruby-io-console | **FMRB実装** - STDIN.read_nonblock をfmrb_hal経由で実装 |
| picoruby-filesystem-fat | **既存mrbgem** - Dir, File クラスは利用可能 |
| picoruby-require | **オプション** - 動的ロード不要なら省略可 |

### 3.4 FMRB固有の考慮事項

**メモリ管理:**
- Editor::Buffer のサイズ制限（ESP32メモリ制約）
- 履歴エントリ数の調整（picoruby: 10 → fmrb: 5?）
- シェル用メモリプール: USER_APP（500KB）

**タスク統合:**
- シェルはUSER_APPとして実行（3.1.1参照）
- USB-UART 入力との統合
- 他のタスクとの排他制御（fmrb_msg使用）

**VT100対応:**
- fmrb_hal でのシリアル出力対応（ESP32: UART, Linux: stdout）
- エスケープシーケンス処理の動作確認

**ファイルシステム:**
- LittleFS との統合（`/flash/` 配下）
- SD カードマウント対応（将来）

**VM間通信:**
- fmrb_msg経由の標準出力転送（3.1.3参照）
- パイプライン実装時のバッファリング戦略

### 3.5 簡易版Editorの実装例

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

### 3.6 簡易版Parserの実装例

**Phase 1向け: Parser不要（推奨）**

```ruby
class FmrbShell
  def execute(line)
    return if line.strip.empty?

    # &記法チェック
    background = line.end_with?(' &')
    line = line.chomp(' &').strip if background

    # 単純なsplitで十分
    tokens = line.split(/\s+/)
    cmd = tokens[0]
    args = tokens[1..-1] || []

    # コマンド実行
    case cmd
    when "cat"
      puts File.read(args[0])
    when "write"
      File.write(args[0], args[1..-1].join(' '))
    when "run"
      FmrbKernel.launch_app(args[0], blocking: !background)
    else
      puts "#{cmd}: command not found"
    end
  rescue => e
    puts "Error: #{e.message}"
  end
end
```

**Phase 2向け: Parserクラス化（必要に応じて）**

```ruby
class FmrbParser
  def initialize(input)
    @input = input
  end

  def parse
    return nil if @input.strip.empty?

    # &記法チェック
    background = @input.end_with?(' &')
    input = background ? @input.chomp(' &').strip : @input

    tokens = input.split(/\s+/)
    {
      command: tokens[0],
      args: tokens[1..-1] || [],
      background: background
    }
  end
end
```

**Phase 3向け: パイプライン対応**

VM間パイプライン前提（3.1.4参照）

```ruby
class FmrbParser
  def parse
    return nil if @input.strip.empty?

    # &記法チェック
    background = @input.end_with?(' &')
    input = background ? @input.chomp(' &').strip : @input

    # パイプラインで分割
    commands = input.split('|').map(&:strip)

    # 各コマンドをパース
    result = commands.map.with_index do |cmd_str, i|
      tokens = cmd_str.split(/\s+/)
      {
        command: tokens[0],
        args: tokens[1..-1] || [],
        input: (i > 0 ? :pipe : nil),
        output: (i < commands.length - 1 ? :pipe : nil),
        pool_id: POOL_ID_USER_APP0 + i  # VM分離
      }
    end

    { commands: result, background: background }
  end
end
```

**設計のポイント:**
- **Phase 1**: Parser不要、直接split → case文（メモリ節約）
- **Phase 2**: &記法対応、Hashで返却（拡張性）
- **Phase 3**: パイプライン対応、VM割り当て管理

## 4. 実装ロードマップ（fmrb-shell版）

### 4.1 Phase 1: 最小限REPL（目標: 2-3日）

**タスク:**
- [ ] FmrbEditor クラス実装（基本入力、Backspace、Enter）
- [ ] Parser不要 - execute()で直接split（3.6参照）
- [ ] FmrbShell クラス実装（REPLループ）
- [ ] 組み込みコマンド実装（cat, write, run）
- [ ] &記法対応（バックグラウンド実行）
- [ ] STDIN.read_nonblock のfmrb_hal実装
- [ ] 動作確認（Linux環境）

**成果物:**
```
fmrb> cat /flash/etc/config.toml
[system]
version = "0.1.0"

fmrb> write /flash/home/test.txt Hello World

fmrb> run /flash/app/game.rb
(ゲーム実行、終了まで待機)

fmrb> run /flash/app/music.rb &
(バックグラウンド実行、シェルにすぐ戻る)
```

**メモリ使用量:** 約400 bytes（Phase 1のみ）

### 4.2 Phase 2: VM間通信と機能拡張（目標: 1-2週間）

**タスク:**
- [ ] fmrb_msg統合（アプリ標準出力受信）
- [ ] アプリ出力をシェルに表示
- [ ] ps コマンド（実行中アプリ一覧）
- [ ] kill コマンド（アプリ終了）
- [ ] 履歴機能（Up/Down矢印キー）
- [ ] カーソル移動（Left/Right矢印キー、Ctrl-A/E）
- [ ] VT100エスケープシーケンス完全対応
- [ ] Ctrl-C による中断処理

**成果物:**
```
fmrb> run app.rb &
App started (VM ID: 1)

fmrb> ps
VM ID  Status   Command
1      Running  /flash/app/app.rb

fmrb> kill 1
App terminated
```

**fmrb_msg統合:**
- アプリの `puts` 出力をシェルで表示
- 矢印キーで履歴呼び出し
- Ctrl-C で中断

### 4.3 Phase 3: VM間パイプライン（目標: 2-3週間）

**タスク:**
- [ ] FmrbParser パイプライン解析実装（3.6参照）
- [ ] VM間Pipeline実装（3.1.4参照）
- [ ] fmrb_msg経由のstdin転送
- [ ] リダイレクト対応（ファイル ⇔ VM）
- [ ] 追加コマンド（grep相当のRubyスクリプト）
- [ ] ESP32環境での動作確認

**成果物:**
```
fmrb> list_files.rb | filter.rb | count.rb
(各スクリプトが別VMで実行、出力が次のVMに転送される)
Result: 42 files

fmrb> generate_data.rb > data.txt
(VM出力をファイルにリダイレクト)

fmrb> process.rb < input.txt
(ファイルをVMの標準入力に)
```

**課題対応:**
- VM数制限（最大3個）の管理
- バッファリング戦略
- エラーハンドリング（途中のVM失敗時）

### 4.4 Phase 4: Sandbox互換層と統合（目標: 1-2週間）

**タスク:**
- [ ] Sandbox互換層実装（3.1.5参照）
- [ ] メモリプール最適化
- [ ] fmrbカーネルとの統合（Window, Task管理）
- [ ] USB-UART 統合
- [ ] 性能測定・改善
- [ ] ドキュメント整備

**成果物:**
```ruby
# picoruby-shellのコードがそのまま動作
sandbox = Sandbox.new
result = sandbox.evaluate { 1 + 1 }
puts result  # => 2
```

**Sandbox互換層:**
- 内部でFmrbKernel経由で別VM実行
- 一時ファイル経由でコード転送
- 戻り値の取得

**統合完了:**
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

### 8.2 fmrb-shell への移植ポイント（主要な違い）

1. **VM分離アーキテクチャ（3.1参照）**
   - シェル: USER_APPとして動作
   - アプリ: 別VMタスクとして起動（FmrbKernel経由）
   - picoruby-shellとの最大の違い

2. **段階的実装の最適化**
   - Phase 1: Parser不要（直接split）→ 実装期間2-3日
   - Phase 2: fmrb_msg統合（VM間通信）
   - Phase 3: VM間パイプライン（将来機能）

3. **依存関係の最小化**
   - picoruby-editor の必要部分のみ移植
   - Sandbox互換層（Phase 4、オプション）

4. **FMRB統合**
   - fmrb_hal, fmrb_mem, fmrb_msg との連携
   - FmrbKernel API使用（launch_app, start_app）

5. **メモリ最適化**
   - Phase 1: 約400 bytes（Parser不要）
   - ESP32制約に合わせたバッファサイズ調整

### 8.3 推奨される開発順序

```
Phase 1 (Linux環境, 2-3日)
  ↓
Phase 2 (Linux環境, 1-2週間) - fmrb_msg統合
  ↓
ESP32移植・動作確認
  ↓
Phase 3 (ESP32/Linux, 2-3週間) - VM間パイプライン
  ↓
Phase 4 (オプション, 1-2週間) - Sandbox互換層
```

**理由:**
- Linux環境での開発が高速（ビルド時間短縮）
- デバッグツールが豊富
- Phase 2完了時点でESP32移植すれば、早期に動作確認可能
- Phase 3以降はLinux/ESP32並行開発

**重要な設計判断:**
- Pipelineは将来実装（VM間fmrb_msg前提）
- Sandboxは互換層として実装（既存コード移植のため）
- 最小構成（Phase 1）で素早く動作させる

## 9. 参考リンク

- [picoruby-shell mrbgem](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/)
- [picoruby-editor mrbgem](../components/picoruby-esp32/picoruby/mrbgems/picoruby-editor/)
- [picoruby-sandbox mrbgem](../components/picoruby-esp32/picoruby/mrbgems/picoruby-sandbox/)
- [VT100 Escape Sequences](https://vt100.net/docs/vt100-ug/chapter3.html)
- [mruby CI調査レポート](mruby_ci_realloc_investigation_ja.md)

## 10. 更新履歴

- 2025-01-XX: 初版作成（picoruby-shell v0.x.x 調査）
- 2025-01-XX: fmrb固有設計を追加（3.1節）
  - VM分離アーキテクチャ
  - &記法によるブロッキング/ノンブロッキング実行
  - fmrb_msg経由のVM間通信
  - VM間パイプライン設計
  - Sandbox互換層
  - Parser簡略化（Phase 1ではsplitのみ）
  - ロードマップ再編（2-3日で動作可能なPhase 1）
