# CLI2 報告: asterism の CLI の出力を人が読みやすく

> 状態: 完了 (検収待ち) | 更新: 2026-10-19 | 指示書 instruction_cli2.md の 4 つ (状態の段付け、tree はメソッドの数だけ・`--methods` で展開、objects の説明の折り返し、色と watch の差分) を入れた。`--json` は前と同じ (実際の出力をバイト単位で比べた)。asterism 0.8.1 (b188176、main、push・タグ・gem push なし)。`pkg/asterism-0.8.1.gem` は作成済

## 1. 変えたもの

- `lib/asterism/cli/text.rb` (新規): `Style` (色と幅) と `Text` (表示幅、折り返し、状態の段付け、状態の差分)。標準ライブラリだけ (`io/console` の `winsize`)。
- `lib/asterism/cli.rb`: `nodes` / `tree` / `objects` / `watch` の文字出力を `Text` で組む。`tree --methods` を足した。`CLI.new` / `CLI.start` が `env:` を受ける (試験で `NO_COLOR` / `COLUMNS` を渡すため。既定は `ENV`)。`--json` の経路は触っていない。
- `test/test_cli.rb`: 試験を足し、出力の形が変わった既存の試験 (`objects` の 2 つ) を新しい形に直した。`--json` の試験は変えていない。
- README「The command line」の例と Output の項、CHANGELOG (Unreleased を 0.8.1 に。ca139a7 の環境変数の項と一緒)、`lib/asterism/version.rb` を 0.8.1、README の gem の名前の例を `asterism-0.8.1.gem`。asterism には `VERSION` ファイルは無い (版は version.rb だけ)。

## 2. 決めた形

1・2 章の範囲で決めた細部。いずれも試験で押さえている。

- **端末の判定**: 標準出力が `tty?` のときだけ色と端末の幅 (`winsize`) を使う。パイプ・ファイルでは色なし、幅は `COLUMNS` (なければ 80)。`NO_COLOR` が空でない値のとき色なし (空文字は未設定扱い、no-color.org の決まりどおり)。幅の下限は 20。
- **状態の段付け**:
  - 同じ段の素の値 (数・文字列・Hash を含まない配列) は `k=v k=v` で 1 行に並べ、幅で折り返す。
  - 値が素の値だけの Hash は `key: k=v k=v`。折り返した続きは最初の値の位置にそろえる (key が幅の 1/3 を超えるときは 2 字下げ)。
  - それより深い Hash は `key:` の下に 2 字下げで再帰。Hash を含む配列は `key:` の下に要素ごとに `- ` の行。空の Hash は `key: {}`、状態自体が空なら `{}`。
  - 値の書き方: 空白や記号を含まない文字列はそのまま、それ以外は JSON の引用 (`s="two words"`)。nil は `nil`。1 行に書く Hash は `{k=v k=v}`。
  - **長い値**: 文字列は表示幅 60 で切って `…`。1 行の配列も 60 を超えたら後ろを落として `…+N` (落とした数)。`key=value` は途中で折らず、行より長いものはその 1 行に置く (端末の折り返しに任せる)。全体は `--json` で見る。
  - 表示幅は東アジアの全角文字を 2 桁と数える (説明文の日本語も幅で折る。空白の無い文は文字の間で折る)。
  - バイナリの文字列は従来どおり `(N bytes)`。
- **tree / nodes**: アプリの行の下に `status:` の行、その下に段付けの状態。tree のオブジェクトは `mahjong1  18 methods` (1 個なら `1 method`)。`--methods` でオブジェクトの下に署名を 2 つの空白で区切って幅で折り返す (署名の途中では折らない)。
- **objects**: パスの行、2 字下げで説明 (段落ごとに詰めて折り返す。説明は先頭行だけでなく全文)、続いてメソッドを 1 行に 1 つ、説明を 1 本の列にそろえて折り返す。列は「最長の署名 (上限 24 桁) + 4」。署名が列に収まらないメソッドは説明を次の行から。どのメソッドにも説明が無いオブジェクトは署名を幅で折り返して並べる。署名は太字。
- **色**: ノード (太字の青)、アプリ (シアン)、オブジェクト (緑)、メソッドの署名 (太字)、注記・プロファイル・時刻 (薄く)、watch の印 `+` 緑・`-` 赤・`~` 黄・`*` 紫。差分の `+key` を緑、`-key` を赤。
- **watch の差分**: パスごとに最後の状態を覚え、最初の 1 回 (と、その後で初めて見たアプリ) は全体を段付けで (行頭は `~ status` の位置にそろえる)。以降は変わった葉ごとに 1 行: `a.b 0→1` (値の変化、両方 Hash なら中を比べる)、`+a.c=1` (増えたキー、Hash なら `{…}` の 1 行)、`-a.d` (消えたキー)。変化が無い put は何も出さない。アプリ (またはノード) が去ったらその状態を忘れ、戻ってきたら全体を出す。出力は Mutex で直列化した (購読の callback は別スレッドから来るため)。

## 3. 実際の出力 (前と後)

手元で zenohd (`eclipse/zenoh:1.10.1`、コンテナ名 cli2-zenohd、127.0.0.1:27447) と asterism-games の作業コピーのホスト (`ruby -I../asterism/lib -I../asterism-zenoh/lib -I../riichi_engine/lib bin/asterism-games --config <scratchpad>/games.yml`、mahjong1 と othello1、`ASTERISM_GAMES_STATE_DIR` は scratchpad、`ANTHROPIC_API_KEY` を外して起動) を立て、前 (`git archive HEAD` で取り出した ca139a7 の lib/exe) と後を同じ網に当てた。出力はパイプ経由 (色なし、80 桁)。どちらも後片付け済 (ホストは TERM で停止、コンテナは `docker rm -f cli2-zenohd`)。

### tree

前:

```
games  (cruby) "asterism-games"
  games  status {"mahjong1":{"st":"wait","members":0,"humans":0,"cpus":0,"ais":0,"away":0},"othello1":{"st":"wait","members":0,"humans":0,"cpus":0,"away":0}}
    mahjong1  join(name) mine() sit(token, seat) stand(token) cpu(token, seat, on) start(token) rule(token, item, value) view(token, assist) act(token, sv, type, tiles) table() describe() look(token) choose(token, sv, n) points(token) records(from, *) record(id, round, part, *) stats(days, game, scope, from, *) leave(token)
    othello1  join(name) mine() sit(token, seat) stand(token) cpu(token, seat, on) start(token) move(token, n, square) resign(token) view(token) table() leave(token)
```

後:

```
games  (cruby) "asterism-games"
  games
    status:
      mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
      othello1: st=wait members=0 humans=0 cpus=0 away=0
    mahjong1  18 methods
    othello1  11 methods
```

### tree --methods

```
games  (cruby) "asterism-games"
  games
    status:
      mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
      othello1: st=wait members=0 humans=0 cpus=0 away=0
    mahjong1  18 methods
      join(name)  mine()  sit(token, seat)  stand(token)  cpu(token, seat, on)
      start(token)  rule(token, item, value)  view(token, assist)
      act(token, sv, type, tiles)  table()  describe()  look(token)
      choose(token, sv, n)  points(token)  records(from, *)
      record(id, round, part, *)  stats(days, game, scope, from, *)
      leave(token)
    othello1  11 methods
      join(name)  mine()  sit(token, seat)  stand(token)  cpu(token, seat, on)
      start(token)  move(token, n, square)  resign(token)  view(token)  table()
      leave(token)
```

### objects

前 (mahjong1 の冒頭。1 行が長いまま):

```
games/games/mahjong1  join(name) mine() sit(token, seat) stand(token) cpu(token, seat, on) start(token) rule(token, item, value) view(token, assist) act(token, sv, type, tiles) table() describe() look(token) choose(token, sv, n) points(token) records(from, *) record(id, round, part, *) stats(days, game, scope, from, *) leave(token)
  A riichi mahjong table (four seats). From a program: call describe first, then join, sit, and look / choose in a loop.
    join(name)  Joins the table as name (or gets this node's membership back). Returns {"token", ...}: every later call brings the token. {"error" => why} when the table is full.
    mine()  This node's membership ({"token", ...}), or nil when it has not joined.
    sit(token, seat)  Takes seat 0-3 (-1: any free one), in the lobby. "ok" or why not.
    ...
```

後 (全体):

```
games/games/mahjong1
  A riichi mahjong table (four seats). From a program: call describe first, then
  join, sit, and look / choose in a loop.
  join(name)                Joins the table as name (or gets this node's
                            membership back). Returns {"token", ...}: every
                            later call brings the token. {"error" => why} when
                            the table is full.
  mine()                    This node's membership ({"token", ...}), or nil when
                            it has not joined.
  sit(token, seat)          Takes seat 0-3 (-1: any free one), in the lobby.
                            "ok" or why not.
  stand(token)              Gives the seat up, in the lobby. "ok" or why not.
  cpu(token, seat, on)      The owner (the first to sit): on true puts a CPU in
                            the empty seat (seat -1: every empty seat), false
                            takes a CPU or AI player out; a name the lobby
                            offers (look's "ai_players": "claude", "jev") puts
                            that AI player in. On one seat that holds a CPU or
                            another AI player, on turns it into the one asked
                            for.
  start(token)              The owner starts the game once the four seats are
                            taken. "ok" or why not.
  rule(token, item, value)  The owner sets a local rule in the lobby: item 0 the
                            game (0 hanchan, 1 east only), see describe. "ok" or
                            why not.
  view(token, assist)       The member's view in the table screens' short keys
                            and tile bytes (assist 0-3: the help shown).
                            Programs read look instead.
  act(token, sv, type, tiles)
                            Plays an action in the screens' numbers. Programs
                            use choose instead.
  table()                   The public view (no hand), as a spectator sees it,
                            in the screens' short keys.
  describe()                How to play from a program and what look returns (a
                            text). Read it first.
  look(token)               The table as this member's seat sees it, in words:
                            hand, rivers, melds, scores, dora indicators, and
                            "moves" (numbered) when it is the seat's decision.
                            See describe.
  choose(token, sv, n)      Plays the move numbered n of look's "moves", with
                            the "sv" of that look. "ok", or why not ("stale":
                            look again).
  points(token)             What the seat's hand would score: the win offered
                            now, or each wait of a tenpai hand by ron and tsumo
                            (no ura dora, furiten not checked).
  records(from, *)          The games this table played to the end, newest first
                            (the kept ones; in a room the room's): {"records" =>
                            [{id, at, game, names, ranks, scores}], "total",
                            "next"}. from (optional, 0) skips that many; "next"
                            is the from of the next page.
  record(id, round, part, *)
                            A finished game's record, in look's tile names.
                            record(id): who played, the rules, the final ranks
                            and a line per round; record(id, round): that round
                            (0-based): deal, dora, every draw, discard and call,
                            the result. A round too big for one answer comes in
                            parts: "parts" says how many, record(id, round,
                            part) gives the others.
  stats(days, game, scope, from, *)
                            The players' stats: per person (by the name at the
                            table) and per kind (CPU, Claude, Jev): games,
                            ranks, avg_rank, win/deal-in/riichi rates of the
                            rounds, avg_score, rank points. All optional: days
                            (0: every game kept), game ("", "hanchan",
                            "tonpuu"), scope ("all": every table and room,
                            "table": this table's; in a room the room's), from
                            (the page: "next").
  leave(token)              Leaves the table (a seat during a game goes to a
                            CPU). true / false.
games/games/othello1
  An othello table (two seats). join, sit, then view / move in a loop.
  join(name)              Joins the table as name (or gets this node's
                          membership back). Returns {"token", ...}: every later
                          call brings the token. {"error" => why} when the table
                          is full.
  mine()                  This node's membership ({"token", ...}), or nil when
                          it has not joined.
  sit(token, seat)        Takes seat 0 or 1 (-1: the free one), in the lobby.
                          "ok" or why not.
  stand(token)            Gives the seat up, in the lobby.
  cpu(token, seat, on)    The owner (the first to sit): on true puts a CPU in
                          the empty seat (-1: every empty seat), false takes it
                          out.
  start(token)            The owner starts the game once both seats are taken
                          (the colours are drawn).
  move(token, n, square)  Plays square ("d3": column a-h, row 1-8) as the seat
                          to move. n is the view's "n" (moves so far), so a move
                          meant for an older board is refused ("stale"). "ok" or
                          why not.
  resign(token)           Resigns the game.
  view(token)             The table: st (wait, play, over), nm names, sk seats
                          (0 empty, 1 member, 2 CPU), bd the board (64 of . B W,
                          a1 b1 .. h8), tn the seat to move (-1), n moves so
                          far, lg the legal squares of the side to move
                          ("d3c4"), cnt [black, white], res the result, me your
                          seat. Seat 0 is black (B), seat 1 white (W): the seats
                          are drawn at the start.
  table()                 The table's view without me (what the state key has).
  leave(token)            Leaves the table (a seat during a game goes to a CPU).
                          true / false.
```

### nodes

前:

```
games  (cruby) "asterism-games"
  games  2 objects  status {"mahjong1":{"st":"wait","members":0,"humans":0,"cpus":0,"ais":0,"away":0},"othello1":{"st":"wait","members":0,"humans":0,"cpus":0,"away":0}}
```

後:

```
games  (cruby) "asterism-games"
  games  2 objects
    status:
      mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
      othello1: st=wait members=0 humans=0 cpus=0 away=0
```

### watch (人が卓に入って座り、去る)

前と後の watch を同時に走らせ、別の端末から `asterism-games play mahjong --table games/games/mahjong1 --node tty-cli2 --name Alice --commands sit` (作業コピー、ローカルの asterism) で入って座り、6 秒後に quit した。

前:

```
21:26:11.987 ~ status games/games {"mahjong1":{"st":"wait","members":0,"humans":0,"cpus":0,"ais":0,"away":0},"othello1":{"st":"wait","members":0,"humans":0,"cpus":0,"away":0}} (now)
asterism: watching (Ctrl-C to stop)
21:26:15.669 + node tty-cli2
21:26:15.669 + app tty-cli2/game
21:26:15.669 * profile tty-cli2  (cruby)
21:26:15.675 ~ status games/games {"mahjong1":{"st":"wait","members":1,"humans":0,"cpus":0,"ais":0,"away":0},"othello1":{"st":"wait","members":0,"humans":0,"cpus":0,"away":0}}
21:26:15.688 ~ status games/games {"mahjong1":{"st":"wait","members":1,"humans":1,"cpus":0,"ais":0,"away":0},"othello1":{"st":"wait","members":0,"humans":0,"cpus":0,"away":0}}
21:26:21.696 - node tty-cli2
21:26:21.696 - app tty-cli2/game
21:26:21.705 ~ status games/games {"mahjong1":{"st":"wait","members":0,"humans":0,"cpus":0,"ais":0,"away":0},"othello1":{"st":"wait","members":0,"humans":0,"cpus":0,"away":0}}
```

後:

```
21:26:11.987 ~ status games/games (now)
               mahjong1: st=wait members=0 humans=0 cpus=0 ais=0 away=0
               othello1: st=wait members=0 humans=0 cpus=0 away=0
asterism: watching (Ctrl-C to stop)
21:26:15.669 + node tty-cli2
21:26:15.669 + app tty-cli2/game
21:26:15.669 * profile tty-cli2  (cruby)
21:26:15.675 ~ status games/games mahjong1.members 0→1
21:26:15.688 ~ status games/games mahjong1.humans 0→1
21:26:21.696 - node tty-cli2
21:26:21.696 - app tty-cli2/game
21:26:21.705 ~ status games/games mahjong1.members 1→0
21:26:21.705 ~ status games/games mahjong1.humans 1→0
```

(「asterism: watching」は標準エラー。前からそう。)

### 端末での確認

`script` で擬似端末を作り `stty cols 60` で `nodes` を走らせると、色の符号が付き、幅 60 で折り返した (`mahjong1: ... ais=0` の後で `away=0` が値の位置にそろって次の行へ)。`NO_COLOR=1` では同じ幅で色なし。

### --json は前と同じ

同じ網に対し、前と後の `tree --json` (10715 バイト) / `nodes --json` / `objects --json` を `cmp` で比べて一致。`watch --json --duration 4` も時刻 (`"t"`) を除いて一致。

## 4. 試験

`rake test` 全体が通る (16 の試験ファイル、失敗・エラー・skip 0)。`rake test:cli` は 12 runs / 145 assertions (前は 6 runs / 84)。mcp gem が入っているので test_mcp も走っている。

足した試験 (test/test_cli.rb):

- `test_status_lines`: 素の値の 1 行、`key: k=v` とその折り返し (続きの字下げ)、深い Hash、Hash を含む配列 (`- ` と、入れ子の Hash の要素)、素の配列、空の Hash、空の状態。
- `test_long_values`: 空白のある文字列の引用、長い文字列を 60 桁で `…`、長い配列の `…+N` (落とした数の勘定)、全角文字の幅、長い `key=value` を折らずに 1 行、バイナリ。
- `test_wrap`: 字下げをそろえた折り返し (全行が幅以内)、空白の無い日本語の折り返し、段落。
- `test_status_diff`: 変わった値、消えたキー、増えたキー (Hash の 1 行)、変化なし、Hash から値への変化。
- `test_style`: パイプ (色なし・80)、`COLUMNS`、端末 (色あり・端末の幅が `COLUMNS` に勝つ)、`NO_COLOR`、空の `NO_COLOR`、色の符号は幅に数えない。
- `test_watch_text`: 最初の 1 回は全体を段付け、変わったキーだけ・増えたキー・消えたキー、変化なしは何も出さない、アプリが去った後は全体に戻る、端末での色。
- `test_commands` (ノードに当てる既存の試験) に、`objects` の新しい形と `COLUMNS=30` での折り返し・色が付かないこと、`nodes` と `tree` の段付け、`tree --methods` の折り返し、文字の `watch` で最初の段付けと `tables 7→8` の差分を足した。`objects` の 2 つの期待値を新しい形に直した。`--json` の試験は変えていない。

## 5. 0.8.1 の gem

- `rake gem` で `/home/kishima/fmrb/family-mruby/asterism/pkg/asterism-0.8.1.gem` (151040 バイト、`lib/asterism/cli/text.rb` を含む) を作った。公開はしていない (`gem push` は親からユーザに依頼)。
- コミット: asterism main の b188176「asterism 0.8.1: the command line's text for people」(CHANGELOG / README / cli.rb / cli/text.rb / version.rb / test_cli.rb)。push・タグなし。差分に実在の値 (アドレス・ドメイン・個人の名前) が無いことを確認した (例のアドレスは文書用の 192.0.2.x のまま)。
- asterism-games の Gemfile は asterism 0.8.0 に固定のまま (触っていない)。0.8.1 の公開後に上げるかは別の判断。

## 6. 止まる条件に当たったもの

なし。
