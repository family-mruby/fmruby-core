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

## 9. 追記: 決定 20 の反映、決定 19 の取り下げ (2026-11-13)

7 章の 2 点について、ユーザが決定 19・20 を出した (design.md、fmruby-core 712f0493)。

### 決定 19 は取り下げ

- 一度「同じ接続で別のアプリ名 (`spectate`) でもトピックを宣言できる API (`publish(..., app:)`)」を入れて試験まで通したが
  (asterism 79e0db6、asterism-games 6d6b0f0)、ユーザが**アプリの境目をきれいに保つ方**を選んで取り下げた: 1 プロセスが
  本物の Asterism の接続を複数持ち、それぞれが丸ごと 1 つのアプリ (接続・生存の印・オブジェクト・`@profile`・`@pubs`) になる形にし、
  ゲームホストは `spectate` という 2 本目の接続を開く。その API は未決定。
- 取り下げは `git revert` の新しいコミットで戻した (asterism f8b44ff、asterism-games b9f6b0a、この report の追記も f81d9fd2 で戻した)。
  観戦のキーは 59cc130 の形 (Zenoh の素の put に `[seq]`、宣言なし) のまま。
- 試した中で分かったこと (次の API を決めるときの材料): 別名には生存の印が無いので `Asterism.apps` や重複の確かめに出ず、
  `*/*` の `@profile` の問い合わせには別名も答える (適合の見本が 1 つ変わった)。ルームのホストは観戦をルームの外の別の接続
  (Asterism の接続ではない) で流すので、別名の API ではルームの観戦を宣言できなかった。複数の本物の接続ならこの穴も埋まる。

### 決定 20 を入れた

- **asterism a071aa0** (`feat(protocol): any and repeated [] in the fields of application types`): フィールドの型の `[]` を
  重ねられる (`"int[][]"`)、基本の型に `any` (`"any[]"` など)。`"[][]"` だけは断る。protocol.md 11.2、README、試験 (CRuby の
  test_topic、mruby の host_test、適合の見本: 被試験側の型に `int[][]` と `any` のフィールド)。
- **asterism-games ad4cc3e** (`feat(host): the members, final ranks and pao back in the state types`): `mb` は `any[]`
  (オセロ・お絵かき・麻雀)、麻雀の `fin` は `int[][]`、和了の `pa` は `any[][]` (`[払う席, 役満の名前]` で混ざる)。型の説明から
  「載せていない」を消し、試験は「ビューのキーがすべてフィールドにある」を確かめる形に締めた。

### 試験 (`ASTERISM_CREDENTIALS` を外して)

| 何を | 結果 |
|---|---|
| asterism `rake test` | 145 runs、2594 assertions、0 failures (適合 cruby 241 passed、0 failed) |
| asterism `rake test:protocol_mruby` | 241 passed、0 failed |
| asterism `rake test:cross` | 1 run、24 assertions、0 failures |
| asterism `rake test:mruby` | 54 + 15 + 26 checks、0 failed |
| asterism-games `rake test` (`MJ_GAMES=4`、手元のルータ) | 406 runs、37363 assertions、2 failures、1 skip。落ちたのは `test_the_delay` (観戦の遅れの時間の比べ、556 > 556) と `test_network` (`stale` のはずが `not offered`)。どちらも型とは関係しない時間の揺れで、単独で 2 回ずつ流して全部通った (spectate 8 runs、network 1 run) |
| asterism-games `rake test:gem` | 165 runs、7074 assertions、0 failures |

## 10. 追記: P2b-2 (asterism-games を接続のオブジェクトに、観戦を 2 本目の接続に) (2026-11-13)

asterism の P2b-1 (接続のオブジェクト、report/p2b.md) の上で、asterism-games を新しい書き方に入れ替え、観戦を本物のアプリ
`spectate` にした (asterism-games b1bdeb6 `feat(host): spectate as a second application with typed topics`)。

### 変えた所

| ファイル | 何を |
|---|---|
| `lib/asterism_games/host.rb` | `::Asterism.reconnecting?` (4 か所) を `@net.reconnecting?` に。観戦: ルームの外 (常設の卓・単独のホスト) では、観戦の景色を出す卓があるとき (麻雀) に 2 本目の接続 `Asterism.connect(locator, node: <ホストのノード>, app: "spectate", credentials:)` を開き、`spectate_up` で型 (`SPECTATE_TYPES`) をその接続に `define_type` し直し、卓ごとに `<table>/state` を `publish(type: SPECTATE_TYPE)` で宣言する。開くのはホストの loop (ロックの外)、失敗・切れたら 30 秒ごとにやり直し、ホストの接続と一緒に閉じる。値は待ちの列のバイト列を解いて `topic.put` する (Asterism に MessagePack の済んだバイト列を渡す口は無いため) |
| `lib/asterism_games/game.rb`・`app/asterism_games/mahjong/table.rb` | `Game::SPECTATE_TYPE` (既定 nil)・`SPECTATE_TYPES` ({})。麻雀は `asterism-games/mahjong.spectate` (table・status・sv・seats・round・dora_indicators・last_discard・result・final `int[][]`・final_reason・rules・delay_s。round・last_discard・result・rules は `any`) と `asterism-games/mahjong.seat` (seat・name・kind・score・wind・riichi・river・riichi_index・called_indexes・melds `any[]`・hand_count・hand・drawn) |
| `lib/asterism_games/rooms.rb` | ルームのホストに `spectate_token` を渡す: 常設の卓のホストが観戦のアプリを開く (常設の卓に観戦の景色を出すゲームがある) ときは false |
| `bin/mahjong-bots`・`test/test_network.rb`・`test/test_app_presence.rb` | `Asterism.connect` の戻り値の接続で `net[...]`・`net.each`・`net.poll`・`net.close` (P3a の時点ではモジュールの関数のままで、P2b-1 の後は動かなかった) |
| `README.md`・`docs/install.md` | 「Topics and types」に `spectate` の表、「Spectate」に「The application」の項とルームの扱い、「One process per room」の理由 (同じ `<node>/games` は 1 つのプログラムで 1 回しか開けない)、`games-host` の役割の説明 |
| `test/` | 偽の網に `connected?`、`host_spectating` (2 本目の偽の接続)。観戦の値が `games/spectate` の宣言・型を持ち、ゲームの接続には型が無いこと、景色のキーがすべて型のフィールドにあること、型が Asterism の検査を通ること、ルームの観戦の番号、ルームのホストの `spectate_token` |

### ルームの観戦は入れなかった (P3a 9 章の穴は半分だけ埋まる)

- ルームのホストは、ルームごとに別のプロセスで、**常設の卓のホストと同じノード** (`rooms.credentials` の既定は常設の卓の証明書) で
  動く。観戦のキーはどれも `asterism/<node>/spectate/...` なので、本物のアプリにすると全部が同じ `<node>/spectate` になり、
  重複の確認 (protocol.md 6.3) で 2 つ目からは開けない。よって**ルームの観戦は今までどおり素の Zenoh の接続**で、宣言なし・番号つき。
- 新しく見つかった衝突: ルームのホストが今までどおり `asterism/<node>/spectate` の生存の印を立てると、後から開く常設の卓のホストの
  `spectate` の接続が「already on the network」で断られる (コンテナの再起動で順番が入れ替わると起きる)。常設の卓のホストが観戦の
  アプリを開くときは、ルームのホストは生存の印を立てないようにした。常設の卓が無い (または観戦の景色を出さない) ときは今までどおり立てる。
- 埋めるには、ルームの観戦を別のアプリ名 (`spectate-room01` など) に分けるか、常設の卓のホストがルームの値も受け持つか、になる。
  前者はキーと console の役割の行・実況の購読の形が変わる (利用者に見える仕様の変更) ので、決めていない (下の確認点 1)。

### 試験 (`ASTERISM_CREDENTIALS` を外して、手元の asterism)

| 何を | 結果 |
|---|---|
| asterism-games `rake test` (`MJ_GAMES=4`、使い捨てのルータ: CRuby の zenoh-c を router モードで開く台本) | **407 runs、44117 assertions、0 failures、1 skip** (skip は既存)。1 回目は `test_rooms` の偽の接続に `reconnecting?` が無くて 1 error、足して 2 回目で全部通った。`test_the_delay`・`test_network` の揺れは今回は出なかった |
| asterism-games `rake test:gem` | **165 runs、7081 assertions、0 failures** (10 skips は既存) |
| うち `test/test_spectate.rb` 単独 | 9 runs、1130 assertions、0 failures |

### 通し (CRuby どうし、手元のルータ)

ホスト (othello1・mahjong1・board1) を立て、asterism の CLI で見てから `asterism-games play othello --commands sit,cpu,start,auto
--until-over` で 1 局。

- `asterism app list`: `games/games` と **`games/spectate`** の 2 つ。
- `asterism topic list 'games/*'`: ゲームの 4 つに加えて **`games/spectate/mahjong1/state  asterism-games/mahjong.spectate`**。
  `topic info`・`app info games/spectate` で `mahjong.spectate` と `mahjong.seat` の定義が引けた。
- `object get games/spectate/mahjong1 state`: `format msgpack`、`seq` つきで値が来た。
- オセロ: CPU 41-23 で最後まで、exit 0。ホストのログに `games: spectate: games/spectate up (mahjong1/state)`。

### 確認してほしい点 (自分では決めていない)

1. **ルームの観戦**: 上の理由で宣言なしのまま。分けるならアプリ名・キーの形 (利用者に見える) を決める必要がある。
2. **ルームのホストの生存の印を止めたこと** (`spectate_token`)。常設の卓のホストがいない間 (落ちて再起動を待つ間) は、
   ルームの観戦の値に生きているアプリが無く見える。
3. **新しい型の名前** `asterism-games/mahjong.spectate`・`asterism-games/mahjong.seat` (今の名前の流儀に合わせた)。指示書の
   「卓の型を使う」は、観戦の景色が卓の状態 (`mahjong.state`) と形が違うので、観戦用の型を足してその接続に置く形に読んだ。
4. `docs/install.md` の `games-host` の役割は、今の行 (`*/@pubs`・`*/@profile`・`spectate`・`spectate/**`) で 2 本目も足りるので
   行は足さず、説明だけ直した。console 側で `spectate` の queryable の宣言 (`asterism/<node>/spectate/**`) が「Read and write」で
   通るかは console の規則の言葉の側 (P3b) で確かめる。
5. 2 本目を開くときの `Asterism.connect` は CRuby で全体のロックを持ったまま重複の確認 (最長 1.5 秒) をするので、ホストの起動直後に
   1 回だけ、その間ゲームの呼び出しの答えが止まる (report/p2b.md 8 章と同じ性質)。

## 11. 追記: P2c-2 (観戦を共有の置き場へ、ルームを Asterism のアプリに、CLI・MCP が共有の値を読む) (2026-11-13)

決定 23〜28 (design.md) と report/p2c.md の 9 章の引き継ぎに沿って、asterism-games の観戦をノードの共有の置き場へ移し、asterism の CLI・MCP が
共有の置き場の値を送り手つきで読むようにした (asterism 93668d1、asterism-games f009eaa)。asterism-console は別のサブ (23dd4ea・4e2bc1f) で、
ロールの行はそちらに合わせた。

### asterism-games (f009eaa `feat(host,commentate): ...`)

| ファイル | 何を |
|---|---|
| `lib/asterism_games/host.rb` | 観戦のキーを `asterism/<node>/@shared/spectate/<卓>/state` に (`spectate_name`・`spectate_key`)。ルームの外のホストは自分のアプリ (`games`) の接続で、つないだとき (`declare_topics`) に観戦の型を `define_type` し、卓ごとに `publish("spectate/<卓>/state", shared: :node, type:)`。**2 本目の接続 `spectate` と生存の印の仕組み (`spectate_token`・`SpectatePub`・`spectate_session`) は消した**。ルームのホストは、ルームの外に Asterism の接続 `<node>/room<slot>` (`open_room_app`・`room_up`) を開き、同じ型と `spectate/room<slot>-<卓>/state` を `shared: :node` で宣言する。開く・切れたら 30 秒ごとにやり直す・ホストの接続と一緒に閉じるのは前と同じ |
| `lib/asterism_games/rooms.rb` | `child_config` から `spectate_token` と `permanent_spectates?` を消した。`spectate_label` (`room<slot>`) はルームのアプリ名と卓の名前の頭を兼ねる |
| `lib/asterism_games/commentate/mahjong.rb`・`commentate.rb` | 購読を `*/@shared/spectate/*/state` に。ホストは送り手 (`info.from`) のノード、卓は `@shared/` の下の名前 (`spectate/<卓>/state`) から (`spectate_source`)。`--table` は `<host>/@shared/spectate/<卓>` も受ける |
| `lib/asterism_games/game.rb` | 説明の直し (`SPECTATE_TYPE` は共有の置き場のトピックの型) |
| `README.md`・`docs/install.md` | 「Topics and types」「Spectate」「Rooms」「Commentary」「Tests」、7 章のロール (下) |
| `test/` | 偽の網 `FakeNet` に `shared: :node` (キー `asterism/<node>/@shared/...`、宣言の項目 `<node>/@shared/...`、attachment `[seq, nil, "<node>/<app>"]`)。観戦の値がホストのアプリの宣言・型を持ち送り手つきで番号が付くこと、ルームのアプリ `games/room01` が宣言・型・値を持つこと、ルームのアプリを開くのは自分の証明書・観戦の景色のある卓・ルームの中だけで 1 回だけ (`Asterism.connect` を stub)、実況の `spectate_source` |

- **ルームのプロセスが素の Zenoh でしていたこと**: 観戦の値の put (と `spectate` の生存の印) だけだった。全部 Asterism のアプリ `room<slot>` に
  置き換えた。ほかに素の Zenoh は `follow_players` の 0.7.1 向けの代わり (`net.session.liveliness_watch`) だけで、protocol 2 の asterism
  (`publish(shared:)` を使う今のコード) では必ず `on_app_join` があるので消した。**素の Zenoh の部分は残っていない**。
- ルームのアプリは証明書のファイルが無くても開くようにした (`spectate_outside` だけを見る)。`spectate_outside` は Rooms が自分の証明書の
  ときだけ立てるので、配置での振る舞いは変わらない。素のルータでの通しのため。
- ルームのアプリは `@status` を置かない (状態は無い)。ノードの自己紹介 (`asterism/<node>/@profile`) は Asterism の接続がどれも答える
  (前からの作り)。

### asterism の CLI・MCP (93668d1 `feat(cli): ...`)

| ファイル | 何を |
|---|---|
| `lib/asterism/cli/values.rb` | パターンに `node/@shared/...`・`@shared/...` を許す (`@shared` は 1 つ目か 2 つ目の区切りだけ)。照合を Zenoh と同じく「`*`・`**` は `@` で始まる区切りに掛からない」に (`glob_parts?`。前は `Asterism.glob?`)。`shared?`・`sender` (attachment の 3 つ目、共有のキーだけ読む)・`lane` (キーと送り手)。`entry` に `from`、欠けは送り手ごと。`Store` は共有のキーを送り手ごとに 1 件持ち、`app` (alive・gone・restarted) は送り手のもの (送り手が無い共有の値は `app` を出さない)、`left` は送り手の値にも印を付ける |
| `lib/asterism/cli/nouns.rb` | `object get / echo` の PATH に `node/@shared/name...`・`@shared/name...`。文字の出力は `<キー> from <node>/<app> <値>`、`-o json` は `from`。echo の差分は送り手ごと |
| `lib/asterism/cli/mcp.rb` | 購読に `asterism/*/@shared/**` と `asterism/@shared/**` を足した (`VALUE_KEYS`)。`asterism_values` の説明・入力の説明・サーバの instructions に共有の置き場と `from` |
| `README.md`・`test/test_cli.rb` | 説明と試験 (`test_values_shared`、`test_topics` に共有の値の echo・get、`test_mcp` に共有の値) |

- `asterism_values` の既定 (`**`) は今までどおり**アプリのキーだけ** (Zenoh と同じく `@shared` に掛からない)。共有の値はパターンで
  `*/@shared/**` や `@shared/**` と名指しする。
- mrblib (Asterism の本体) は変えていない (`Asterism::Subscription.sender` を使っただけ)。

### ロールの行 (console と同じ形)

console のサブ (23dd4ea) の行に合わせた。ルームのアプリ名は `room<2 桁>` なので `room$*` に合う。

```
games-host  Read and write  asterism/${node}
                            asterism/${node}/games/**
                            asterism/${node}/@profile
                            asterism/${node}/*/@profile
                            asterism/${node}/*/@status
                            asterism/${node}/*/@pubs
                            asterism/${node}/@shared/**
                            asterism/${node}/room$*
                            asterism/${node}/room$*/**
            Read            asterism/*/game
            Watch           asterism/**

spectator   Read            asterism/*/@shared/spectate/**
                            asterism/*/games/@profile
                            asterism/*/games/@pubs
                            asterism/*/room$*/@profile
                            asterism/*/room$*/@pubs
            Ask             asterism/*/games/@profile
                            asterism/*/room$*/@profile
            Watch           asterism/**
```

- ルームのアプリの `@profile`・`@status`・`@pubs` は `asterism/${node}/*/@...` の行で足りる (ルームはホストと同じ証明書・同じノード)。
  足りないのは生存の印と queryable (`asterism/<node>/room01`・`asterism/<node>/room01/**`) で、それが `room$*` の 2 行。
- 実況が要るのは値の行 `asterism/*/@shared/spectate/**` だけ (型は引かない)。`@profile`・`@pubs` の行は `asterism topic info` や
  `type_of` で送り手の型を読むため。spectator に網全体の `@shared` は入れない。
- 一度は `asterism/${node}/**` と `asterism/*/*/@profile` の広い形を書いたが、親の指示で console と同じ狭い形に直した (コミットは狭い形のみ)。

### 試験 (`ASTERISM_CREDENTIALS` を外して)

| 何を | 結果 |
|---|---|
| asterism `rake test` | 0 failures (test_cli **30 runs、654 assertions**、前は 29 runs)、適合 `cruby: 243 passed, 0 failed` |
| asterism-games `rake test` (`MJ_GAMES=4`、使い捨てのルータ) | **408 runs、48120 assertions、0 failures、1 skip** (skip は既存)。1 回目は `test_rooms` の偽の網に `connected?` が無くて 1 error (足した)、`test_the_delay` (既知の揺れ) と `test_mahjong_watch` の 1 つが落ち、どちらも単独で通った |
| asterism-games `rake test:gem` | **166 runs、7081 assertions、0 failures** (10 skips は既存) |
| `test_the_delay` の揺れ | 変更後に単独 6 回で 1 回、変更前 (stash) で 6 回 0 回。表の景色の版が最後まで同じになる (局が止まる) ときの比べ (`>`) で、観戦の置き場とは関係しない見立て |

### 通し (CRuby どうし、ACL の無い使い捨てのルータ)

常設の卓のホスト (`games/games`、mahjong1 を CPU 4 人) とルームのホスト (名前空間 `room/01`、`spectate_outside`、ルームのアプリ
`games/room01`) を 1 つの台本 (scratchpad) で立てた。

- ホストのログ: `room01: games: games/room01 up outside room/01 (@shared/spectate/room01-mahjong1/state)`。
- `asterism app list`: `games/games` と **`games/room01`** (ルームの `games` は名前空間の中なので出ない)。
- `asterism topic list 'games/*'`: `games/@shared/spectate/mahjong1/state  asterism-games/mahjong.spectate  (node, by games/games)` と
  `games/@shared/spectate/room01-mahjong1/state  ...  (node, by games/room01)`。`topic info` でルームのアプリの型の定義が引けた。
- `object get games/@shared/spectate/mahjong1/state -o json`: `from` `games/games`、`format` msgpack。ルームの卓は `from` `games/room01`。
  `object echo '*/@shared/spectate' '*/state'` の文字の出力は `... room01-mahjong1/state from games/room01` の行。6 秒の `-o json` で
  2 つの送り手がそれぞれの番号の列 (14 件・19 件、欠け 0)。
- 実況 (`commentate mahjong --json --ai off`、25 秒): `games/mahjong1` 9 行・`games/room01-mahjong1` 11 行、両方の卓を追った。
- **ACL のあるルータ (zenohd + console のロール) では確かめていない**。上のロールの行で通るかは、console の側の試験とデプロイで見る。

### 止まらずに決めたこと・確認してほしい点

1. ルームのアプリを証明書のファイルなしでも開くようにした (`spectate_outside` だけを見る)。配置では Rooms が自分の証明書のときだけ立てる。
2. `follow_players` の 0.7.1 向けの代わり (素の Zenoh の liveliness watch) を消した。
3. CLI・MCP の照合を「`*`・`**` は `@` の区切りに掛からない」にした。`asterism_values` の既定 `**` に共有の値は入らない (名指しが要る)。
4. 共有の値で送り手の無いもの (形の悪い attachment) は、MCP で `app` を出さない。
5. console の `viewer` (Read `asterism/**`) は `@shared` に届かない。`object get` や MCP で観戦を見るには、viewer のロールに
   `asterism/*/@shared/**` (網全体なら `asterism/@shared/**`) が要る (console の側の判断)。
6. ブラウザ版 (fmruby-core) の観戦の購読はまだ `spectate` の下 (P3c)。

### 残件

- 版・CHANGELOG は上げていない。push していない。`docs/install.md` の「Updating」に版ごとの手順 (ロールの直し) は書いていない (P4 で版と一緒に)。
- ACL のあるルータでの通し。

## 12. 追記: P2d-2 (asterism-games の今の値の取り直し、ms の時刻) (2026-11-13)

design.md の決定 30〜33 と report/p2d.md の 10 章の引き継ぎに沿って、asterism-games の卓の状態・観戦の読み手が、欠け・つなぎ直しのときに
ホストの呼び出しで今の値を取り直すようにした (asterism-games c5660f6 `feat(host,commentate): ...`)。asterism・asterism-console・fmruby-core の
ソースは変えていない。

### 今の値を答える呼び出しがあるか (決定 31)

| 読み手 | 今の値を答える呼び出し | 結果 |
|---|---|---|
| 遊ぶ側 (`play othello`・`play mahjong`)、常設の卓・ルームの卓 | 卓のオブジェクトの `view(token)` (ルームの卓は名前空間の中で同じ) | **あった**。足していない |
| 観戦の読み手 (実況 `commentate`) | 無かった。卓の `table` は手の無い景色で、観戦の値 (4 人の手・遅らせた景色) ではない。値はホストが遅らせて持つ (`@spec`) ので卓は知らない。spectator のロールは卓のオブジェクトに届かない | **足した** (下) |

- **足したもの**: 観戦のキーを宣言するアプリ (常設はホストの `games`、ルームは `room<slot>`) が、オブジェクト `spectate` を公開する。
  `state(table)` が `@shared/spectate/<table>/state` に**最後に put した値** (キーと同じだけ遅れた値、まだ put していなければ nil) を答える。
  `table` はキーの名前の `spectate/` の下 (ルームは `room01-mahjong1`)。宛先は `games/games/spectate`・`games/room01/spectate`。
  卓に `spectate` という名前があるときは公開しない (卓の名前を優先、ログに出す)。端末のロビー (`games/mahjong*`・`games/othello*`) と
  `play` の卓探し (名前の頭で絞る) には出てこない。
- 遅らせた値だけを答えるので、観戦の遅れ (麻雀の観戦者の遅れ) を呼び出しで追い越せない (ホストの再起動の直後は put するまで nil)。

### 読み手の取り直し

| 読み手 | いつ | 何を |
|---|---|---|
| 実況 (`Commentate::Refetch`) | 卓ごとに、送り手の**最初の値**、`info.lost > 0`、`info.restarted?`、送り手が網を去った後の次の値 | 送り手 (`info.from`) の `spectate` の `state(<卓>)` を呼び、答えを hub に渡す。呼んでいる間に新しい値が来ていたら新しい値を残す。卓ごとに 1 つずつ、受信のスレッドの外 (専用のスレッド)。失敗はログに出して次の値で続ける |
| 遊ぶ側 (`Play.ask_again?`) | `info.lost > 0` か `info.restarted?` の状態の値 | 版 (`v`) が同じでもすぐ `view` を呼ぶ (最初・ホストが戻ったとき・5 秒ごとの `view` は前から) |

- **最初の値と leave の扱いを足した理由**: 通しで、ルームを kill -9 してすぐ起こすと、実況は `restarted?` を一度も見なかった。kill で TCP が
  切れてルータがすぐ生存の印を消すので、読み手は先に leave を受けて番号の記憶を捨て (決定 29)、新しい回の値は「最初の値」(`lost` 0・
  `restarted?` false) として来る。design.md の「今の値」は「最初と、`lost > 0` か `restarted?` のとき」なので、最初の値でも取り直すことにし、
  `on_app_leave` で送り手を忘れて、戻った後の値を最初の値として扱う。`restarted?` が立つのは、読み手が leave を見る前に送り手の新しい回の
  値が届いたときだけ。
- 観戦の値・卓の状態は毎回まるごとの状態なので、欠けの後に届いた値そのものが今の状態でもある。取り直しの答えは多くの場合それと同じで、
  実況の hub の差分は何も出さない (害はない)。

### 決定 30・32・33

- **決定 30 (時刻)**: 記録簿の `rec["time"] = now.to_i` (秒) は、記録簿の JSON ファイル・SQLite の形 (統計の `days` の絞りにも秒で使う) で、
  そのまま残した。ただし**網に出ていた**: 麻雀の卓の `record(id)` の答え (記録の頭) が `time` を入れていて、README に「Unix seconds」と
  書いてあった。答えの側で **ms (1970 年から)** にし (`time * 1000`)、README を直した。端末の麻雀アプリ・console は `time` を読んでいない
  (grep で 0 件、端末は `at` の文字列を出す)。ほかに網に出る時刻は無かった (状態の `dl` などは残りの ms、`@status` に時刻は無い。ホストの
  `status_now` の `at` は supervisor が読むローカルのファイル)。
- **決定 32**: 整数のフィールドの型は残っていなかった (`define_type` が整数を断るので、試験が全部通ったことで確かめた)。
- **決定 33 / attachment**: games は attachment を直に読んでいなかった (直すところ無し)。試験の偽の接続 `FakeNet` の番号を新しい形
  (`[seq, run_id]`・`[seq, run_id, nil, "<node>/<app>"]`) にし、公開したオブジェクトを記録するようにした。

### 試験の直し (試験だけの不具合)

`test_network` の揺れの 1 つは、試験の不具合だった。`test_spectate.rb` の `test_who_opens_a_rooms_application` (P2c-2 で足した) が
Minitest の `stub` で `Asterism.connect` を差し替えると、`stub` の alias と戻しが、前に prepend されている `LockedModule#connect`
(asterism の CRuby 部分) を super の輪にし、同じプロセスのその後の本物の `connect` が全部 `SystemStackError` になる。試験の順番で
`test_network` が後に来ると落ちていた (1 回目の `rake test` で再現、最小の台本でも再現)。`stub` をやめ、試験の側で切り替えの
モジュールを 1 回 prepend する形にした。asterism は変えていない。

### 試験 (`ASTERISM_CREDENTIALS` を外して、手元の asterism)

| 何を | 結果 |
|---|---|
| asterism-games `rake test` (`MJ_GAMES=4`、使い捨てのルータ) | 1 回目: 411 runs、1 failure (`test_the_delay`、既知の揺れ)・1 error (`test_network` の `SystemStackError`、上の試験の不具合)。`test_network` は単独で通った。直した後の 2 回: **411 runs、0 failures、0 errors、1 skip** (skip は既存) |
| asterism-games `rake test:gem` | **168 runs、7109 assertions、0 failures** (10 skips は既存) |
| 足した試験 | `test_spectate.rb` (ホストのアプリとルームのアプリの `spectate` の答え、遅れの間は nil、ほかの卓は nil、`spectate` という卓では公開しない)、`test_commentate.rb` (最初の値・欠け・つなぎ直し・leave の後で 1 回ずつ聞く、新しい値が勝つ、失敗、送り手なし)、`test_play.rb` (`Play.ask_again?`)、`test_mahjong_records.rb` (記録の頭の `time` が ms) |

### 通し (CRuby どうし、ACL の無い使い捨てのルータ)

ルームのホスト 1 つ (名前空間 `room/01`、ルームのアプリ `games/room01`、麻雀を CPU 4 人) と実況 (`commentate mahjong --json --ai off`) を立てた。

- 実況の起動: `games/room01-mahjong1: the first value from games/room01; asking games/room01/spectate for the table now` →
  `took the table now from games/room01`。
- ルームのホストを kill -9、0.2 秒後に起こした (state 664 から再開、`games/room01 up outside room/01`)。実況は同じ 2 行
  (最初の値 → 取り直し) を出し、そのまま実況を続けた (30 秒で 15 行増えた、次の局の始まり・テンパイなど)。
- `asterism call games/room01/spectate state '"room01-mahjong1"'` は観戦の値 (4 人の手つき) を、無い卓は nil を返した。
- 最初の版 (欠け・`restarted?` だけで取り直す) では、同じ kill と再起動で取り直しが起きなかった (上の「最初の値と leave」の理由)。
- **欠け (`lost > 0`) と、leave より先に新しい回の値が届く場合 (`restarted?`) は通しで起こせていない** (単体の試験だけ)。
- **ACL のあるルータでは確かめていない**。

### 止まらずに決めたこと・確認してほしい点

1. **新しい公開のオブジェクト `<node>/games/spectate`・`<node>/room<slot>/spectate` (`state(table)`)**。指示 (無ければ足す) どおりに
   足した。名前と形は仮決め。卓の名前 `spectate` とぶつかったら卓を優先する。
2. **spectator のロールに行が要る**: Read と Ask の `asterism/*/games/spectate/call/${node}` と `asterism/*/room$*/spectate/call/${node}`
   (`docs/install.md`・README に書いた。`@profile` の行と同じく Read で答えが届く見立て、ACL で確かめていない)。console の
   docs/rooms.md とロールの既定 (console のサブの側) にも同じ行が要る。行が無いと実況は呼び出しの失敗をログに出して、次の値で続ける
   (今までと同じ見え方)。games-host の行 (`asterism/${node}/games/**`・`asterism/${node}/room$*/**`) は今のままで足りる。
3. **実況は送り手の最初の値でも取り直す** (起動時に卓ごとに 1 回、送り手が戻ったときに 1 回)。design.md の「最初と」に合わせた。
4. **`record(id)` の `time` を秒から ms に変えた** (README の答えの説明が変わる、利用者に見える)。指示 (網に出るなら ms) どおり。
   記録簿のファイル・データベースは秒のまま。
5. ブラウザ版 (fmruby-core) の観戦・卓の読み手の取り直しは P3c。

### 残件

- 版・CHANGELOG は上げていない。push していない。
- ACL のあるルータでの通し (spectator のロールの新しい行を含む)。
