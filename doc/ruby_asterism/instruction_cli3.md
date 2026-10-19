# CLI3 指示書: asterism の CLI を ros2 コマンドのように

> 状態: 指示 | 更新: 2026-10-19 | CLI2 (b188176、0.8.1、未公開) で出力は読みやすくなった。ユーザが思い描くのは ros2 コマンドの使いやすさ。「名詞 動詞」の形、Tab 補完、ros2 にある道具 (echo・find・hz・yaml) を足す。0.8.1 は公開せず、この段と合わせて **0.9.0** で出す

前提: asterism (`/home/kishima/fmrb/family-mruby/asterism`、main、b188176)。`exe/asterism`、`lib/asterism/cli.rb`、`lib/asterism/cli/text.rb` (CLI2 の段付け・折り返し・色・差分)、`lib/asterism/cli/*.rb`、`test/test_cli.rb` (`rake test:cli`)、README「The command line」、CHANGELOG (0.8.1 の節)。CLI2 の report は `doc/ruby_asterism/report/cli2.md`。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism の文書は英語。

## 1. 決まっていること (2026-10-19 ユーザ、選択形式)

1. **「名詞 動詞」の形にし、今の形も残す**。今の `nodes` / `objects` / `tree` / `watch` / `ping` / `call` / `mcp` は別名として残す (README の例・MCP・試験が壊れない)。
2. **Tab 補完は網の名前まで**: `asterism completion bash` (と `zsh`) で補完の台本を出す。コマンド名・オプションに加え、ノード・app・オブジェクト・メソッドの名前を網に問い合わせて補完する。
3. **ros2 にある道具を 4 つとも足す**: `status echo` (1 つの app の状態を流す、`--once`)、`find` (メソッド名でオブジェクトを探す)、`hz` (状態の変わる頻度)、`-o yaml` (と `-o json`、今の `--json` は残す)。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

### コマンドの並び (例。細部の名前は ros2 に寄せて決めてよい)

```
asterism node list                 名前だけ 1 行 1 件
asterism node info NODE            1 つを詳しく (種類・説明・apps・objects・状態)
asterism app list                  node/app を 1 行 1 件
asterism app info NODE/APP         状態 (段付け) とオブジェクト
asterism object list [PATTERN]     node/app/object を 1 行 1 件
asterism object info PATH          説明つき (今の objects の 1 件分)
asterism object find METHOD        そのメソッドを持つオブジェクト
asterism status get NODE/APP       今の状態を 1 回 (段付け)
asterism status echo NODE/APP      状態の変化を流す (CLI2 の差分の形、--once で 1 回)
asterism status hz NODE/APP        状態の変わる頻度 (ros2 topic hz のように平均・最小・最大・件数を周期で出す)
asterism call PATH METHOD [ARG...] 今のまま
asterism ping PATH                 今のまま
asterism watch                     今のまま (網全体の出入り)
asterism tree                      今のまま (全体の見取り図)
asterism completion bash|zsh       補完の台本
```

- `list` は**名前だけを 1 行 1 件**で出す (色は付けてよいが、パイプでは素の名前だけ。grep・xargs に渡せる)。並びは名前順。
- `info` は CLI2 の段付け・折り返しを使う。
- 引数が足りないときは、その名詞で使える動詞の一覧 (短い説明つき) を出す (`asterism node` だけで help)。`asterism --help` は名詞ごとにまとめた一覧にし、別名の旧コマンドは末尾に小さく載せる。
- **出力の形式**: `-o yaml` / `-o json` をすべての読むコマンドに。`--json` は `-o json` の別名として残し、今の `--json` の出力は 1 バイトも変えない。YAML は標準ライブラリの `yaml` (Psych) で、`-o json` と同じ中身を出す。

### Tab 補完

- 台本は `asterism completion bash` が標準出力に出す。使い方は README に (`source <(asterism completion bash)` / `~/.bashrc` に足す形)。zsh は `bashcompinit` を使う形でもよい。
- 台本は補完のたびに `asterism __complete WORDS...` (隠しコマンド) を呼び、候補を 1 行 1 件で受け取る。名詞・動詞・オプションは網に触らずに返す。
- 網の名前 (ノード、node/app、node/app/object、メソッド) は、`__complete` が網に問い合わせて返す。**問い合わせは短く切り上げる** (1〜2 秒程度。何も返らなければ候補なし、端末を待たせない)。**結果は少しの間覚えておく** (例: `$XDG_CACHE_HOME/asterism/` に網ごと 10〜30 秒。網の区別は `--router` / `--credentials` / 環境変数から作る。鍵の中身や証明書をキャッシュに書かない)。
- `/` で区切る名前は段ごとに補完する (`ga<Tab>` → `games/`、`games/games/<Tab>` → オブジェクト)。`call PATH <Tab>` はそのオブジェクトのメソッド。
- 網の場所の指定 (`--router` など) が行にあればそれを、無ければ環境変数を使う (今の `network(env)` の決まり)。

### その他

- 依存は増やさない (標準ライブラリだけ)。MCP の道具の名前と中身は変えない (別名を足すのはよい)。
- 版は **0.9.0** に上げる (CHANGELOG の 0.8.1 の節は 0.9.0 にまとめる。0.8.1 は公開していない)。`rake gem` で `pkg/asterism-0.9.0.gem` を作る (公開は親がユーザに頼む)。`pkg/asterism-0.8.1.gem` は消してよい。

## 3. 作るもの

1. 上のコマンドの並び、`-o yaml|json`、`status echo/hz/get`、`object find`、`completion`、`__complete`。
2. 試験: 名詞 動詞の各コマンドの出力 (list は名前だけ、パイプでの素の形)、旧コマンドの別名が前と同じ出力、`--json` が前と同じ、`-o yaml` の中身が `-o json` と同じ、`__complete` の候補 (名詞・動詞・オプション、網の名前の段ごとの補完、網が無いとき切り上げて空)、キャッシュ (期限・網ごと)、`hz` の数え方 (時刻を差し替えて単体試験)、`status echo --once`。
3. bash で実際に補完が効くことを確認する (`bash -i` を擬似端末で動かし Tab を送る、または `COMP_WORDS` / `COMP_CWORD` を作って補完関数を呼ぶ)。zsh があれば zsh も。
4. README「The command line」を名詞 動詞の形で書き直す (list・info・echo・find・hz・-o yaml・補完の入れ方)。CHANGELOG 0.9.0、`lib/asterism/version.rb`。

## 4. 受け入れ条件

1. `rake test` (全体) が通る。
2. 手元の zenohd とゲームホスト (asterism-games の作業コピー、`ASTERISM_GAMES_STATE_DIR` は scratchpad、外の AI の API は呼ばない) に対して、各コマンドの実際の出力 (色なしの写し) と、bash での補完の様子 (候補の写し) を report に貼る。`status echo` と `hz` は人が卓に入る・座ると動くこと。
3. `--json` と旧コマンドの出力が前と同じ。

## 5. 範囲・決まり

- **触ってよいもの**: asterism (main)。fmruby-core は report だけ (ファイルとして書くだけ、git の操作はしない)。
- **触らないもの**: asterism-zenoh、asterism-games・asterism-console のコード (試しに動かすのはよい)、VPS、GitHub の設定、Tab5。
- 手元で親が動かしているもの (zenohd コンテナ `try-zenohd`、`/tmp/try-games` のゲームホスト) には触らない。自分の試験には別の名前・別のポートで起こし、終わったら名前で止めて消す。docker の prune はしない。
- push・タグ・`gem push` はしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする (例のアドレスは 192.0.2.x、例のノード名は架空のもの)。
- **止まる条件**: 1・2 章に無い仕様の選択が要る (例: 旧コマンドの出力を変えないと名詞 動詞の形と両立しない)。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/cli3.md`、常体): コマンドの一覧、出力の例、補完の仕組みと様子、キャッシュの扱い、試験の結果、0.9.0 の gem の状態。
