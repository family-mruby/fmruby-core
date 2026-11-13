# P2c report: 共有の置き場 `@shared` (asterism、P2c-1)

> 状態: 済 (P2c-2 への引き継ぎあり) | 更新: 2026-11-13 | asterism に共有の置き場 (ノードごと `asterism/<node>/@shared/<名前>` と網全体 `asterism/@shared/<名前>`) を入れた (決定 23〜25 と、途中で足された「型は送り手の定義で読む」)。`net.publish(..., shared: :node | :global)`、送り手つきの attachment `[seq, ms または nil, "<node>/<app>"]`、送り手ごとの欠けの数え方、`@pubs` の項目の形、`publishers_of`・`declaration_of`・`type_of`、CLI と MCP の表示、protocol.md 17 章と README、試験 (CRuby・mruby・cross・適合)。asterism 05b3eac・15fb6aa。asterism-games・console・fmruby-core は触っていない (P2c-2)

## 1. 足した API

| API | 中身 |
|---|---|
| `net.publish(name, ..., shared: :node)` | `asterism/<自分のノード>/@shared/<name>` に流す。型・単位・`ts`・encoding は今までどおり。宣言は自分のアプリの `@pubs` に入る |
| `net.publish(name, ..., shared: :global)` | `asterism/@shared/<name>` に流す |
| `shared:` の値 | `nil` (既定、自分のアプリの下)・`:node`・`:global` (String も可)。それ以外は ArgumentError |
| `topic.shared` / `topic.entry` | 置き場 (`nil`・`:node`・`:global`) と、`@pubs` での項目名 |
| `net.published` | 項目名の一覧 (共有のものは `"<node>/@shared/<name>"`・`"@shared/<name>"`) |
| `net.unpublish(topic)` / `net.unpublish(name, shared: :node)` / `net.unpublish("@shared/x")` | 取り下げ |
| `net.subscribe("*/@shared/spectate/**")` / `net.subscribe("@shared/lobby/**")` | 共有の置き場の購読。キーの形をそのまま `asterism/` の下に付けるだけなので、作りは今のまま |
| `info.from` / `info.shared` / `info.path` | 送り手 `"<node>/<app>"` (自分のアプリの下の値ではキーから)、置き場、`asterism/` の下のキー。共有の値では `info.node`・`info.app` は送り手のもの、`info.name` は `@shared/` の下の名前 |
| `net.publishers_of(path, timeout: 2.0)` | 共有のキー (`@shared/<name>`、`<node>/@shared/<name>`、ノードに `*` 可) を宣言しているアプリ `{"<node>/<app>" => 宣言}`。`asterism/<node か *>/*/@profile?pubs` に get を 1 つ出して答えを全部集める。自分のアプリの分は網を使わずに足す |
| `net.declaration_of(info)` | 受け取った値の宣言を、その送り手 (`info.from`) の `@pubs` から |
| `net.type_of(info)` | 受け取った値の型を送り手から: 組み込みは番号、独自の型は `[名前, 送り手の定義]` (`@profile?pubs;types` の問い合わせ 1 つ) |
| `Asterism::Pubs.place(entry)` / `entry` / `path` | 項目名の読み分け (`[nil, nil, name]`・`[:node, node, name]`・`[:global, nil, name]`、読めなければ nil) と組み立て |

例:

```ruby
t = room.publish("spectate/t1/state", shared: :node, type: "asterism-games/table")
t.put(state)                     # asterism/host/@shared/spectate/t1/state, attachment [0, nil, "host/room01"]
g = net.publish("lobby", shared: :global, type: :text, ts: true)
g.put("hello")                   # asterism/@shared/lobby, attachment [0, 1794556800123, "pc/chat"]

viewer.subscribe("*/@shared/spectate/**") do |v, info|
  info.from                      # => "host/room01"
  info.lost                      # 送り手ごとに数える
  viewer.type_of(info)           # => ["asterism-games/table", {"f" => [...], "d" => ...}] (room01 の定義)
end
viewer.publishers_of("host/@shared/spectate/t1/state")
# => {"host/games" => {"t" => "asterism-games/table"}, "host/room01" => {...}}
```

名前は指示書の案 (`shared: :node` / `:global`) のまま。`publishers_of`・`declaration_of`・`type_of` は自分で決めた名前。

## 2. 宣言の項目の形 (`@pubs` のキー)

- 自分のアプリのトピック: 今までどおり名前 (`imu`、`board1/strokes`)。
- ノードの置き場: `"<node>/@shared/<name>"`。網全体: `"@shared/<name>"`。どちらも **`asterism/` の下のキーそのもの**
  (読み手は `asterism/` を前に付ければ値のキーになる)。
- 取り違えは起きない: トピックの名前の区切りは `@` で始まれない (2.1) ので、`@shared` を含むものは必ず共有のもの。
- 読み手 (`Pubs.check`) はこの 3 つの形だけを残す。区切りが名前でないもの・ほかの `@` の区切り (予約)・
  **宣言したアプリのノードと違うノードの置き場** (`pubs_of`・`@pubs` の put を読むとき) は落とす。前は名前ならどんな String でも
  残していたので、`a/*` のような名前は今は落ちる (適合の見本に足した)。
- 短くする案 (ノードの置き場を `@shared/...`、網全体を別の印に) も考えたが、`asterism/` の下のキーと同じ形のほうが読み手が
  組み立てを覚えずに済み、ノード名が宣言に入っても `@pubs` 1 回あたり数バイトなので、明示のほうを採った。

## 3. attachment と欠けの数え方

- 共有の置き場の値: いつも 3 要素 `[seq, ms または nil, "<node>/<app>"]`。`ms` は `ts: true` のときだけ、それ以外は MessagePack の nil。
  アプリの下の値は今どおり `[seq]` / `[seq, ms]`。大きさは `[seq]` より `c0` 1 バイト + 送り手の文字列 (`"host/room01"` で 12 バイト) 多い。
- 読み手は共有のキーを**キーと送り手の組ごと**に数える (`@last` の鍵を `"<key>\n<from>"` にした)。送り手が無い・`"node/app"` の形でない
  ときは送り手なし (キーだけで数える。ノードの置き場ならキーのノードを `info.node` に)。アプリの下のキーでは 3 つ目の要素を読まない。

## 4. 型は送り手の定義で読む (途中で足された決定)

- 同じキーの複数の送り手が同じ型の名前を違う定義にしてよい。読み手は `info.from` の送り手の `@profile?types` で読む。
- `net.type_of(info)` / `net.declaration_of(info)` がそれをする。CLI の `topic info <共有のキー>` は宣言しているアプリごとに、
  そのアプリ自身の宣言と型の定義を出す。MCP の `asterism_topics` はもともとアプリごとに `topics` と `types` を返す形なので、
  共有の項目に `"shared"` を付け、説明文に「型は送り手のものを読む」を書いた。
- protocol.md 11.2 と 17 章に決まりとして書いた。試験: test_shared (CRuby、`host/games` と `host/room01` が `g/table` を別の定義で)、
  host_test/test_connections (mruby、`host/game` と `host/spectate` が同じく)。

## 5. protocol.md と README

- **17 章 Shared places** を足した (章の番号を動かすと 94 か所の参照が動くので、末尾 (付録の前) に置いた)。17.1 なぜ、17.2 2 つの
  置き場と使い分け (ノードが出すものの写しはノードごと、網全体の取り決めの場は網全体。網全体は信頼できるメンバーどうしのデータの
  やり取りと網の状態の知らせのため)、17.3 値と attachment と送り手ごとの数え方、17.4 ACL (ノードごとは `asterism/${node}/@shared/**` で
  自分のノード、網全体は管理者がロールで許したものだけ、既定ではだれも書けない、どのロールに最初から許すかは配置の選択)、
  17.5 Ruby の API、17.6 複数の送り手。
- ほかに 1 章 (モデル)、2.2 (キーの表と、`@shared` が verbatim なので `asterism/**` やアプリ向けの規則が届かないこと)、9.3・9.4・9.5、
  10.1 (項目の形と読み手の掃除)、11.2 (型は送り手のもの)、15.3・15.4 (見本の版 6)、16 (2 つの送り手の例とバイト列) を直した。
  1.1 の「観戦の 2 本目の接続」の例は「ゲームホストとルーム」に変えた。
- README: 「Shared places」の節、API 表の行 (`publish` の `shared:`、`publishers_of`、`declaration_of` / `type_of`)、`info` の項目、
  Several connections の例 (`games` と `room01`、観戦は `shared: :node`)。
- **既定の ACL は何も変えていない** (asterism に既定の ACL は無い。console の既定のロールは P2c-2)。

## 6. 試験

すべて `env -u ASTERISM_CREDENTIALS` で流した。

| 何を | 結果 |
|---|---|
| `rake` (CRuby、全部) | **151 runs、2765 assertions、0 failures** (前は 148 runs・2678)、msgs 2775 checks、適合 `cruby: 243 passed, 0 failed` |
| うち新設 `test/test_shared.rb` | **3 runs、74 assertions**: 項目の形 (`place`・`entry`・`path`・`check` のノードの絞り)、送り手ごとの読み (欠け・つなぎ直し・送り手なし・形の悪い送り手・アプリの下のキー)、プロセス内のルータで 4 本の接続 (`host/games`・`host/room01`・`pc/chat`・`viewer/v`): ノードの置き場に 2 つの送り手 (別々に数える、`ts` の有無)、網全体の置き場、`**` が共有に届かない、`type_of` が送り手ごとの定義を返す、`declaration_of`、`publishers_of` (ノード指定・`*`・網全体・無い・自分を含む)、取り下げ。4 回流して揺れなし |
| `test_cli.rb` の `test_topics` | 共有のトピックの `topic list` (テキスト・JSON)、`topic info` (ノードの置き場・網全体・無いキー) を足した (+13 assertions) |
| `rake test:protocol` / `test:protocol_mruby` | 各 **243 passed、0 failed** (+1 informational、前からの「method name as bin」)。見本は版 6 |
| `rake test:mruby` | **65 + 15 + 25 checks、0 failed** (test_profile に共有の置き場の読み・宣言・attachment・取り下げを 10 足した) |
| `rake test:cross` | `cross_mruby.rb` **1 run、32 assertions** (mruby のノードの置き場を CRuby が読む: 送り手・番号・`type_of`・`publishers_of`。CRuby の網全体の置き場の値を mruby が読む: 送り手・欠け)、`host_test/test_connections.rb` **34 checks** (mruby の 3 本の接続、ルータ経由: 同じノードの 2 つのアプリが 1 つのキーに書き、別々に数える、同じ型の名前の別の定義を送り手ごとに読む、`publishers_of`、網全体)。3 回流して揺れなし |
| zenohd 1.10.1 (docker) をルータに | test_shared 3/74、test_topic 7/142、test_connections 2/65、`test:protocol` 243 passed |

- 適合の見本 (版 6) で足したもの: 被試験側が `score` (ノードの置き場、number) と `lobby/hello` (網全体、text、ts) を宣言し、`@profile` と
  `@pubs` の答えに共有の項目が入る。値の put に 2 つ (attachment `[0, nil, "iut/conf"]` と `[0, int, "iut/conf"]`)。driver が
  `asterism/drv/@shared/feed/s` に送り手 `drv/a`・`drv/b` を交互に (a に欠け 1、b は別に数える)、送り手なし、形の悪い送り手、
  `asterism/@shared/feed/g` を流し、報告の行の末尾に `from` を足した。`pubs_of` の読み手の見本に、共有の項目 (残る)・ほかのノードの
  置き場・ほかの `@` の区切り・`@shared` だけ・`a/*` (落ちる) を足した。
- **test:cross の「2 つのアプリが 1 つのキーに」は mruby どうし (host_test/test_connections.rb、CRuby のルータ経由) で確かめた**。
  CRuby ↔ mruby の peer の組み (cross_mruby) で CRuby の 2 本目の接続を mruby の listen につなぐと、zenoh-pico が 2 本目の peer を
  落とした (`Dropping peer due to processing error`、2 本目の put が `session is closed`)。cross_mruby は送り手 1 つずつにし、
  2 つの送り手は test_shared (CRuby) と test_connections (mruby) で見る形にした。

## 7. 見立てと違った点・気づき

- 1 本の接続に購読が 2 つあるとき、ブロックは**購読ごとに**まとめて呼ばれる (購読 1 の値を全部、次に購読 2)。到着の順ではない。
  前からの作り (`deliver_samples`) で、適合の見本は網全体の値を最後に置いた。protocol.md 15.3 に書いた。
- zenoh-pico の publisher が購読者を知る前の最初の put を落とすことがある (P1・P2 の既知) ので、mruby の試験は「両方の送り手の値が
  届くまで流す → 0.3 秒 poll して残りを受け取る → そこから欠けを作る」の順にした (最初は残りの値が混ざって 1 度落ちた)。
- `**` と `<node>/**` は `@shared` に届かない (verbatim)。購読する側は `*/@shared/**` と `@shared/**` を別に書く。CLI の
  `object get/echo` と MCP の `asterism_values` は `asterism/**` を購読しアプリごとに値を覚える作りなので、**共有の置き場の値は出ない**
  (今回は触っていない。8 章)。

## 8. 止まらずに決めたこと (確認してほしい点)

1. 項目の形: `<node>/@shared/<name>`・`@shared/<name>` (`asterism/` の下のキーそのもの)。2 章。
2. 共有の値の `info.node`・`info.app` は送り手のもの (キーのノードではない)。送り手が無いノードの置き場の値だけ、キーのノード。
   `info.path` はキーの形 (`host/@shared/...`)。
3. `Pubs.check` が読めない項目名・ほかのノードの置き場を落とすようにした (前は名前ならどれも残した)。
4. `publishers_of` は網全体のアプリに get を 1 回出す (ノードの置き場でノードを指定すればそのノードだけ)。答えを全部集めるので、
   時間切れまで待つことがある (ルータがあれば、答える相手が全部答えた時点で get が終わる)。
5. protocol.md の章は 17 (末尾) に置いた。

## 9. P2c-2 への引き継ぎ

### asterism-games

- 観戦は `asterism/<node>/@shared/spectate/<卓>/...` へ (決定 26)。常設のホストは自分のアプリ (`games`) の接続で
  `games.publish("spectate/<卓>/state", shared: :node, type: ...)`、ルームのプロセスは自分を Asterism のアプリ (`room01` など) として
  `Asterism.connect` し、同じく `shared: :node` で宣言する。**2 本目の接続 `spectate` は消す**。
- 型は送り手ごと: ホストとルームのそれぞれが `define_type` で置く (同じ名前で同じ定義にしておく。違っても読み手は送り手で読む)。
- 観戦する側 (実況・ブラウザ版) の購読は `"<ホストのノード>/@shared/spectate/<卓>/**"` か `"*/@shared/spectate/**"`。`info.from` で
  送り手 (ホストかルームか) が分かり、欠けは送り手ごと。ルームの出入りで送り手が変わっても、それぞれ別の番号の列として読める。
- 卓の一覧は `publishers_of("<node>/@shared/spectate/<卓>/state")` か、各アプリの `pubs_of` の共有の項目から作れる。

### asterism-console

- **図**: `@pubs` の共有の項目 (`Pubs.place` で `:node`・`:global`) を、アプリの下ではなく置き場 (ノードの `@shared`、網全体) の
  節点として描き、宣言しているアプリから線を引くのが素直。`pubs_of`・CLI の Survey (`topic_entry` の `"shared"`) が材料。
- **記録 (MCAP)**: channel を「キー」にすると共有のキーで送り手が混ざる。通し番号は送り手ごとなので、channel を「キー + 送り手」
  にするか、message の sequence を送り手ごとに読む必要がある。schema は送り手の宣言・型から (送り手ごとに違いうる)。
  購読は `asterism/**` に加えて `asterism/*/@shared/**` と `asterism/@shared/**` が要る (verbatim)。
- **ロール** (既定の ACL は今回変えていない、網全体の既定はユーザに確認中):
  - 読み手 (spectator・図・記録): `asterism/*/@shared/**` (と要るなら `asterism/@shared/**`) の subscription。spectator は今の
    `spectate` アプリの下の規則を `asterism/*/@shared/spectate/**` に置き換える。
  - 書き手 (games-host・ルームのアプリ): `asterism/${node}/@shared/**` の put と publisher の宣言 (`declare_publisher` 相当、zenohd の
    ACL の `put` で足りるかは console の ACL の書き方で確認)。ルームは別のアプリ名 (`room01`…) で `@profile`・`@status`・`@pubs` の
    規則にも掛かる。
  - 網全体: `asterism/@shared/**` の put は管理者が許したロールだけ。
- **MCP の `asterism_values` と CLI の `object get/echo`** は共有の置き場の値を拾わない (7 章)。console や MCP で観戦の値を見たいなら、
  そこを足す (アプリが去っても消さない値として、送り手ごとに持つ)。
- console の `Transfer::Endpoint` には関係しない。

### fmruby-core (P3c)

- 端末のアプリで共有の置き場に流すものは今のところ無い。ブラウザ版の観戦の購読は games と同時に `*/@shared/spectate/**` へ移す。
- mrblib (Ruby) だけの変更で、内蔵 RAM の静的な増分は無い見込み (端末のビルドは通していない)。共有の値は attachment が
  送り手の文字列の分 (十数バイト) 大きい。

## 10. 残件

- 版・CHANGELOG は上げていない。push していない。
- 端末 (esp32・wasm) のビルドは通していない。
- 網全体の置き場の既定の ACL (だれに最初から許すか) はユーザに確認中で、決まりの文書では「配置の選択」とだけ書いた。
