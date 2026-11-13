# P3a report: asterism の CLI・MCP と asterism-games を protocol 2 に入れ替えた

> 状態: 済 (確認待ちの点あり) | 更新: 2026-11-13 | asterism の CLI・MCP がトピックと型を見せ、値を encoding・通し番号つきで読むようにした (asterism 8c90094)。asterism-games は卓の状態・お絵かき・観戦を protocol 2 で流し、遊ぶ側・実況は `Asterism.subscribe` で受け取る (asterism-games fda40ac・59cc130)。CRuby どうしの通しで、オセロ 1 局と麻雀の東風戦 1 回が最後まで進んだ。**観戦のキーには宣言が付けられない** (`spectate` はホストの Asterism アプリではない) のと、**型の言葉で書けないフィールドがある** (混ざった組・配列の配列) ので、どちらも暫定の形で残した (下の「止まった点」)

## 1. 変えた所

### asterism (8c90094、`feat(cli): ...`)

| ファイル | 何を |
|---|---|
| `lib/asterism/cli/survey.rb` | `app_profiles` (アプリの `@profile` に一斉に get)、`statuses` をその上に、`topics` (`?pubs;types` を 1 回の get で)、`Survey.topic_entry` / `topic_list` (宣言を読める形に: `type` は組み込みなら名前、`encoding` は名前、`unit`・`desc`・`ts`)。`tree` は引数なしの `@profile` 1 回で状態とトピックを取る |
| `lib/asterism/cli/commands.rb`・`nouns.rb`・`complete.rb` | 新しい名詞 **`topic`**: `topic list [NODE/APP]` (node/app/name と型を 1 行ずつ)、`topic info NODE/APP/NAME` (宣言と、アプリ独自の型ならその定義)。`app info` は独自の型も出す。Tab 補完も足した |
| `lib/asterism/cli.rb` | `status_block` の下に `topics:` の段 (tree・nodes・node info・app info)、`types_block` |
| `lib/asterism/cli/values.rb` | 値を encoding で読む (なし・`zenoh/bytes` は MessagePack、JSON・テキストの encoding はそのまま、`image/jpeg` などは大きさ)。attachment から `seq`・`sent` (送り手の時刻)。`Values::Seq` で欠け (`lost`) とつなぎ直し (`restarted`) を数える (読み方は `Asterism::Subscription` と同じ)。`Store` は画像の bytes を持ち、`answer_images` で渡す |
| `lib/asterism/cli/mcp.rb` | 新しい道具 **`asterism_topics`** (`node`・`app` で絞る)。`asterism_values` は encoding・attachment を読み、**`images: true`** で絵 (png・jpeg・gif・webp、4 枚まで) を MCP の image として返す (既定は大きさだけ) |
| `README.md`・`test/test_cli.rb` | 説明と試験 (下の 3 章) |

- `object get / echo` と `asterism_values` は素の `session.subscribe` で値を受けたまま (`Asterism.subscribe` にしなかった)。理由: 見る道具として、線の上の大きさと、MessagePack でない値 (protocol に沿わない送り手) も見せたい。`Asterism.subscribe` は解けない値を落とし、大きさを渡さない。
- 文字の出力の変化: `object echo` は欠けがあれば `(2 lost)`、番号が戻れば `(started again at 0)` を 1 行で先に出す。絵は `(image/jpeg, 100 bytes)` と出す。`-o json` の 1 件に `seq`・`sent`・`lost`・`restarted` が増え、`format` は encoding の名前 (なしは `msgpack`)。

### asterism-games (fda40ac `build(gemfile): ...`、59cc130 `feat(host): ...`)

| ファイル | 何を |
|---|---|
| `Gemfile`・`.gitignore` | `ASTERISM_DIR` で手元の asterism を path で使う分岐 (下の 2 章) |
| `lib/asterism_games/host.rb` | `<table>/state` と言ったもの (`<table>/strokes`) を `Asterism.publish` のトピックに。つないだとき (`up`) に全部の卓のトピックを宣言し、ゲームの型を `define_type`。観戦は `SpectatePub` (Zenoh の publisher に `[seq]` の attachment)。`TablesStatus` は asterism 0.7.1 向けの手作りの `@status` queryable を消し、`net.status` だけに |
| `lib/asterism_games/game.rb` | `TYPES`・`STATE_TYPE`・`TOLD_TYPES` (既定は無し) |
| `lib/asterism_games/othello/table.rb`・`board/table.rb`・`app/asterism_games/mahjong/table.rb` | 型の定義: `asterism-games/othello.state`・`.result`、`board.state`・`board.strokes`、`mahjong.state`・`.result`・`.win` |
| `lib/asterism_games/play/othello.rb`・`play/mahjong.rb` | 卓の状態を `net.subscribe("<host>/<app>/<table>/state") { \|v\| }` で受ける |
| `lib/asterism_games/commentate/mahjong.rb` | 観戦のキーを `net.subscribe("*/spectate/*/state") { \|v, info\| }` で受ける (卓の名前は `info.name` の先頭) |
| `README.md`・`docs/install.md` | 「Topics and types」の節、開発中のつなぎ方、`games-host` の役割に `@pubs` と `@profile` |
| `test/` | 偽の網 `FakeNet` (test_helper.rb) に置き換え、宣言・型・観戦の番号・状態のキーが型に入っていることを確かめる試験を足した。test_tally の網の試験を protocol 2 に (`subscribe` の seq、`pubs_of`、`status_of`) |

- 「端末の側」(bin/mahjong-bots) は呼び出しだけで、Asterism のデータのキーを使っていなかった (変更なし)。

## 2. 開発中の手元のつなぎ方と、P4 での戻し方

- **asterism-games**: Gemfile に分岐を足した。`ASTERISM_DIR` があれば `gem "asterism", path: ENV["ASTERISM_DIR"]` と
  `lockfile "Gemfile.asterism_dir.lock"` (Bundler 2.6 以降の Gemfile の書き方。手元は 4.0.6)。無ければ今どおり `gem "asterism", "0.8.0"`。
  別のロックファイルは `.gitignore` に入れたので、`Gemfile.lock` は公開版のまま変わらない (手元の絶対パスもコミットされない)。
  初回は `cp Gemfile.lock Gemfile.asterism_dir.lock` してから `ASTERISM_DIR=<手元> bundle install` すると、asterism 以外の版がそろう。
  使い方: `ASTERISM_DIR=../asterism bundle exec rake test`。
- **asterism 本体の試験・CLI**: Rakefile が `../asterism-zenoh` を `-I` で使う (今までどおり)。asterism-zenoh は変更なし (0.4.2)。
- **P4 での戻し方**: asterism を版上げして公開したら、Gemfile の `else` 側を `gem "asterism", "<新しい版>"` にして `bundle install`
  で `Gemfile.lock` を更新し、gemspec の依存 (今は `"~> 0.7", ">= 0.7.1"`) を新しい版以上に上げる (今のコードは protocol 2 の
  `publish`・`subscribe`・`define_type` を使うので、古い asterism では動かない)。分岐そのものは残してよい (次の開発でも使える)。
  イメージ (deploy/vps/Dockerfile) は `Gemfile.lock` から入れるので、ロックの更新だけで新しい asterism になる。

## 3. 試験

いずれも `ASTERISM_CREDENTIALS` を外して流した。

| 何を | 結果 |
|---|---|
| asterism `rake test` (既定の一式) | **145 runs、2590 assertions、0 failures** (msgs 2775 checks、適合 `cruby: 241 passed, 0 failed`)。うち `test_cli.rb` 29 runs 594 assertions (前は 27 runs) |
| asterism の新しい試験 | `test_values_encodings` (encoding の読み、attachment、`Seq` の欠け・つなぎ直し・折り返し、Store の `lost` と画像)、`test_put_lines` に絵と欠けの行、`test_topics` (node をまたいで topic list / info、app info、tree、nodes の JSON、object echo の絵・seq・lost)、`test_mcp` に `asterism_topics` と `asterism_values images: true` |
| asterism-games `rake test` (`MJ_GAMES=4`、`ASTERISM_TEST_ROUTER` に手元のルータ) | **406 runs、45768 assertions、0 failures、1 skip** (skip は手番の前に局が終わったときの既存のもの) |
| asterism-games `rake test:gem` | **165 runs、7074 assertions、0 failures** (10 skips は既存: 網の試験とmahjong 抜き) |

- ルータは zenoh-c の router モードのセッションを CRuby で開く使い捨てのスクリプト (scratchpad、`Asterism::Zenoh::Session.open(config: {"mode" => "router", ...})`) で立てた。
  docker の zenohd は使っていない。
- 途中、ルータなしの 1 回目で麻雀の試験が 1 つずつ落ちた (`test_a_tonpuu_game_chosen_in_the_lobby_ends_in_the_east`: 3 局 < 4、`test_members_gone_for_long_are_forgotten_and_it_stops`)。
  前者は変更前のコード (手元の asterism) でも起きた。後者は単独で 3 回流して 3 回通った。どちらもトピックとは関係しない対局の揺れと見ている
  (2 回目の一式では出なかった)。

## 4. 通し (CRuby どうし、手元のルータ)

ルータ・ゲームホスト (othello1・mahjong1 (東風)・board1)・CLI (`object echo -o json` を 2 本)・`asterism-games commentate mahjong --json`・
`asterism-games play` を 1 つの台本 (scratchpad) で流した。

- `asterism topic list 'games/*'`: 3 卓の `state` と `board1/strokes` が型つきで出た。`topic info games/games/othello1/state` で型の定義、
  `board1/strokes` の JSON で定義 (`o str`・`id int`・…) が引けた。値が来る前に見えることを確かめた。
- **オセロ**: `play othello --commands sit,cpu,start,auto --until-over` が 1 局を最後まで (CPU 41-23)、終わって exit 0。
- **麻雀**: `play mahjong --commands sit,cpu,tonpuu,start,auto --until-over` が東風戦 (5 局) を最後まで、終局で exit 0。
- CLI の受け取り: オセロの state 66 値 (seq 2..67)、麻雀の state 329 値 (seq 2..330)、**欠け 0・つなぎ直し 0**、format はすべて `msgpack`。
  先頭が 2 なのは、宣言直後のホストの put を CLI が購読する前に送ったため (起動の順番。欠けには数えない)。
- 実況: 観戦のキー (番号つき、宣言なし) を `Asterism.subscribe` で受け、38 行の実況を出した (対局開始から)。ホストのログは「spectate 329 puts」。

## 5. 見立てと違った点

- asterism-games の Gemfile は **asterism 0.8.0 に固定**されていた (gemspec は `>= 0.7.1`)。手元の asterism (0.13.0 + protocol 2) に
  替えても、変更前のコードの試験は麻雀の揺れ 1 つを除いて通った (0.8.0 から 0.13.0 への互換は保たれていた)。
- ホストの `@status` には 0.7.1 向けの手作りの queryable が残っていた。protocol 2 では `@status` に誰も答えないので、残すと
  0.7.1 の頃の形を名乗る道になる。消した。
- zenoh-c の publisher (CRuby) は宣言してすぐの put も届いた (P1 の zenoh-pico の write filter のような取りこぼしは CRuby の試験・通しでは見ていない)。

## 6. 撤回した仮説

- 「観戦のキーも `Asterism.publish` で流せる」: 流せない。`publish` のキーは常に `asterism/<node>/<自分の app>/<name>` で、観戦は
  `spectate` という別のアプリの区切りにある (選手の役割 `asterism/*/games/**` に掛からないようにするため、README「Spectate」)。
- 「CLI の値の表示は `Asterism.subscribe` に寄せられる」: 寄せない (1 章の理由)。

## 7. 止まった点・確認してほしい点 (自分では決めていない)

1. **観戦のキーの宣言** (決まりの穴): Asterism のアプリでない区切り (`asterism/<node>/spectate/...`) に流すものを宣言する手段が protocol 2 に無い。
   今は値に `[seq]` を付けるだけで、`@profile?pubs` には出ない (`asterism topic list` に出ない、console の図にも出ない見込み)。候補:
   (a) このまま (宣言なし、番号だけ)。(b) 2 本目の接続や同じ接続で `spectate` の `@profile` queryable と `@pubs` を手で立てる (Asterism に
   「別のアプリ名で publish する」API を足すか、ホストが手で書く。API を足すのは止まる条件)。(c) 観戦のキーをホストのアプリの下に移し、
   ACL で分ける (キーと console の規則が変わる、仕様の変更)。
2. **型の言葉で書けないフィールド**: 型のフィールドには混ざった組 (`mb` の `[name, seat, flags]`)・配列の配列 (麻雀の `fin`、和了の `pa`)・
   「無いか値か」を書く手段が無い。今は**定義から外し、型の説明 (`d`) に外したことを書いた**。候補: (a) このまま。(b) 組の書き方
   (`"[str,int,int]"` など) を protocol に足す。(c) 何でもよい型 (`any`) を基本の型に足す。(d) ゲームの値の形を map に変える (画面が読む形が変わる)。
3. CLI・MCP に足した名前 (案のまま進めた): 名詞 `topic` (`list`・`info`)、MCP の `asterism_topics`、`asterism_values` の入力 `images`
   (既定 false)、値の項目 `seq`・`sent`・`lost`・`restarted`。`watch` にトピックの変化 (`@pubs` の put) は足していない。

## 8. 残件 (P4 で公開するときの順序の注意)

- **gem の順**: asterism-zenoh は変更なし (0.4.2 のまま)。asterism (P2 の本体と P3a の CLI) を先に版上げして公開し、その版を
  asterism-games の Gemfile・gemspec に書いて (2 章)、asterism-games を版上げして公開する。
- **console と VPS のルータの ACL を同時に**: ホストは `@status` の queryable をやめ、`asterism/<node>/<app>/@profile` の queryable と
  `@pubs` の put を使う。permanent tables のホストの役割 (`games-host`) に `asterism/${node}/*/@pubs` (書き) と `asterism/${node}/*/@profile`
  (queryable と reply) が要る (docs/install.md に足した)。読む側 (CLI・MCP・console) は `asterism/*/*/@profile` の query と `@pubs` の購読が要る。
  console の規則の言葉に直すのは P3b。新しい asterism-games のイメージを VPS に入れるのは、console の ACL を変えたのと同時にする
  (先にホストだけ替えると状態の答えが規則で落ちる。後だと古いホストの `@status` の queryable に誰も問い合わせない)。
- 観戦を読む側 (console の観戦の行、ブラウザ版) が `asterism/*/spectate/*/state` を購読しているなら、値は今までどおり MessagePack で
  attachment が増えただけなので、protocol 1 の読み手でも読める。
- 手元の作業物: `asterism-games/Gemfile.asterism_dir.lock` (無視される) と `vendor/bundle` に入った anthropic 1.82.0 (別ロックを作り直したとき。今のロックは 1.80.0 にそろえた)。

## 9. 追記: 決定 19・20 の反映 (2026-11-13)

7 章の 2 点がユーザに決められた (design.md の 19・20、fmruby-core 712f0493)。

### 変えた所

- **asterism 79e0db6** (`feat(protocol): topics under another app name, and any and nested arrays in types`)
  - 19: `Asterism.publish(..., app:)`・`define_type(..., app:)` (と `unpublish`・`published` の `app:`、`Topic#app`、CRuby の
    `net.publish` ほか)。別の名前のトピックは `asterism/<node>/<app:>/<name>`。その名前に `@profile` の queryable (最初のトピックか型が
    できたときに立てる。`pubs`・`types` に答え、状態は答えない: `?status` は空の map) と `@pubs` の put。オブジェクト・`**` の
    queryable・生存の印は無い (`Asterism.apps` に出ず、重複の確かめも掛からない)。protocol.md に 10.4 を足した。自分の別名への
    `pubs_of` / `types_of` は網を使わない。
  - 20: `[]` を重ねられる (`"int[][]"`)、基本の型に `any`。`"[][]"` だけは断る。protocol.md 11.2。
  - 試験: test_topic (CRuby の 2 プロセス間で別名の `@profile`・値・`@pubs`、自分の別名)、host_test (mruby で別名の `pubs_of`・
    `types_of`・値、新しい型)、適合の見本 (被試験側が `watch` の名前で型を定義し、`int[][]`・`any` を使う。別名の `@profile` の
    問い合わせ 3 つ、`*/*` が別名にも届く)。
- **asterism-games 6d6b0f0** (`feat(host): spectate keys as topics of the name spectate, full state types`)
  - ルームの外 (LAN・permanent tables) では観戦のキーを `publish(..., app: "spectate")` で宣言する (型なし、説明つき)。
  - **ルームのホストは今までどおり**: 観戦はルームの外に開いた 2 本目の接続 (Asterism の接続ではない、Asterism は 1 プロセス 1 接続)
    で流すので、宣言は付かず番号だけ。ここは決定 19 の API では覆えない (止まるほどの不整合ではないので報告に留める)。
  - 型: `mb` は `any[]` (オセロ・お絵かき・麻雀)、麻雀の `fin` は `int[][]`、和了の `pa` は `any[][]` (`[席, 役満の名前]` で混ざる)。
    型の説明から「載せていない」の文を消した。
  - docs/install.md の `games-host` の役割は前に足した `asterism/${node}/*/@pubs`・`asterism/${node}/*/@profile` の `*` が
    `spectate` にも掛かるので、行は足していない。観戦を読む役割 (spectator など) が `@profile` に問い合わせるには、console の
    規則に `asterism/*/spectate/@profile` の query と `@pubs` の購読が要る (P3b)。

### 試験 (`ASTERISM_CREDENTIALS` を外して)

| 何を | 結果 |
|---|---|
| asterism `rake test` | 145 runs、2618 assertions、0 failures (適合 cruby 244 passed、0 failed) |
| asterism `rake test:mruby` | 59 + 15 + 26 checks、0 failed |
| asterism `rake test:protocol_mruby` | 244 passed、0 failed |
| asterism `rake test:cross` | 1 run、24 assertions、0 failures |
| asterism-games `rake test` (`MJ_GAMES=4`、手元のルータ) | 406 runs、36326 assertions、0 failures、1 skip |
| asterism-games `rake test:gem` | 165 runs、7078 assertions、0 failures |

### 通し

ホスト (othello1・mahjong1・board1) を手元のルータで上げ、`asterism topic list 'games/*'` に
`games/spectate/mahjong1/state` が出た (`topic info` で説明、`object get` で `seq` つきの値)。`play othello` の 1 局が終局まで
(CPU 41-23)、exit 0。

### 残件

- ルームの観戦のキーは宣言されない (上)。宣言したいなら、ルームの外の接続を Asterism の接続にする手段 (1 プロセス 2 接続) が要る。
- 別名には生存の印が無いので、`asterism tree` などは別名をアプリとして出さない (ホストは観戦のために手で `asterism/<node>/spectate`
  の印を立てているので、ゲームホストの場合は出る)。
