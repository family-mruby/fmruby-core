# CLI4 報告: 流れている値を MCP と CLI で読む

> 状態: 完了 (検収待ち) | 更新: 2026-10-20 | 指示書 instruction_cli4.md のとおり、MCP に道具 `asterism_values`、CLI に `object get` / `object echo` を足した。既存の道具・コマンド・`--json` の出力は前と同じ (実際の出力をバイト単位で比べた)。asterism 0.10.0 (2a039b8、main、push・タグ・gem push なし)。`pkg/asterism-0.10.0.gem` は作成済

## 1. 変えたもの

- `lib/asterism/cli/values.rb` (新規): MCP と CLI の共通部分。型の検査 (`check_pattern!`)、値の読み方 (`decode`)、put 1 件の形 (`entry`)、覚える箱 `Values::Store` (上限・忘れ方・待ち・去った app の印・切り詰め・答えの形)。
- `lib/asterism/cli/mcp.rb`: 道具 `asterism_values`。server の起動時 (`run`、transport を開く前) に `asterism/**` を subscribe し (depth 1024)、`asterism/*/*` の liveliness を見て去った app の値に印を付ける。server の instructions に 1 文足した (asterism_values の紹介。道具の出力ではない)。
- `lib/asterism/cli/nouns.rb`: `cmd_object_get` / `cmd_object_echo` と、その下回り (`put_pattern!`、`nothing_came`、`put_lines`)。
- `lib/asterism/cli.rb`: `NOUNS["object"]` に `get` / `echo`、`object get` の `--timeout` (既定 5.0、ほかの命令の `--timeout` 2.0 とは別の値)、`object echo` の `--once` / `--raw` / `--duration`。
- `lib/asterism/cli/complete.rb`: `object get` / `object echo` の PATH を `node/app/object` の段ごとに補完 (KEY は網に問わず候補なし)。
- `test/test_cli.rb`、README (コマンドの一覧と「object get / echo」の項、MCP の表と asterism_values の項、オセロの盤面を AI に見せる例)、CHANGELOG 0.10.0、`lib/asterism/version.rb` (0.10.0)。依存は増やしていない。

## 2. 道具とコマンドの形

### MCP `asterism_values`

- 引数: `pattern` (文字列、`node/app/object/more`、`*` は 1 段、`**` は何段でも、既定 `**`)、`wait` (秒、0〜30、既定 0)。段は名前・`*`・`**` のどれか (`oth*` のような段の一部の `*`、空の段、`@` で始まる段は誤りとして isError で返す)。`node/app/object` だけでは put のキーに合わない (put のキーは object の後に段がある) ので、説明に `games/games/othello1/**` か `games/*/*/state` の形を書いた。
- 答え: `{"since", "values": [{"key", "t", "age_s", "app", "bytes", "format", "value"(, "truncated")}]}`。`key` は `asterism/` を除いた形、`values` はキーの名前順。`since` は覚え始めた時刻。必要なときだけ `"more"` (答えに載せなかったキーの数)、`"forgotten"` (上限で忘れたキーの数)、`"note"` (何も無かったとき)。
- `app`: `alive` / `gone` (今その app が網に居ない。値は最後のもので今の様子ではない) / `restarted` (この値の後に一度去って戻った)。
- `value`: MessagePack (Asterism の形) ならそのまま、そうでなければ JSON として読めればそれ、UTF-8 の文字列なら文字列、どれでもなければ `"(n bytes)"`。`format` がどれで読んだかを言う (`msgpack` / `json` / `text` / `bytes` / `empty` / `dropped`)。値の中のバイナリは `"(n bytes)"`、NaN などは文字列。
- `wait`: 型に合う値が 1 つでもあればすぐ答える。無ければ最初の合う put まで最大 `wait` 秒待つ。切れたら `values: []` と `note` (「server の起動後に put されていない (put は値が変わったときだけ来る) か、ルータがこの server に読ませていない」)。
- description (AI が読む) には、put は変わったときだけ来ること、覚えているのは server を起動してから受け取った分だけであること (`since`)、`wait` で待てること、`app` の意味、切り詰めと上限、`@status` は asterism_status の担当であることを書いた。

### CLI

```
asterism object get  PATH [KEY]   次の put を 1 件出して終わる (--timeout 5.0)
asterism object echo PATH [KEY]   put を流す (--once: 最初の 1 件、--raw: 毎回全体、--duration)
```

- PATH は `node/app/object` (段に `*` 可)、KEY は残り (`*`・`**` 可、既定 `**`)。合わせて 1 つの型にして `asterism/<PATH>/<KEY>` を subscribe する。
- 文字出力: 1 行目に受け取った時刻とキー、値が Hash なら `status get` と同じ段付けの `key=value`。`echo` は 2 回目以降、そのキーの前回から変わった葉だけを `時刻 キー a.b 0→1` / `+a.c=…` / `-a.d` で出す (`status echo` と同じ `Text.status_diff`)。Hash でない値は変わったときに 1 行で全体。同じ値の put は何も出さない。`--raw` は毎回全体。
- `-o json` / `-o yaml`: put 1 件を `{"key", "t", "bytes", "format", "value"}` で (MCP の 1 件から `age_s` と `app` を除いた形。`entry` を共通に使う)。`get` は整形した JSON、`echo` は 1 行 1 件 (YAML は 1 件ごとに `---`)。`echo -o json` は差分にせず毎回全体 (`status echo -o json` に合わせた)。
- 何も来なかったとき: 標準エラーに `asterism: nothing came on <型> in <秒> s (the router does not let this node read it, or it has not changed)`。`get` は終了 1。`echo` は `--duration` が切れたときに同じ文を出し、`--once` なら終了 1、そうでなければ 0。
- PATH が 3 段でない・段の一部に `*`・`--timeout 0` は終了 2。

決めた細部 (1・2 章の範囲): 道具の名前は `asterism_values` (既存の `asterism_nodes` / `asterism_objects` / `asterism_status` に合わせた)。`object get` の既定の待ちは 5 秒。`app` の印は `restarted` まで分けた (「去った app の印」を、戻ってきた app の古い値にも効かせるため)。`echo -o json` は毎回全体。

## 3. 覚え方と上限

- 覚えるのは `asterism/**` に来た put (verbatim な `@profile` / `@status` は `asterism/**` に合わないので入らない。docs/protocol.md 2.2)。キーごとに最後の値・受け取った時刻 (壁時計と単調時計)・バイト数・印を 1 件。put を受け取った時点で読み (decode) を済ませて持つ。
- 上限 (`Values::Store` の定数):
  - キーの数 1000、payload の合計 16 MiB。超えたら受け取ったのが一番古いキーから忘れる (同じキーに新しい put が来るとそのキーは一番新しくなる)。忘れた数は答えの `forgotten`。
  - 1 つの put が 256 KiB を超えたら読まずに大きさだけ覚える (`format: "dropped"`、`truncated: true`、値は `"(n bytes: too large to keep)"`。合計には数えない)。
  - 答えでは、値の JSON が 8192 バイトを超えたら JSON の文字列の先頭 8192 バイト (文字の途中で切らない) に `…` を付けて `value` にし、`truncated: true`。
  - 1 回の答えに載せるキーは 100 まで (残りの数は `more`)。`wait` は 30 秒まで (負は 0)。
- 去った app: liveliness の `asterism/<node>/<app>` が消えたら、そのキーの値に「去った」印を付ける (値は消さない)。答えるときに今の liveliness (`Asterism.apps`) を見て、居なければ `gone`、居て印があれば `restarted`、印が無ければ `alive`。新しい put が来ると印は消える。
- 鍵・秘密: put の値をそのまま返す。ゲームホストの卓の state (`Othello::Table#public_view`) の中身は v / t / st / nm / sk / aw / ow / mb (名前・席・印) / ts / bd / tn / n / last / lg / cnt / dl / res で、token もノード ID も入っていないことをコードで確認し、実際の確認の記録 (下) にも `token` の文字列が 0 件であることを確認した。

## 4. 確認 (手元の zenohd とゲームホスト)

手元で zenohd (`eclipse/zenoh:1.10.1`、コンテナ名 cli4-zenohd、127.0.0.1:27449) を起こし、asterism-games の HEAD (f699f98) を `git archive` で scratchpad に取り出して othello1 だけのホストを立てた (node `games`、`ASTERISM_GAMES_STATE_DIR` と state_dir は scratchpad、`ANTHROPIC_API_KEY` / `OPENAI_API_KEY` を外して起動、外の AI の API は呼んでいない)。ユーザの `~/asterism_keys/` は使っていない (VPS にはつないでいない)。MCP は stdio の `asterism mcp` (この作業コピー) に JSON-RPC の行を送る小さな台本で叩いた。オセロは `asterism-games play othello --table games/games/othello1 --name Alice --node tty-cli4 --commands "sit,cpu,start,auto" --until-over` で人 (auto) 対 CPU を 1 局 (CPU が黒、41-23 で黒の勝ち)。後片付け済 (ホストは ruby の pid に TERM、`docker rm -f cli4-zenohd`)。

途中で一度、ホストを `cd ... && ... &` で起こして `$!` がサブシェルの pid になり、TERM がホストに届かず同じ node/app のホストが 2 つ動いた (最初の 1 局の後に起こした 2 つ目)。ps で見つけて 2 つとも pid で止め、以降はホストを直接 `&` で起こして記録を取り直した。下の写しはすべて取り直した方 (ホスト 1 つ)。

### 起動直後と静かな卓

```
→ asterism_values {}
{"since":"2026-10-20T02:16:50.191Z","values":[],"note":"nothing came for **: it has not been put since this server started (puts come only when a value changes), or the router does not let this server read it"}
→ asterism_values {"pattern":"games/games/othello1/state","wait":2}
{"since":"2026-10-20T02:16:50.191Z","values":[{"key":"games/games/othello1/state","t":"2026-10-20T02:16:51.858Z","age_s":0.0,"app":"alive","bytes":51,"format":"msgpack","value":{"v":1,"t":"othello1","st":"wait","nm":["",""],"sk":[0,0],"aw":0,"ow":-1,"mb":[],"ts":0}}]}
```

(2 回目は、待っている間にホストが卓の state を put したので、それを受け取って答えた。前の回 (ホストを立てて時間が経っていた回) では同じ呼び出しが 2 秒で切れて `"nothing came for games/games/othello1/state in 2 s: ..."` と答えた。)

### 対局中の盤面 (MCP)

```
→ asterism_values {"pattern":"games/games/othello1/**","wait":10}
{"since":"2026-10-20T02:16:50.191Z","values":[{"key":"games/games/othello1/state","t":"2026-10-20T02:16:58.583Z","age_s":0.4,"app":"alive","bytes":181,"format":"msgpack","value":{"v":15,"t":"othello1","st":"play","nm":["CPU","Alice"],"sk":[2,1],"aw":0,"ow":1,"mb":[["Alice",1,2]],"ts":0,"bd":"..W.......W.......WBWWW...WBB....W.BBB..........................","tn":0,"n":10,"last":"c1","lg":"b1b2e2f2g2b3h3b4a6","cnt":[6,8]}}]}
```

`bd` を 8x8 に並べたもの (a1 から、B 黒 = CPU、W 白 = Alice。10 手目 c1 の後、黒の番、6 対 8):

```
    a b c d e f g h
  1 . . W . . . . .
  2 . . W . . . . .
  3 . . W B W W W .
  4 . . W B B . . .
  5 . W . B B B . .
  6 . . . . . . . .
  7 . . . . . . . .
  8 . . . . . . . .
```

### 終局の結果 (MCP)

```
→ asterism_values {"pattern":"games/games/othello1/state"}
{"since":"2026-10-20T02:16:50.191Z","values":[{"key":"games/games/othello1/state","t":"2026-10-20T02:17:16.766Z","age_s":1.3,"app":"alive","bytes":176,"format":"msgpack","value":{"v":66,"t":"othello1","st":"over","nm":["CPU",""],"sk":[2,0],"aw":0,"ow":-1,"mb":[],"ts":0,"bd":"BBBBBBBBBBWBWWWBBWBBWWBBBBWBWBWWBWBBWBWWBWWBBBWWBBBWWBBWBBBBWBBB","tn":-1,"n":61,"last":"g8","lg":"","cnt":[41,23],"res":{"w":0,"why":"count","b":41,"wc":23}}}]}
```

`res`: 席 0 (黒、CPU) の勝ち、数えて 41 対 23。最後の put は終局の後に Alice が `--until-over` で卓を去った分 (nm の白が空、mb が空) で、結果 (`st: over`、`res`) はそのまま残っている。

```
→ asterism_values {"pattern":"games/oth*"}
isError: "games/oth*": a key's parts are names, * (one part) or ** (any parts)
```

### ホストが去った後 (gone)

ホストの pid に TERM を送って 4 秒後:

```
→ asterism_values {"pattern":"games/**"}
{"since":"2026-10-20T02:16:50.191Z","values":[{"key":"games/games/othello1/state","t":"2026-10-20T02:17:16.766Z","age_s":6.3,"app":"gone","bytes":176,"format":"msgpack","value":{"v":66,...,"st":"over",...,"res":{"w":0,"why":"count","b":41,"wc":23}}}]}
```

別の回で 1 秒ごとに見たところ、TERM の 1 秒後から `app: "gone"` になり、値と時刻はそのまま残った。

### CLI (同じ対局を同時に)

```
$ asterism object get games/games/othello1 state -o json      (--timeout 30、対局を始める前に起動)
{
  "key": "games/games/othello1/state",
  "t": "2026-10-20T02:16:54.966Z",
  "bytes": 60,
  "format": "msgpack",
  "value": {
    "v": 2,
    "t": "othello1",
    "st": "wait",
    ...
    "mb": [
      [
        "Alice",
        -1,
        0
      ]
    ],
    "ts": 0
  }
}
$ asterism object get games/games/othello1
11:16:54.966 games/games/othello1/state
             v=2 t=othello1 st=wait nm=["", ""] sk=[0, 0] aw=0 ow=-1
             mb=[[Alice, -1, 0]] ts=0
```

```
$ asterism object echo games/games/othello1 state --duration 120
11:16:54.966 games/games/othello1/state
             v=2 t=othello1 st=wait nm=["", ""] sk=[0, 0] aw=0 ow=-1
             mb=[[Alice, -1, 0]] ts=0
11:16:54.979 games/games/othello1/state v 2→4
11:16:54.979 games/games/othello1/state nm ["", ""]→[Alice, CPU]
11:16:54.979 games/games/othello1/state sk [0, 0]→[1, 2]
...
11:16:54.989 games/games/othello1/state st wait→play
11:16:54.989 games/games/othello1/state +bd=...........................WB......BW......................…
11:16:54.989 games/games/othello1/state +tn=0
11:16:54.989 games/games/othello1/state +lg=d3c4f5e6
11:16:54.989 games/games/othello1/state +cnt=[2, 2]
...
11:17:16.581 games/games/othello1/state st play→over
11:17:16.581 games/games/othello1/state bd BBBBBBBBBBWBWWWBBWBBWWBBBBWBWBWWBWBBWBWWBWWBWBWWBBBWWWBWBBB…→BBBBBBBBBBWBWWWBBWBBWWBBBBWBWBWWBWBBWBWWBWWBBBWWBBBWWBBWBBB…
11:17:16.581 games/games/othello1/state tn 0→-1
11:17:16.581 games/games/othello1/state n 60→61
11:17:16.581 games/games/othello1/state last e8→g8
11:17:16.581 games/games/othello1/state lg g8→""
11:17:16.581 games/games/othello1/state cnt [38, 25]→[41, 23]
11:17:16.581 games/games/othello1/state +res={w=0 why=count b=41 wc=23}
11:17:16.766 games/games/othello1/state v 65→66
11:17:16.766 games/games/othello1/state nm [CPU, Alice]→[CPU, ""]
...
$ asterism object echo games/games/othello1 state -o json --duration 120
{"key":"games/games/othello1/state","t":"2026-10-20T02:16:54.966Z","bytes":60,"format":"msgpack","value":{"v":2,"t":"othello1","st":"wait","nm":["",""],"sk":[0,0],"aw":0,"ow":-1,"mb":[["Alice",-1,0]],"ts":0}}
{"key":"games/games/othello1/state","t":"2026-10-20T02:16:54.979Z","bytes":68,"format":"msgpack","value":{"v":4,...}}
...
```

文字出力では 64 文字の `bd` が既存の決まり (`Text::LONG` = 60 桁を超える文字列は `…` で切る、`status get` と同じ) で切れる。盤面を見るなら `-o json` (全体が出る)。

ホストを立て直して (保存から再開、終局の卓) `object get -o yaml`:

```
$ asterism object get games/games/othello1 state -o yaml --timeout 10
---
key: games/games/othello1/state
t: '2026-10-20T02:19:22.691Z'
bytes: 176
format: msgpack
value:
  v: 67
  t: othello1
  st: over
  ...
  bd: BBBBBBBBBBWBWWWBBWBBWWBBBBWBWBWWBWBBWBWWBWWBBBWWBBBWWBBWBBBBWBBB
  ...
  res:
    w: 0
    why: count
    b: 41
    wc: 23
```

### 既存の出力は前と同じ

同じ網 (上のホスト) に対し、前 (9eca444 を `git archive` で取り出した lib/exe) と後で次の 25 のコマンドを走らせて `cmp` で比べた。`status echo --once` (先頭の時刻だけが違う) 以外の 24 がバイト単位で一致。

`nodes` / `nodes --json` / `objects` / `objects --json` / `objects games/games/othello1` / `tree` / `tree --json` / `tree --methods` / `tree -o yaml` / `node list` / `node info games` / `node info games -o json` / `app list` / `app info games/games` / `app info games/games -o yaml` / `object list` / `object info games/games/othello1` / `object info ... -o json` / `object find join` / `object find 're*' -o json` / `status get games/games` (文字・`-o json`・`-o yaml`) / `status echo games/games --once` / `call games/games/othello1 table --json`

MCP も前と後の server に同じ JSON-RPC を送って比べた: tools/list の `asterism_nodes` / `asterism_objects` / `asterism_status` の定義 (名前・説明・入力の形) が一致、3 つの道具の答え (`asterism_nodes {}`、`asterism_objects {"pattern":"games/**"}`、`asterism_status {}`) も一致。変わったのは tools/list に `asterism_values` が増えたことと、server の instructions に 1 文増えたこと。

`asterism --help` と `asterism object` (動詞の一覧) には `object get` / `object echo` の 2 行が増える:

```
  object get PATH [KEY]       the next value the application puts on PATH/KEY
                              (its own keys; KEY: ** by default; --timeout 5.0)
  object echo PATH [KEY]      the values it puts there as they come (the text:
                              what changed; --once: the first; --raw: each
                              whole)
```

## 5. 試験

`rake test` 全体が通る (失敗・エラー・skip 0)。`rake test:cli` は 26 runs / 472 assertions (前は 21 runs / 338)。mcp gem が入っているので test_mcp も走っている。

足した試験 (test/test_cli.rb):

- `test_values_decode`: MessagePack・JSON・文字列・バイト列・空、数のキー、値の中のバイナリと NaN、`entry` の形、型の検査 (段の一部の `*`、空の段、`@`、`$` を拒む)。
- `test_values_store`: 型の一致 (`g/g/**`、`*/*/*/log`、既定 `**`)、`since` と `age_s` (時計を差し替え)、新しい put で値が替わり一番新しくなる、キーの数の上限で一番古いものを忘れる (`forgotten`)、合計の上限、大きすぎる put (`dropped`)、去った app の印 (`gone` → 戻ると `restarted` → 新しい put で `alive`)、切り詰め (`truncated`、JSON の先頭と `…`、長さ)、答えのキーの上限 (`more`)、誤った型。
- `test_values_wait`: 何も来なければ `wait` 秒で切れて `note`、`wait` なしならすぐ、型に合わない put では起きず合う put で起きる、値があれば待たない、負の `wait` は 0。
- `test_put_lines`: 文字出力の全体・変わった葉・変化なし・Hash でない値。
- `test_object_values` (別プロセスのノードに当てる。ノードの試験台本に `put KEY JSON` の行で自分のキーへ put する口を足した): `object get` が後から来た合う put を待って出す (合わないキーの put は飛ばす)、`--timeout` が切れたら「nothing came ...」と終了 1、PATH の誤りは終了 2、`object echo` の文字出力 (全体 → `n 1→2` / `cnt [2, 2]→[4, 1]` → 同じ値は何も出さない → 別のキーは全体)、`--once -o json` は 1 件、`--raw` は毎回全体、`--once --duration` で何も来なければ終了 1。
- `test_mcp`: 道具の一覧に `asterism_values` (既存の期待値に 1 つ足した)、説明の要点、起動直後は `values: []` と `note`、`wait` の間に来た put を受け取る (`app: alive`、`format: msgpack`)、2 つのキー、誤った型は isError。
- `test_usage`・`test_complete_words`・`test_complete_names`: `object` の動詞の一覧、`object get` / `echo` の引数の誤り、`--once` / `--raw` が `object echo` にだけある、`--timeout` は `object get` にある、PATH の補完、KEY は候補なし。

既存の試験で期待値を変えたのは、動詞・道具が増えた分の 2 か所だけ (bash の台本の試験の `object` の動詞の一覧、test_mcp の道具の一覧)。旧コマンドと `--json` の期待値は変えていない。

## 6. 0.10.0 の gem

- `rake gem` で `/home/kishima/fmrb/family-mruby/asterism/pkg/asterism-0.10.0.gem` (168960 バイト、`lib/asterism/cli/values.rb` を含む) を作った。公開はしていない (`gem push` は親からユーザに依頼)。`pkg/asterism-0.9.0.gem` (公開済み) は残した。
- コミット: asterism main の 2a039b8「asterism 0.10.0: the values the applications put, from MCP and the command line」(CHANGELOG / README / cli.rb / cli/complete.rb / cli/mcp.rb / cli/nouns.rb / cli/values.rb / version.rb / test_cli.rb)。push・タグなし。`git commit -a` は使っていない。差分に実在の値 (127.0.0.1・192.0.2.x 以外のアドレス、ドメイン、実在のノード名、鍵、ホームのパス) が無いことを確認した。

## 7. 確認できていないこと・止まる条件

- console の viewer の証明書 (ACL のあるルータ) では動かしていない (ユーザの証明書は使わない決まりのため)。viewer の役割は `read` / `watch` を `asterism/**` に持つので、subscribe (`asterism/**`) と liveliness (`asterism/*/*`) は通るはず。読めない型のときの「何も来なかった」は、ACL の代わりに put の無い型で確認した。
- 止まる条件に当たったものはない。
