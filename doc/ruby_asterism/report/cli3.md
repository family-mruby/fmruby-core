# CLI3 報告: asterism の CLI を ros2 コマンドのように

> 状態: 完了 (検収待ち) | 更新: 2026-10-19 | 指示書 instruction_cli3.md の 3 つ (名詞 動詞の形と旧コマンドの別名、網の名前までの Tab 補完、status echo・object find・status hz・-o yaml) を入れた。旧コマンドの出力と `--json` は前と同じ (実際の出力をバイト単位で比べた)。asterism 0.9.0 (9eca444、main、push・タグ・gem push なし)。`pkg/asterism-0.9.0.gem` は作成済

## 1. 変えたもの

- `lib/asterism/cli.rb`: 命令の表 (`NOUNS` = 名詞と動詞、`COMMANDS` = 名詞の無い命令、`ALIASES` = 旧名)。`--help` と `asterism NOUN` の一覧はこの表から作る。`-o json|yaml` (`--output`) を読む命令すべてに足し、`--json` は `-o json` と同じ旗にした。JSON の出し方 (`JSON.pretty_generate` / 1 行の `JSON.generate`) は前のまま、YAML は同じ値を JSON に通してから `YAML.dump`。
- `lib/asterism/cli/nouns.rb` (新規): node / app / object / status の各動詞、`completion`、隠しコマンド `__complete` とその網への問い合わせ。
- `lib/asterism/cli/complete.rb` (新規): 補完の候補を作る `Complete`、キャッシュ `Complete::Cache`、bash と zsh の台本。
- `lib/asterism/cli/hz.rb` (新規): `status hz` の数え方 (`Hz`、時計を差し替えられる)。
- `lib/asterism/cli/survey.rb`: `tree` に `node:` / `app:` の絞り込みを足した (既定の呼び方は前と同じ経路。MCP は触っていない)。
- `test/test_cli.rb`、README「The command line」(名詞 動詞の形で書き直し、「Tab completion」の項を新設)、CHANGELOG (0.8.1 の節を 0.9.0 にまとめた)、`lib/asterism/version.rb` (0.9.0)、Rakefile の test:cli の説明。
- 依存は増やしていない (`yaml`・`digest`・`shellwords`・`fileutils` は標準ライブラリ)。MCP の道具の名前と中身は変えていない。

## 2. コマンドの一覧

```
asterism node list                  名前だけ 1 行 1 件 (名前順)
asterism node info NODE             プロファイル、apps (状態)、objects (メソッドの数と説明)
asterism app list                   node/app を 1 行 1 件
asterism app info NODE/APP          状態とオブジェクト
asterism object list [PATTERN]      node/app/object を 1 行 1 件 (PATTERN は * と ** を段ごとに)
asterism object info PATH           説明とメソッド (旧 objects の 1 件分と同じ形)
asterism object find METHOD         そのメソッドを持つオブジェクトのパス (* が使える)
asterism status get NODE/APP        今の状態 (段付け)
asterism status echo NODE/APP       状態、続いて変化 (--once で今の状態を 1 回、--duration)
asterism status hz NODE/APP         変わる頻度 (--period 1.0、--window 1000、--duration)
asterism call / ping / watch / tree / mcp      今のまま
asterism completion bash|zsh        補完の台本
旧名: nodes / objects / echo        出力は前と同じ
```

決めた細部 (1・2 章の範囲):

- `asterism node` (動詞なし、または動詞の位置に `--router` などの旗) は動詞の一覧を標準出力に出して終了コード 2。`asterism node -h` と `asterism help node` は 0。知らない動詞は標準エラーに理由と一覧、2。
- `list` / `find` の文字出力は名前だけ。端末では段ごとに色 (ノード・アプリ・オブジェクト)、パイプでは素の名前。何も無ければ何も出さない (終了 0)。
- `node info` / `app info` / `object info` / `status get` で対象が無いときは標準エラーに一言、終了 1。`object info` はワイルドカード不可 (パターンは `object list` / `objects` へ案内)、`app info` と `status *` は `NODE/APP` の 2 段だけ。
- `-o json` の中身: list は名前の配列、`node info` は `tree --json` の 1 要素、`app info` は `{"node","app","status","objects"}`、`object info` は `objects --json` の 1 要素、`object find` は `[{"path","methods":[合うメソッド]}]`、`status get` は状態そのもの。流れるもの (`watch`、`status echo`、`status hz`) は 1 行 1 件の JSON、YAML では 1 件ごとに `---` の文書。`call` / `ping` も `-o yaml` を受ける。`mcp` と `completion` には `-o` が無い。
- `status echo` の文字出力: 最初に時刻・パス・`(now)` と段付けの全体、以降は変わった葉ごとに `時刻 a.b 0→1` / `+a.c=1` / `-a.d` (watch と同じ差分。パスは決まっているので省く)。変化の無い put は何も出さない。`--once` は今の状態を取って 1 回出して終わる (無ければ終了 1)。
- `status hz`: 開始時の状態を基準にし、それと違う put を 1 回の変化と数える (同じ状態の put、たとえば再接続後の put は数えない)。周期 (`--period`) ごとに、前回から変化があったときだけ ros2 topic hz と同じ形で出す。率は窓の間隔の平均の逆数。

## 3. 実際の出力

手元で zenohd (`eclipse/zenoh:1.10.1`、コンテナ名 cli3-zenohd、127.0.0.1:27448) と、asterism-games の HEAD (2bb409c) を `git archive` で scratchpad に取り出したホスト (作業コピーは別のサブエージェントが編集中なので、その変更を巻き込まないため。mahjong1 と othello1、`ASTERISM_GAMES_STATE_DIR` は scratchpad、`ANTHROPIC_API_KEY` / `OPENAI_API_KEY` を外して起動、外の AI の API は呼んでいない) を立て、`ASTERISM_ROUTER=tcp/127.0.0.1:27448` で当てた。出力はパイプ経由 (色なし、80 桁)。後片付け済 (ホストは pid に TERM、コンテナは `docker rm -f cli3-zenohd`)。親の try-zenohd と /tmp/try-games には触っていない。

### asterism --help (冒頭) と asterism node

```
Usage: asterism NOUN VERB [ARG...] [options]    (asterism NOUN: its verbs)

Commands (they only read, except call):
  node list                   the nodes, a name a line
  node info NODE              one node: its profile, applications (with their
                              statuses) and objects
  app list                    the applications, node/app a line
  app info NODE/APP           one application: its status and its objects
  object list [PATTERN]       the objects, node/app/object a line (PATTERN: *
                              and ** for whole parts)
  object info PATH            one object (node/app/object): its description and
                              its methods
  object find METHOD          the objects that have the method (* as a wildcard)
  status get NODE/APP         an application's status, now
  status echo NODE/APP        its status, then each change as it comes (--once:
                              now, once)
  status hz NODE/APP          how often it changes: the rate, the intervals' min
                              / max / std dev

  call PATH METHOD [ARG...]   calls a method; each ARG is JSON, else a Ruby
                              literal, else text
  ...
Older names, kept: nodes (all the nodes in one listing), objects [PATTERN]
(object info of each), echo (ping).
```

```
$ asterism node
Usage: asterism node VERB [ARG...] [options]

  node list                   the nodes, a name a line
  node info NODE              one node: its profile, applications (with their
                              statuses) and objects

asterism node VERB --help: its options; asterism --help: every command
```

### list と find

```
$ asterism node list
games
$ asterism app list
games/games
$ asterism object list
games/games/mahjong1
games/games/othello1
$ asterism object find join
games/games/mahjong1
games/games/othello1
$ asterism object find 're*'
games/games/mahjong1
games/games/othello1
$ asterism app list -o yaml
---
- games/games
```

### info

```
$ asterism node info games
games
  profile: kind=cruby protocol=1 name=asterism-games icon_hash=2a8c0626
  games
    status:
      mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
      othello1: st=wait members=0 humans=0 cpus=0 away=0
    objects:
      mahjong1  18 methods
        A riichi mahjong table (four seats). From a program: call describe
        first, then join, sit, and look / choose in a loop.
      othello1  11 methods
        An othello table (two seats). join, sit, then view / move in a loop.
$ asterism app info games/games
games/games
  status:
    mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
    othello1: st=wait members=0 humans=0 cpus=0 away=0
  objects:
    mahjong1  18 methods
      A riichi mahjong table (four seats). From a program: call describe first,
      then join, sit, and look / choose in a loop.
    othello1  11 methods
      An othello table (two seats). join, sit, then view / move in a loop.
$ asterism object info games/games/othello1
games/games/othello1
  An othello table (two seats). join, sit, then view / move in a loop.
  join(name)              Joins the table as name (or gets this node's
                          membership back). Returns {"token", ...}: every later
                          call brings the token. {"error" => why} when the table
                          is full.
  mine()                  This node's membership ({"token", ...}), or nil when
                          it has not joined.
  ...
  leave(token)            Leaves the table (a seat during a game goes to a CPU).
                          true / false.
```

### status get (-o yaml / -o json)

```
$ asterism status get games/games
mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
othello1: st=wait members=0 humans=0 cpus=0 away=0
$ asterism status get games/games -o yaml
---
mahjong1:
  st: wait
  members: 0
  humans: 0
  cpus: 0
  ais: 0
  away: 0
othello1:
  st: wait
  members: 0
  humans: 0
  cpus: 0
  away: 0
$ asterism status get games/games -o json
{
  "mahjong1": {
    "st": "wait",
    "members": 0,
  ...
```

### status echo と status hz (人が卓に入って座る)

echo (文字と `-o json`) と hz (文字と `-o yaml`) を同時に `--duration 16` で走らせ、別に `asterism-games play mahjong --table games/games/mahjong1 --node tty-cli3 --name Alice --commands sit` (取り出した HEAD) を 5 秒、続いて Bob で 4 秒動かした (いずれも timeout で終わる)。

```
$ asterism status echo games/games
22:00:48.561 games/games (now)
             mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
             othello1: st=wait members=0 humans=0 cpus=0 away=0
22:00:57.777 mahjong1.members 0→1
22:00:57.787 mahjong1.humans 0→1
22:01:02.665 mahjong1.members 1→0
22:01:02.665 mahjong1.humans 1→0
22:01:03.782 mahjong1.members 0→1
22:01:03.792 mahjong1.humans 0→1
```

(Bob が去る分は echo の `--duration` が先に切れたので写っていない。)

```
$ asterism status echo games/games -o json
{"t":"2026-10-19T13:00:48.566Z","path":"games/games","status":{"mahjong1":{"st":"wait","members":0,...},"othello1":{...}},"now":true}
{"t":"2026-10-19T13:00:57.777Z","path":"games/games","status":{"mahjong1":{"st":"wait","members":1,"humans":0,...},...}}
...
$ asterism status echo games/games --once
22:05:52.503 games/games (now)
             mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
             othello1: st=wait members=0 humans=0 cpus=0 away=0
```

```
$ asterism status hz games/games
asterism: games/games: counting the status's changes (Ctrl-C to stop)     (標準エラー)
average rate: 79.667 Hz
  min: 0.013 s  max: 0.013 s  std dev: 0.000 s  window: 1
average rate: 0.409 Hz
  min: 0.013 s  max: 4.876 s  std dev: 2.432 s  window: 2
average rate: 0.665 Hz
  min: 0.010 s  max: 4.876 s  std dev: 1.999 s  window: 4
$ asterism status hz games/games -o yaml
---
t: '2026-10-19T13:00:58.591Z'
path: games/games
rate_hz: 79.6432237524938
min_s: 0.012555996014270931
max_s: 0.012555996014270931
std_s: 0.0
window: 1
count: 2
---
...
```

ゲームのホストは 1 人の入室で members と humans を 13 ms 差の 2 回の put で出すため、最初の窓は 79 Hz と出る。数え方としては正しい (put 単位の頻度)。

### 旧コマンドと --json は前と同じ

同じ網に対し、前 (`git archive` で取り出した b188176 の lib/exe) と後の `nodes` / `nodes --json` / `objects` / `objects --json` / `objects games/games/othello1` / `tree` / `tree --json` (10715 バイト) / `tree --methods` を `cmp` で比べて 8 つとも一致。`nodes -o json` / `objects -o json` / `tree -o json` も前の `--json` と一致。試験の既存の期待値 (`test_commands` の旧コマンドと `--json`) は 1 つも変えずに通る。

## 4. Tab 補完

### 仕組み

- `asterism completion bash` が台本を出す。`source <(asterism completion bash)` (または `~/.bashrc` に同じ行)。zsh は `source <(asterism completion zsh)` を compinit の後に (bashcompinit ではなく zsh の `compadd` で書いた)。
- 台本は Tab のたびに `asterism __complete --bash "${COMP_LINE:0:COMP_POINT}"` を呼ぶ。行をそのまま渡して Ruby 側で `Shellwords` で切る (bash は `COMP_WORDBREAKS` の `:` で `tcp/127.0.0.1:27448` を 3 語に割るので、`COMP_WORDS` からは `--router` の値が取れないため)。候補は 1 行 1 件で、今の語の `:` / `=` より前は bash の流儀に合わせて落とす。zsh は `__complete -- 語...` の形。
- 候補が `/` で終わるときは空白を足さない (`compopt -o nospace`、zsh は `compadd -S ''`)。`--credentials` の後は特別な行 `:files` を返し、台本がファイル名の補完に切り替える。`-o` の後は `json yaml`。
- 名詞・動詞・オプションは命令の表と OptionParser の定義から作り、網に触らない (試験で網への問い合わせが 0 回であることを確認)。
- 網の名前: ノード (`node info`)、`node/app` (`app info`、`status get/echo/hz`)、`node/app/object` (`object list/info`、`objects`、`call`)、`ping` はノードとオブジェクト、`call PATH` と `ping PATH --call` の後はそのオブジェクトのメソッド、`object find` は網にあるメソッド名すべて。`/` の段ごとに補完する。
- 網の場所: 行の `--router` / `--credentials` / `--peer`、無ければ環境変数 (`network(env)` の決まりどおり)。どれも無ければ網には問い合わせず候補なし。
- 問い合わせは別スレッドで接続 (settle 0.6 秒、get 0.6 秒) し、全体で 1.5 秒 (`Complete::BUDGET`) を待って返らなければ候補なしで終える。置き去りの接続は待たずに `Process.exit!` で抜ける。実測: 網があると初回 0.45 秒、キャッシュから 0.11〜0.13 秒。閉じたポート (127.0.0.1:1) と応答の無いアドレス (192.0.2.1) はどちらも 1.6 秒で空を返した。
- 補完の問い合わせも一瞬だけ網のノードになる (`cli-` か証明書の名前)。README に書いた。

### キャッシュ

- `$XDG_CACHE_HOME/asterism/` (未設定なら `~/.cache/asterism/`)。網ごとに 1 ファイル、名前は網の場所 (router、credentials の絶対パス、peer) の SHA-256 の先頭 24 桁。中身は `{"names": {"t", "v"}, "methods games/games/othello1": {...}, "methods": {...}}` で、名前だけ。鍵・証明書の中身も、網の場所そのものも書かない (試験で `.tar` のパスとアドレスが入らないことを確認)。
- 期限 20 秒。期限切れの項目は書き込みのたびに落とす。返らなかった問い合わせは覚えない (網が戻れば次の Tab で取れる)。ディレクトリ 0700、ファイル 0600、一時ファイルから rename で置き換える。

### bash での様子

`ruby` で擬似端末 (PTY) に対話の bash (`--norc -i`、`show-all-if-ambiguous on`) を起こし、`asterism` をこの作業コピーを呼ぶ関数にして台本を source し、Tab を送った。画面の写し (制御符号を除く):

```
$ asterism st<Tab>                 → asterism status 
$ asterism status <Tab>
echo  get   hz
$ asterism status echo ga<Tab>     → asterism status echo games/
                         <Tab>     → asterism status echo games/games 
$ asterism call games/games/<Tab>
games/games/mahjong1  games/games/othello1
$ asterism call games/games/o<Tab> → asterism call games/games/othello1 
$ asterism call games/games/othello1 s<Tab>
sit    stand  start
$ asterism object find re<Tab>
record   records  resign
$ asterism tree -o <Tab>
json  yaml
$ asterism ping <Tab>
games   games/
```

zsh はこの環境に無いので動かしていない (台本は書いて README に載せた。試験は `completion zsh` が `compdef` を含むことだけ)。

## 5. 試験

`rake test` 全体が通る (失敗・エラー・skip 0)。`rake test:cli` は 21 runs / 338 assertions (前は 12 runs / 145)。mcp gem が入っているので test_mcp も走っている。

足した試験 (test/test_cli.rb):

- `test_usage` に: `--help` の名詞ごとの並びと末尾の旧名、`asterism object` (動詞の一覧、2)、旗だけの `asterism status --router ...`、`help node`、知らない動詞、`-o xml` の拒否、`status get games` (2 段でない)、`object info a/*/c` (パターンの拒否)、`completion bash|zsh`。
- `test_hz`: 時計を差し替えて、開始時と同じ状態は数えない、1 回目は間隔なし、同じ状態の put は 1 回、率・最小・最大・標準偏差、前回から変化が無ければ報告なし、窓が古い間隔を落とす、文字の形。
- `test_echo_text`: 最初の全体、変わった葉、変化なしで何も出さない、増えたキーと消えたキー。
- `test_complete_words`: 命令、名詞の前方一致、動詞、`-o` / `--output` の値、`--credentials` のファイル、命令ごとのオプション (`status echo` だけに `--once`、`tree` に `--methods`)、`completion` の値、知らない命令。どれも網への問い合わせ 0 回。
- `test_complete_names`: 網の名前を段ごとに (`ga` → `games/`、`games/` → `games/games`、app は 2 段で止まる)、`call` のメソッド、`ping` のノードとオブジェクト、`ping PATH --call`、`object find`、網が答えないとき候補なし。
- `test_complete_line`: 行の切り方 (末尾の空白、閉じていない引用)、bash の `:` の扱い。
- `test_complete_cache`: 覚える、網ごとに別のファイル、期限 (19 秒は覚えていて 21 秒で問い直す)、nil は覚えない、古い項目を落とす、0600 / 0700、網の場所を書かない、`XDG_CACHE_HOME`。
- `test_noun_verbs` (別プロセスのノードに当てる): 各動詞の文字出力 (list は名前だけ・色なし、info の形、`COLUMNS=40` での app info)、`-o yaml` を `YAML.safe_load` したものが `-o json` と同じ (node info、object info)、`--json` と `-o json` が同じ、無いものは終了 1、`status echo --once` (文字と JSON)、`status echo` の流れ (ノードに状態を 3 回変えさせて `tables 1→2` .. `3→4`) と同時に走らせた `status hz -o json` (count 3、window 2)、`__complete` の網の名前・メソッド・キャッシュのファイル 1 つ・`--bash` の行の形。
- `test_bash_script`: bash の台本の関数を `COMP_LINE` / `COMP_WORDS` / `COMP_CWORD` を作って呼び、候補を確認。
- `test_complete_without_network`: 誰も聞いていないポートに 4 秒未満で空を返し、網の指定が無ければ問い合わせず、返らなかったものはキャッシュに書かない。

既存の試験の期待値は変えていない (旧コマンドと `--json` の出力が前と同じことの確認を兼ねる)。

## 6. 0.9.0 の gem

- `rake gem` で `/home/kishima/fmrb/family-mruby/asterism/pkg/asterism-0.9.0.gem` (162816 バイト、`lib/asterism/cli/complete.rb`・`hz.rb`・`nouns.rb` を含む) を作った。`pkg/asterism-0.8.1.gem` は消した。公開はしていない (`gem push` は親からユーザに依頼)。
- コミット: asterism main の 9eca444「asterism 0.9.0: the command line's commands as a noun and a verb」(CHANGELOG / README / Rakefile / cli.rb / cli/complete.rb / cli/hz.rb / cli/nouns.rb / cli/survey.rb / version.rb / test_cli.rb)。push・タグなし。差分に実在の値 (127.0.0.1・192.0.2.x 以外のアドレス、ドメイン、実在のノード名、鍵) が無いことを確認した。
- asterism-games の Gemfile は触っていない。

## 7. 止まる条件に当たったもの

なし。旧コマンドの出力を変えずに名詞 動詞の形と両立できた (旧コマンドは別の経路のまま残し、新しい動詞は別の関数にした)。
