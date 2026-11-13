# P2b report: 接続をオブジェクトにした (asterism、P2b-1)

> 状態: 済 (P2b-2 と P3c への引き継ぎあり) | 更新: 2026-11-13 | asterism の `Asterism.connect` が `Asterism::Connection` を返し、モジュールが持っていた状態 (セッション・オブジェクト・状態・トピック・型・購読・生存の印・呼び出しの待ち・ブロック) を全部接続が持つようにした。アプリが使うモジュールの関数は消し (別名なし)、接続のメソッドにした。1 つのプログラムが複数の接続 (= 複数のアプリ) を持てる。ノードの自己紹介 `@profile` は、同じノードの接続のどれが答えても同じ中身になる。CRuby の `Asterism::Net` は接続に畳んだ。試験は全部新しい書き方にし、複数の接続の試験を CRuby と mruby に足した (asterism 2152661・81b3707)。部品 (picoruby-asterism-zenoh) に全体の状態は無く、触っていない

## 1. 消した関数と移した先

モジュールに残したのは `Asterism.connect`、新しい `Asterism.connections`・`Asterism.poll_all`・`Asterism.profile_defaults`、
状態を持たない部品 (`Codec`・`Profile`・`Status`・`Types`・`Pubs`・名前の検査・`now_ms` など) だけ。

| 消した関数 | 移した先 (`net = Asterism.connect(...)`) |
|---|---|
| `Asterism.expose` / `unexpose` / `exposed` | `net.expose` / `unexpose` / `exposed` |
| `Asterism[path]` / `Asterism.each` / `nodes` / `apps` | `net[path]` / `net.each` / `net.nodes` / `net.apps` |
| `Asterism.poll` | `net.poll` (全部の接続をまとめて回すのは `Asterism.poll_all`) |
| `Asterism.on_join` ほか `on_*` / `off_*` | `net.on_join` ほか |
| `Asterism.current_call` | `net.current_call` (その接続が答えている呼び出し) |
| `Asterism.session` / `connected?` / `reconnecting?` / `node_id` / `app` / `lost_reason` / `close` | `net.session` ほか同名。`net.path` (`"<node>/<app>"`) を足した |
| `Asterism.status` / `status_of` | `net.status` / `net.status_of` |
| `Asterism.publish` / `unpublish` / `published` / `subscribe` / `unsubscribe` / `define_type` / `pubs_of` / `types_of` | `net.` の同名 |
| `Asterism.profile(**fields)` | `net.profile(**fields)` (ノードの分)。ファームの既定は `Asterism.profile_defaults(**fields)` (新設、3 章) |
| `Asterism.profile_of` | `net.profile_of` |
| `Asterism.meta` / `call_async` / `profile_async` / `app_profile_async` (内部) | `net.` の同名 (CLI が使う) |
| CRuby `Asterism.net` / `Asterism::Net` | 接続そのもの。`connect { \|net\| }` は接続を渡す |
| CRuby `net.session` (`Zenoh::Connection`) | `net.zenoh`。`net.session` は両方で素の `Session` |
| `Transfer::Sender.new(to, kind, path, ...)` | `net:` (呼び出しを出す接続) を必須のキーワードに足した |
| `CLI::Survey.new(timeout:)` | `Survey.new(net, timeout:)` |

- クラス名は `Asterism::Connection` (指示書の案のまま)。`Topic`・`Subscription`・`Future`・`Proxy` は作った接続を持つ
  (`topic.connection`、`proxy.asterism_connection`)。
- 状態・型・トピックは接続のもの。前は「プロセスのもの、接続をまたいで残る」だったので、**接続の前に状態や型を置く書き方は無くなった**
  (接続の直後に置く。置いたときに put と `@profile` の queryable の宣言が出る)。閉じた後も読めるが、どこにも出ない。
- 前の 「`Asterism.close` のあと `connect` し直すと同じ状態・型が残る」も無い。つなぎ直しは新しい接続を作り、置き直す。
  ノードの自己紹介だけは残る (ノードのものなので)。
- 中で全体に 1 つ持つもの (状態ではなく、プログラムに 1 つしかないもの): 開いている接続の一覧 (`Asterism.connections`)、
  待ちの入れ子の深さ (C のスタックは 1 本なので全部の接続で数える)、ノードごとの自己紹介の表 (3 章)、既定のアイコンの読み込み。

## 2. 複数の接続の振る舞い

- 同じ `node` で別の `app` の 2 本は、ほかのアプリとまったく同じにふるまう: 互いを `apps`・`on_app_join` で見て、ルータ (の ACL) を
  通して呼び合い、相手の値を購読で受け取る (自分の値は自分に戻らないが、もう 1 本には届く)。
- **待っている呼び出しは、ほかの接続の呼び出しにも答える** (`Asterism.pump_others`)。一方が他方を呼んで待つと、待っている間に
  相手の queryable が回らないと時間切れになるため。答えるだけで、相手の `on_join`・購読のブロックはその接続の `poll` を待つ。
  入れ子の上限 `MAX_NESTING` (4) は全部の接続の待ちを合わせて数える。
- 重複の確認は `node/app` の組ごと。網の確認 (protocol.md 6.3) の前に、同じプログラムの接続を見て即座に断る
  (`"... is already connected in this program"`)。
- 片方を閉じても他方は動き続ける。失った接続は、`on_leave` を伝え終えたら `Asterism.connections` から抜ける。
- CRuby: ロックは全部の接続で 1 つ (`Asterism::LOCK`)。接続ごとに受信のスレッドを持てる (`net.start`)。待ちが他の接続を回すので、
  接続ごとのロックにすると 2 つのスレッドが互いのロックを待つ形ができるため、1 つのままにした。
- **zenoh-pico のセッションどうしは同じプロセスの中で peer にできない**: 2 本目の `Session.open` の握手が、1 本目の poll を
  待つ間に時間切れになる (-120)。端末はルータのクライアントなので困らないが、mruby の試験はルータ (CRuby の zenoh-c を router
  モードで開く `test/router.rb`) を挟んだ。

## 3. ノードの自己紹介 `@profile` (案と実装)

- **ノードの自己紹介はノードのもの、接続のものではない**として、プログラムの中でノードの名前ごとに 1 つ持つ。
  `net.profile(name: ...)` はその接続のノードの分を変え、同じノードのほかの接続にも同じに見える。どの接続が
  `asterism/<node>/@profile` に答えても同じ中身になる (試験で確認)。変わったときの put は、そのノードの接続の 1 本から 1 回だけ出す。
- **プログラムの既定** `Asterism.profile_defaults(kind:, name:, host:, icon:)` を新しく置いた。ノードが自分で決めていない項目に使う。
  ファームが接続より前に端末のことを書く場所が要るため (Family mruby の `fmrb-asterism.rb` は今 `Asterism.profile(kind:, host:)` を
  起動時に呼んでいる)。重なりは「ノードの項目 → プログラムの既定 → 組み込みの既定」。ノード側の `nil` (`host: nil`) は既定を
  隠して項目を消す。`kind: nil` は既定に戻す (前の `profile` と同じ意味)。
- ノードの項目は接続を閉じても残る (同じノードでつなぎ直すと同じ自己紹介)。前の「プロセスのもの、close をまたいで残る」と同じ。
- 退けた案: 各接続が自分の自己紹介を持つ (同じノードで中身がずれる)。ノードの queryable を 1 本目だけが立てる (閉じる順で
  受け渡しが要り、つなぎ直しが絡む。節約は 2 本目以降の 1 つ分 約 650 B (PSRAM) + 96 B なので、普通 1 本の端末では効かない)。
- 別のプログラム (端末の 2 つの VM) の同じノードは、今までどおりそれぞれが答える。同じ既定を置けば同じになる。

## 4. 端末で 1 本ごとに使う資源

### 測り方 (P1 と同じ)

i386 (`-m32`、`MRB_INT64`・`MRB_NO_BOXING`) で mruby を作り、scratchpad の写しで zenoh-pico の `z_malloc` / `z_realloc` / `z_free` と
mruby の `mrb_basic_alloc_func` に生きている量を数える計装を入れた (コミットしていない、写しごと消す)。CRuby の zenoh-c を router
モードで開き、端末と同じくクライアントでつないだ。各段の後に 0.6 秒 poll して `GC.start` してから差を取った。網にはルータと
この 2 本だけ。5 回まわして 2 回目以降は同じ数で、全部を閉じると zenoh-pico 側は 0 に戻る (回をまたいで増えない)。

### 数字 (1 本あたり)

| 何を | zenoh-pico (z_malloc。ESP-IDF では PSRAM) | VM のプール (mrb_malloc) |
|---|---|---|
| 素の Zenoh セッション (クライアント、つないだだけ) | 5,497 B (33 塊) | 228 B |
| `Asterism.connect` 1 本 (上を含む: アプリの印、呼び出しの queryable (深さ 32)、ノードの `@profile` の queryable (深さ 4)、生存の印の購読 (深さ 64)、重複の確認の get) | 8,352 B (95 塊) | 2,752 B |
| + 状態を置く (アプリの `@profile` の queryable、深さ 4、と `@status` の put) | +634 B | +456 B |
| + トピック 1 つ (publisher と `@pubs` の put) | +688 B | +695 B |
| 2 本目 (`host/spectate`、同じノード) | 8,688 B (113 塊) | 3,030 B |
| 2 本目を閉じる | -9,288 B (状態の分を含め全部戻る) | -2,466 B |

- VM のプールの中身は、gem の構造体 (i386 で `zrb_session` 36 B、`zrb_qable` 64 B + 深さ × 8 B、`zrb_sub` 64 B + 深さ × 20 B
  (`zrb_entry`)。生存の印の購読は深さ 64 なので 1,280 B が輪) と、接続の Ruby のオブジェクト (Hash・Array・文字列) 。
- 2 本目の zenoh-pico 側が 1 本目より 300 B ほど多いのは、生存の印の購読に 1 本目の印 (アプリ・オブジェクト) が届いて輪に入る分。
  網の上の印の数に比例して増える (1 つ 20 B + キーの長さ、z_malloc)。

### 数字にない分 (測れない、既存の測定と設定から)

- **TLS**: 1 本ごとに mbedTLS の record の buffer が約 21 KB (report/s3_next の測定。`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` 16384 +
  `OUT_CONTENT_LEN` 4096 + 状態)。置き場所は機種で違う: P4 (Tab5・NARYAv4)・AtomS3R・StickS3 は `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`
  で PSRAM、NARYAv3 (n16r8) は内蔵の heap (ただし NARYAv3 は TLS 1.3 が無く tls/ のルータにはつながらない見込み)。
  ホストの host_test は TLS を使わないので測っていない。
- **ソケット**: 1 本ごとに TCP のソケット 1 つ (lwIP、`CONFIG_LWIP_MAX_SOCKETS=10` の 1 つ) と、その PCB と送受信の buffer (内蔵 RAM)。
- **ルータの席**: 1 本ごとにルータへの transport が 1 つ。zenohd の受け入れの上限、証明書ごとの ACL はそのまま 1 本ずつに効く。
  **ブラウザ版で `wss://` の入口が使い捨ての合言葉で 1 回だけ通す形なら、2 本目は通らない** (README「Why the same」にある)。
- 呼び出しを受ける queryable (深さ 32) は 1 本ごとに 1 つ (アプリの数だけ、今と同じ)。ノードの `@profile` の queryable も 1 本ごと
  (3 章の退けた案)。

まとめ: 端末で 2 本目を開くと、PSRAM に約 9 KB (zenoh-pico) + TLS なら約 21 KB、VM のプールに約 3 KB、内蔵 RAM にソケット 1 つ分、
ルータに席 1 つを足す。README と protocol.md 1.1 に「普通は 1 本、2 本目は 2 つ目のアプリのときだけ」と書いた。

## 5. 部品 (picoruby-asterism-zenoh) に全体の状態があるか

- `src/zenoh.c`・`src/zp_*.c`・`ports/` の file scope の変数を見た: `static const` の `mrb_data_type` と表 (flash) だけで、書き換わる
  全体の状態は無い。セッションごとの状態は `zrb_session` (宣言の鎖、namespace) に閉じている。TLS の文脈も `zp_tls_context_new` で
  セッションごとに作る。
- 実際に 1 つの mruby プロセスで 3 本 (同じノード 2 本 + 別のノード 1 本) を開いて、呼び合い・購読・閉じる順を試験で通した。
- 部品は触っていない。上の「同じプロセスで peer にできない」は zenoh-pico の単一スレッドの作り (`Z_FEATURE_MULTI_THREAD=0`) の
  性質で、不具合ではない。

## 6. 試験

すべて `env -u ASTERISM_CREDENTIALS` で流した。

| 何を | 結果 |
|---|---|
| `rake test` (CRuby、全部) | **148 runs、2678 assertions、0 failures** (前は 145 runs・2594)、msgs 2775 checks、適合 `cruby: 241 passed, 0 failed` |
| うち新設 `test/test_connections.rb` | **2 runs、65 assertions**: 同じノードの 2 本 + 別のノード 1 本 (プロセス内のルータ)。互いに見える・呼び合う (入れ子を含む、待っている接続へ呼び返す)・呼んだ相手はノード・状態とトピックと型は別々・値が届く (自分には戻らない)・ノードの自己紹介が 1 つ・プログラム内と網の重複を断る・1 本閉じても残り 2 本が動く・同じ名前で新しい接続 (自己紹介は残り状態は無い)・`poll_all`。受信のスレッドを 2 本の接続でそれぞれ動かして呼び合う。4 回流して揺れなし。zenohd 1.10.1 (docker) をルータにしても通った |
| `rake test:protocol` / `test:protocol_mruby` | 各 **241 passed、0 failed** (+1 informational、前からの「method name as bin」) |
| `rake test:mruby` | **55 + 15 + 25 checks、0 failed** (前は 54 + 15 + 26。profile に「接続のノードはプログラムの既定から始まる」を足し、session から「接続の前の `session`」を外した: 接続の前という状態が無くなったため) |
| `rake test:cross` | **1 run、24 assertions、0 failures** と、新設 `host_test/test_connections.rb` (mruby、ルータは `test/router.rb`) **25 checks、0 failed** |
| `rake test:reconnect` (docker) | CRuby・mruby とも `reconnection ok` |
| `rake test:credentials` (docker) | `credentials: ok` |
| ルータあり (`ASTERISM_TEST_ROUTER`、zenohd 1.10.1) | test_connections 2/65、test_asterism 6/134、test_status 4/60、test_topic 7/142、すべて 0 failures |

- 既存の試験の書き換えで見えた違い: 状態・型・自己紹介を「接続の前に置く」書き方をしていた子プロセスの台本 (test_status・
  test_topic・test_cli・host_test/node.rb・適合の node.rb) は接続の後に置くよう直した。適合の node.rb は、接続の時の自己紹介の put を
  1 回に保つために `Asterism.profile_defaults` を接続の前に呼ぶ (put の数を数える見本があるため)。

## 7. CLI・MCP・Transfer・ROS

- CLI: 開いた接続 (または渡された接続) を `Survey` とコマンドに渡す。自己紹介の名前 (`asterism CLI`/`asterism MCP`) は接続の後に
  `net.profile` で置く (CLI を同じプロセスで動かす試験に、プログラムの既定を漏らさないため)。`net.session.subscribe {}` は `net.zenoh.subscribe {}` に。
- MCP: 同じ。
- `Asterism::Transfer::Endpoint` は消さずに残した (asterism-console が使っている。素の Zenoh の接続の上で受け口を立てる)。
  説明に「2 本目の Asterism の接続で `expose` すれば同じことができる」と書いた。
- `Asterism::ROS` は接続の状態に触れていない (セッションを受け取るだけ) ので変更なし。CRuby の `ROS::Connection.new(net.zenoh)`。

## 8. 見立てと違った点

- 指示書は「部品のセッションは既にオブジェクト」で、全体の状態も無かったが、**同じプロセスの zenoh-pico のセッションどうしは peer で
  つながらない** (2 章)。端末の運用 (ルータのクライアント) には影響しない。mruby の試験にルータが要るので test:cross に入れた
  (test:mruby は asterism-zenoh を要らないままにした)。
- 待っている呼び出しが自分の接続しか回さないと、同じプログラムの 2 本が呼び合えない (時間切れ)。待ちの中でほかの接続にも答える
  ようにした (`pump_others`)。指示書にない仕組みだが、「互いに呼べる」の前提として入れた。
- 状態を接続の前に置けなくなったため、状態の put と `@profile` の queryable の宣言は接続の直後になる。直後に `status_of` を
  別のプロセスから聞くと、宣言が届く前で nil のことがある (smoke で見た。試験は待ってから聞いている)。前も「接続の時の put」と
  同じ順なので、網の上の順序は変わっていない。
- `Asterism.connect` が CRuby で全体のロックを持ったまま重複の確認 (最長 1.5 秒) をするので、その間ほかの接続の受信のスレッドが
  止まる (前も connect はロックの下)。

## 9. 止まらずに決めたこと (確認してほしい点)

1. **`Asterism.profile_defaults`** (新しいモジュールの関数)。ファームが接続の前に端末の自己紹介を置く場所として要るので足した。
   名前と「ノードの項目が勝つ、`nil` で既定を隠す」の重なりは案。
2. **CRuby の `net.zenoh`** (前の `net.session` の `Zenoh::Connection`)。`net.session` を両方で素の `Session` にそろえたので、
   CRuby だけの包みに別の名前が要った。`ROS::Connection#zenoh` と同じ語にした。
3. **`Transfer::Sender.new(..., net:)`** を必須にした (呼び出しを出す接続)。端末の転送のアプリ (fmruby-core) は P3c で直す。
4. **`Asterism.connections` / `Asterism.poll_all` / `net.path`**。`poll_all` は指示書の例どおり。
5. 状態・型を接続の前に置けない、つなぎ直しで状態・型を置き直す (1 章)。指示書の「全部接続が持つ」の帰結として決めた。
   接続を開き直して状態を引き継ぐ手段 (`net.reconnect` のような) は足していない。要るなら P3c で決める。

## 10. P2b-2 への引き継ぎ

- asterism-games・console は `Asterism.publish`・`Asterism.status`・`Asterism.net` などを使っている。全部 `net.` に。
  `Asterism.connect { |net| }` の `net` は今は `Asterism::Connection` (メソッドは前の `Net` とほぼ同じ。`net.session` が
  `Zenoh::Connection` だった所は `net.zenoh`)。
- 観戦は 2 本目 `Asterism.connect(router, node: <ホストのノード>, app: "spectate")` を開き、`spectate.publish("<table>/state",
  type: ...)` で宣言する。型は `spectate.define_type` で同じものを置く (型は接続ごと)。値の送り手は `game` から `spectate` に。
- console の `Transfer::Endpoint` は残してあるので、そのままでも動く。2 本目の接続に移すかは P2b-2 の判断。

## 11. P3c (fmruby-core の端末のアプリに接続をどう渡すか) への材料

- **今の fmruby-core の呼び方** (`lib/add/picoruby-fmrb-asterism/mrblib/`): `fmrb-asterism.rb` が読み込み時に `Asterism.profile(kind:
  :family_mruby, host: ...)` と名前を置き、`link.rb` が `::Asterism.connect(...)` (3 通り) をし、`table_client.rb` が
  `::Asterism.session.subscribe(...)` をする。シェルのコマンド (`shell_commands.rb`) が `::Asterism.profile` を読む。
  これらは全部動かなくなる (モジュールの関数が無い)。
- **自己紹介**: 起動時の `Asterism.profile(kind:, host:, name:)` は `Asterism.profile_defaults(...)` に置き換えるのが素直
  (接続より前、ノードの名前を知る前に置ける)。名前の設定の変更も `profile_defaults(name:)` で、開いている接続があれば put される。
  シェルが読む自分の自己紹介は `Asterism.profile_defaults` (接続していないとき) か `net.profile`。
- **接続の渡し方の候補** (決めるのは P3c):
  (a) `link.rb` が開いた接続をアプリに渡す (`FmrbAsterism.link.net` のような入口)。端末のアプリは 1 VM 1 アプリなので、
  VM に 1 本を基本にし、アプリはそれを受け取る。
  (b) アプリが自分で `Asterism.connect` する (今の `link.rb` の役目をアプリに戻す)。複数の接続ができるので、同じ VM で 2 つの
  アプリ名を名乗ることもできるが、1 本ごとに 4 章の資源 (TLS なら PSRAM 約 21 KB) を使う。
  どちらでも、アプリのコードは `net.` を通すことになるので、アプリの台本 (`.app.rb`) が `Asterism.` を直接書いている所を数えて
  直す必要がある。
- **つなぎ直し**: 前は「close して connect し直しても状態・型は残る」だったが、今は新しい接続で置き直す。`link.rb` がつなぎ直しを
  するなら、置き直しの面倒 (状態・型・トピック・expose) をアプリに戻すか、`link.rb` がアプリの登録を持って置き直すかを決める。
  ノードの自己紹介は残る。
- **転送** (`/usr/share/asterism/transfer/transfer.rb`): `Sender.new(..., net: 接続)` が要る。受け手の `expose` も `net.expose`。
- **待ちの入れ子**: 端末の 16 KB のスタックで、待ちが別の接続の呼び出しにも答えるようになった。同じ VM に 2 本開くと、入れ子は
  合わせて 4 まで。1 本なら今と同じ。
- **ブラウザ版**: 使い捨ての合言葉の入口なら 2 本目は通らない (4 章)。ブラウザ版は 1 本に限るのが安全。

## 12. 残件

- 端末 (esp32・wasm) のビルドは通していない。変えたのは mrblib (Ruby) だけで、内蔵 RAM の静的な増分は無い見込み (VM が読む mrblib が
  少し増える)。fmruby-core は今の版の asterism ではアプリが動かなくなるので、P3c で一斉に入れ替える。
- `docs/api_review.md` の T4 (「モジュールの状態、1 プロセス 1 接続、1.0 の後に」) は古い記録として触っていない。
- 版・CHANGELOG は上げていない。
