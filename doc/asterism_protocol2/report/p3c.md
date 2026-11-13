# P3c report: fmruby-core を protocol 2 に入れ替えた

> 状態: 済 (P4 への引き継ぎあり) | 更新: 2026-11-13 | fmruby-core (ファーム・ネイティブ版・sim・ブラウザ版・回帰の道具) を、手元の asterism (35eafab) と picoruby-asterism-zenoh (b0717f8) の接続のオブジェクトで動くようにした。`FmrbAsterism::Link` が接続を持ち `link.conn` で渡す。値を流すアプリはトピックにし型を付けた。卓のクライアントは、送り手の最初の値・欠け・新しい回・leave の後の最初の値で今の値を呼び出しで取り直す。回帰は全部通り、内蔵 RAM の静的な増分は S3 −56 B・P4 −360 B。端末の 1 値は約 29 B、受け取れる `@profile` の答えは約 3.9 KB まで (zenoh-pico の `Z_FRAG_MAX_SIZE` 4096)。通しはネイティブ版 2 つ・sim・ブラウザ版・CRuby の間で通った。コミットは ac43faf5・1b3832ed・8620c1af。ピンは変えていない

## 1. 入れ替えた所

| 所 | 変えたこと |
|---|---|
| `lib/add/picoruby-fmrb-asterism/mrblib/link.rb` | `Asterism.connect` が返す接続を `@conn` に持ち、`link.conn` で渡す (つなぐ前と、失った後・閉じた後は nil)。poll・close・`connected?`・`reconnecting?`・`lost_reason` は接続に聞く |
| `fmrb-asterism.rb` | 起動時の自己紹介 (kind・host・保存した名前) を `Asterism.profile_defaults` に置く。`FmrbAsterism.save_name` も既定を変える |
| `table_client.rb` | 状態は `conn.subscribe("*/games/*/state") { \|v, info\| }` で受け、`watch_states` まで溜める (poll → `table_watch` → 状態 の順を保つ)。`tc.conn`・`tc.proxy` は接続から。取り直しの決まり (3 章) |
| Shell (`shell_commands.rb`) | `anodes` が `link.conn` の `nodes`・`apps`・`each`・`profile`・`profile_of`・`status_of` を読む |
| 転送 (`/usr/share/transfer/transfer.app.rb`)・`receiving.app.rb`・`xfer_probe` | `link.conn.expose`、`Sender.new(..., net:)`、`conn.nodes`・`conn.profile_async`・`conn[path]` |
| `board`・`othello`・`mahjong` (卓) | 名前は `Asterism.profile_defaults` から。麻雀の記録と統計の呼び出しは `@tc.proxy(@rp)` (使わなくなった `CALL_TIMEOUT` を消した)。お絵かきの線は購読のブロックで受け、欠けか新しい回なら絵を読み直す |
| `walk_scroller` | 歩数とセンサの生の値は購読のブロック、進み具合はトピック `scroller/progress` (型 `family-mruby/walk.progress`)、チューニングの呼び出しは `link.conn.call_async` |
| `step_sensor` | `pedometer/step` (型 `family-mruby/step.state`) と `pedometer/accel` (型 `family-mruby/step.accel`) のトピック。キーは前と同じ |
| `flash/app/test/` の自分でつなぐアプリ (`asterism_demo`・`asterism_rover`・`othello`・`rover_brain`・`rover_pilot`・`rt_probe`) | 返った接続を持って使う。rover_brain の記録は組み込みの log 型のトピック (`time` は 1970 年からの ms)。rt_probe の `rt/pos`・`rt/stats`、test の othello の `othello/state` はトピック (型なし) |
| CRuby の道具 | `tool/imu/step_send.rb`・`step_watch.rb` は接続の publish・subscribe、`tools/fmrb_send.rb` は開いた接続を `send_file` と `Sender` に渡す。`tools/mahjong_overflow/fixture_host.rb` はトピック |
| 回帰の道具 | `fake_asterism.rb` (偽の接続、購読のブロックは poll で、トピックの put は `Net.published` に)、`transfer_quick.rb`・`checkpoint_asterism.rb`・`counterparts.rb` は接続を持つ。CRuby の相手 (`step_send`・counterparts) は `ASTERISM_DIR` があればそこから asterism を読む |
| ホストの試験 | `test/asterism/link_run.rb`・`table_run.rb` の偽物が接続を返す。取り直しの確認を足した |

触らなかったもの:

- ros2_*・zenoh_*・rover_cam の試しのアプリは素の Zenoh のセッション (`Asterism::Zenoh::Session.open`) と `Asterism::ROS::Node` だけを使い、変わっていない。
- 直下の `tools/` (`fmrb_zenoh.rb` の call/meta) と MCP は呼び出しの形だけで、呼び出しは protocol 2 で変わっていない。MCP に Asterism を読むものは無かった。
- `flash/usr/share/asterism/transfer/transfer.rb` は asterism の data/transfer の写し (gitignore) で、`rake setup` が新しいものに置き換える。
- doc/ruby_asterism/ の古い指示書・game.md・browser.md などの `Asterism.expose` の例は記録として残した。

## 2. Link の形

```ruby
def on_create
  @link = FmrbAsterism::Link.new(self, "step", tag: "step_sensor")
end

def link_up(link)                  # つながるたび (新しい接続)
  c = link.conn
  c.expose("pedometer", Pedometer.new(self), methods: { state: 0 })
  c.define_type("family-mruby/step.state", FIELDS)
  @steps = c.publish("pedometer/step", type: "family-mruby/step.state")
  c.subscribe("*/walk/scroller/progress") { |v, info| ... }   # ブロックは link.update (poll) の中から
end

def on_update
  @link.update
end
```

- つなぎ直しは毎回新しい接続なので、置いたもの (公開・トピック・型・購読・on_join) は `link_up` で置き直す。トピックの持ち物 (`@steps`) は `link_down` で捨ててよい (古い接続のトピックに put しても届かない)。
- 自分でつなぐ試しのアプリは `@net = Asterism.connect(...)` を持ち、`@net.poll`・`@net.close`。
- 購読のブロックは poll の中で呼ばれる。描画の順が大事なもの (卓の状態・お絵かきの線・rt_probe) はブロックで溜めて、前と同じ所で読む。そうでないもの (walk_scroller の歩数・センサの値) はブロックの中で処理する。

## 3. 卓のクライアントの取り直し (決定 31、p3a の 12 章と同じ決まり)

卓の状態の値 (`<host>/games/<卓>/state`) について、座っている卓 (`@token` がある) なら、次のどれかで `view` をすぐ呼ぶ (版が同じでも):

- この接続で、その卓の値の最初のもの (送り手の最初の値)
- `info.lost > 0`
- `info.restarted?`
- ホストのノードが網を去った後の、その卓の最初の値 (`on_leave` で記憶を捨てる)

座っていない卓は一覧の状態を使うだけで呼ばない。ほかの卓の欠けでは呼ばない。`table_run.rb` に 6 つの確認を足した (欠け・新しい回・ほかの卓・leave の後・座ってからの最初の値・欠けの無い同じ版)。お絵かき (board) は線が差分なので、線の欠け・新しい回で絵を全部読み直す (`start_picture`)。

## 4. 確かめた数字

### 4.1 回帰の道具

すべて `env -u ASTERISM_CREDENTIALS`、`ASTERISM_DIR`・`PICORUBY_ASTERISM_ZENOH_DIR` を手元に向けて流した。

| 何を | 結果 |
|---|---|
| `rake regress` | host 26 suites 全部通過 (36.0 s)、229 runs (81 scenes on 4 profiles) 全部通過 |
| `rake regress:size` (NARYAv3・TAB5) | ok。2 章の表 |
| `rake regress:live` | modern 16/16 (1 回目は CRuby の相手 `step_send.rb` が入れてある asterism 0.11.0 の gem を読んで落ちた。`ASTERISM_DIR` を読むよう直して 16/16。5 章) |
| `rake regress:transfer:quick` | ok (native、0.7 min)。1 回目はネイティブ版が古いビルドのまま (Link に `conn` が無い) で落ちた。checkpoint がネイティブ版を作り直した後は通った |
| `rake regress:checkpoint` | ok (standard・mruby・web、asterism move・given up・late を含む、7.4 min) |
| `rake regress:screens ONLY=web` | 6 passed / 0 failed |

基準の更新が要る変化は無かった。

### 4.2 内蔵 RAM (regress:size、基準 4e7e7e61)

| 端末 | DIRAM | IRAM | ほかの内蔵 | ファーム |
|---|---|---|---|---|
| NARYAv3 | 125,403 (−56) | 16,384 (+0) | 104 (+0) | 5,425,984 (+212,112) |
| TAB5 | 129,338 (−360) | - | 948 (+0) | 7,445,440 (+756,512) |

- 基準は 277 コミット前のもので、ファームの増分はその間の全部 (漢字の辞書の読みなど) を含む。この段の分は部品ごとに見た: Asterism と zenoh-pico の C と mrblib が入る `libmruby.a` は TAB5 で内蔵 −232・NARYAv3 で内蔵 +0。この段の変更は Ruby (mrblib と flash/ のアプリ) だけで、mrblib は flash の上のバイトコードになる。
- 内蔵 RAM の静的な増分は 0 以下で、止まる条件には当たらない。同じコミットの前後での比較 (ピンの asterism で同じ木を作る) はしていない。

### 4.3 端末の 1 値あたりの大きさ

p2・p2d と同じ測り方 (送り手と受け手の間に TCP の中継を挟み、送り手 → ルータのバイト数を数える、Zenoh の枠を含む)。送り手はネイティブ版の試しのアプリ (scratchpad、コミットしていない) で、`conn.publish("probe/v", type: :vector3)` に `[0.5, -1, 3]` を 2 ms おきに put した。受け手は sim の zenohd につないだ CRuby。

| 個数 | 線の上 | 1 値あたり |
|---|---|---|
| 500 | 14,505 B | **29.0 B** |
| 1,000 | 29,505 B | 29.5 B (500〜999 番は 30.0 B: 通し番号が 2 バイトの整数になる) |

- 見積もり (約 28 B) と合う。回の識別子は乱数なので 1〜3 バイトの幅がある。
- **購読者がいないと、zenoh-pico の publisher は put を線に出さない** (中継で 0 バイト)。最初の測りで受け手の CRuby を止めていたときに見た。端末の送り手には得 (誰も聞いていない値で WiFi を使わない)。CRuby の受け手は 491 個を受け、欠け 9 を数えた (受け手の購読の深さ 16 で落ちた分。欠けは正しく数えられた)。

### 4.4 受け取れる宣言の大きさ (review 3 章の 3)

CRuby のホスト (アプリ `bighost/games`) にトピックを N 個 (説明 100 バイト) 宣言させ、ネイティブ版とブラウザ版の試しのアプリから `pubs_of`・`types_of`・`@profile?pubs;types` を聞いた。

| `@profile` の答えの payload | ネイティブ版 | ブラウザ版 |
|---|---|---|
| 3,689 B (30 個) | 受け取れた | - |
| 3,935 B (32 個) | 受け取れた | 受け取れた |
| 4,052 B (33 個) | **nil** (答えなしと同じ、約 40 ms で) | **nil** |
| 4,436 B (20 個 + 型 5) | nil | - |

- 上限は約 3.9〜4.0 KB。zenoh-pico の `Z_FRAG_MAX_SIZE` 4096 (picoruby-asterism-zenoh の `zenoh_generic_config.h`、fmruby-core は上書きしていない) から、答えのキーと枠の分を引いたもの。超えた答えは分割の組み立てで捨てられ、待たずに「答えなし」(nil) になる。アプリが居ないのと見分けが付かない。
- 本物のゲームホスト (卓 4 つ: mahjong×2・othello・board) は `?status` 219 B、`?pubs` 392 B (7 トピック)、`?types` 2,506 B (9 型)、`?pubs;types` 2,897 B。ネイティブ版はどれも受け取れた。型は遊びの種類ごとに増え、卓ごとには増えない。`pubs` は卓ごとに約 55 B (state と told、麻雀は観戦も)。
- 決まりに全体の上限を置くか (たとえば 3.5 KB) は design.md の「まだ決めていないこと」のとおりユーザが決める。材料: 今のホストは余裕があるが、型の説明 (`d` 2048 B まで) が 2 つ長ければ超える。`?types` と `?pubs` を別に聞けば、それぞれ 3.9 KB まで受け取れる (`type_of` は `pubs;types` を 1 回で聞くので先に超える)。

### 4.5 zenoh-pico で source info が読めるか

- zenoh-pico 1.10.1 は受け取った値の source info を `sample->source_info` に読み込んでいる (`src/net/sample.c`)。公開の API `z_sample_source_info()` は `Z_FEATURE_UNSTABLE_API` のときだけあり、picoruby-asterism-zenoh の config.h は入れていない (`/* cmakedefine (unused) Z_FEATURE_UNSTABLE_API */`)。
- よって**今の部品のままでは Ruby から読めない**。読むには不安定の API を入れる (ほかの不安定の API も入る) か、部品が中の欄を直に読むかになる。送り手が source info を付けない限り中身は空 (zenoh-c も付けるのは不安定の API)。使わないので部品は変えていない。

### 4.6 NARYAv3 (S3) と TLS 1.3 のルータ (見込み)

- `config/sdkconfig.defaults.n16r8` (NARYAv3 と、板を名指さない S3) には `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3` も `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` も無い (IDF の既定でどちらも無効、`sdkconfig.regress.saved` でも `# CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 is not set`)。naryav4・p4・atoms3r (StickS3 も同じ) にはある。
- zenohd 1.10.1 は相互 TLS のとき TLS 1.3 しか受けない (doc/ruby_asterism/report/g2a.md の 2.4)。**NARYAv3 は tls/ のルータ (console の VPS) にはつながらない見込みで、今のままでは P4 の受け入れに S3 の TLS を入れられない**。家の tcp/ のルータは使える。
- 入れるには `sdkconfig.defaults.n16r8` に TLS 1.3 (flash +35〜50 KB) と、mbedTLS の確保を PSRAM に回す `EXTERNAL_MEM_ALLOC` (入れないと 1 接続あたり record の buffer 約 21 KB と証明書などが内蔵の heap から) が要る。sdkconfig の変更はユーザの承認が要るので変えていない (doc/checkpoint/report/s3_next.md 1.4 と同じ結論)。

### 4.7 乱数 (回の識別子)

`Asterism.new_run_id` は `Kernel#rand` (picoruby-rng)。picoruby-rng は全部の構成の gembox (`lib/add/family_mruby.gembox`) にあり、ポートは sim・ネイティブ版・ブラウザ版が posix (`/dev/urandom`、Emscripten も)、ESP32 は `components/picoruby-esp32/CMakeLists.txt` の esp32 のもの (regress:size の S3・P4 のビルドがリンクした)。通しで見た回の識別子: ネイティブ版 35154・16039、sim 18782、ブラウザ版 21260・64616 (接続ごとに違う)。

## 5. 通し (手元: sim の zenohd に ws/ を足したもの)

sim (Retro、ノード `linux`)、ネイティブ版 2 つ (`p3c-nat-a`・`p3c-nat-b`、自前の run dir と user dir)、ブラウザ版 (`wasm-a10ea0`)、CRuby (ゲームホスト `p3cgames/games`、`step_send.rb`、見張りの台本、CLI)。

| 何を | 結果 |
|---|---|
| CLI `asterism topic list` | `linux/step/pedometer/{step,accel}` (`family-mruby/step.state`・`.accel`)、`p3c-nat-a/game/rt/{pos,stats}`、`p3c-nat-b/walk/scroller/progress` (`family-mruby/walk.progress`)、ゲームホストの卓の状態・線 (asterism-games の型)、`p3cgames/@shared/spectate/mahjong*/state` (node、by p3cgames/games)。`topic info linux/step/pedometer/step` が sim の型の定義 (フィールドの型は名前) を出した |
| ネイティブ版 ↔ ネイティブ版 (rt_probe、25/s) | 往復 30 ms、受け 25/s、欠け 0 |
| CRuby の見張り (`**` を 30 秒) | `p3c-nat-a/game/rt/pos` 747 個、通し番号・回の識別子つき、欠け 0。`linux/step/pedometer/step` 10 個、宣言 `{"t" => "family-mruby/step.state"}`。ゲームホストを kill -9 して 0.3 秒で起こすと、卓の状態の回の識別子が 29498 → 3839 に変わった (読み手は先に leave を受けて記憶を捨てるので `restarted?` は 0、新しい回の値は最初の値として来る。p3a の 12 章と同じ) |
| 共有の置き場 (`*/@shared/**`) | 観戦の値が送り手つきで届いた (同じく回の識別子が変わった) |
| CRuby (`step_send.rb --measure`) → ネイティブ版の walk_scroller → CRuby | 8 歩すべてが画面に出て、進み具合のトピックが戻った。put から画面まで中央 16 ms (3〜29) |
| ブラウザ版のオセロ (卓) | 卓の一覧が状態のトピックから出て、入る・座る・CPU・始めるが通った。ゲームホストを kill -9 して起こした後も座ったまま手を指せ、CPU が返した |
| ブラウザ版の 2 本目の接続 | オセロ (卓) を開いたまま試しのアプリが別の接続でつながった (ws/ の入口は合言葉なし) |

確かめていないもの: 欠け (`lost > 0`) から卓の取り直しが起きるところの通し (単体の試験だけ)、ACL のあるルータ、実機。

## 6. 見立てと違った点

- **ブラウザ版で walk_scroller が起動できなかった** (`Spawn failed (error=-6)`)。`large_memory = 1` のアプリで、ブラウザ版の大きいプールは 1 つ (`FMRB_USER_APP_LARGE_POOL_COUNT`)。ブラウザ版の /home (道具の Chrome のプロファイル) で受け取りが on になっていて、起動時に転送のアプリ (これも `large_memory = 1`) がプールを取っていたと見る (ブラウザ版のログは道具から見えず、確かめてはいない)。この段の変更とは関係が無い見込み。ネイティブ版では同じ walk_scroller が動いた。ブラウザ版の通しはオセロ (卓) に替えた。
- regress:live の CRuby の相手 (`step_send.rb`・counterparts の呼び出し) は `require "asterism"` で入れてある gem (0.11.0、接続のオブジェクトが無い) を読んでいた。games のホストは Gemfile が `ASTERISM_DIR` を読むので手元のものだった。相手も `ASTERISM_DIR` を読むようにした。P4 で gem を公開すれば、`ASTERISM_DIR` なしでも新しい gem を読む。
- regress:transfer:quick はネイティブ版を作らずに今あるものを使う。lib/ を変えた後は、先にネイティブ版を作り直す (checkpoint か `rake native:core`) 必要がある。
- 購読者のいないトピックの put は zenoh-pico が線に出さない (4.3)。

## 7. 撤回した仮説

- 「1 値あたりの測りが 0 バイトなのは中継が壊れた」: 中継は動いていて、受け手がいなかったので送り手が put を出していなかった。
- 「宣言の大きさの上限は時間切れで現れる」: 超えた答えは待たずに約 40 ms で「答えなし」になる (時間切れ 5 秒を待たない)。

## 8. 止まらずに決めたこと (確認してほしい点)

1. 独自の型の名前空間を `family-mruby/` にした (`family-mruby/step.state`・`step.accel`・`walk.progress`)。フィールドの型は名前で、nil になりうる欄 (`t_ms`・`ago_ms`) は `any`。
2. rt_probe・test の othello・fixture_host・`step_send.rb` のトピックは型なしのまま (試しの道具)。
3. お絵かき (board) は、線の値に欠けか新しい回があれば絵を全部読み直す (`start_picture`)。今までは版の比較だけで、線の欠けは読み直さなかった。
4. rover_brain の記録の `time` を秒の float から ms の整数にした (決定 30、組み込みの log 型)。
5. Link に `conn` を足しただけで、ほかの入口は今のまま (`link.update`・`up?`・`connected?`・`link_up`/`link_down`)。

## 9. P4 への注意

- **ピン**: `lib/add/ASTERISM_PIN` と `PICORUBY_ASTERISM_ZENOH_PIN` は変えていない。P4 で asterism と picoruby-asterism-zenoh を push した後、2 つのピンを上げ、`ASTERISM_DIR` なしで `rake clean_all && rake build:linux`・`rake regress`・`rake regress:live` を流す。今のピン (c221b08・15b0ff4) のままでは、このコミットのアプリは動かない (`link.conn` が無い接続、モジュールの関数が無い)。fmruby-core の develop を push するのは、ピンを上げるのと同じときにする。
- **CRuby の道具の gem**: `step_send.rb`・counterparts・`fmrb_send.rb` (vendor のピン) は、`ASTERISM_DIR` が無ければ入れてある gem かピンの asterism を読む。asterism の gem を公開して入れ替えるまで、regress:live の steps は手元の `ASTERISM_DIR` が要る。
- **ブラウザ版の配置**: ブラウザ版はこのコミットと新しい asterism で作る (`rake wasm:web`)。公開のブラウザ版と VPS の console・games を同時に上げないと、protocol の違いで卓が見えない。
- **宣言の大きさ**: 4.4 の上限 (約 3.9 KB) を決まりに置くかはユーザが決める。games の型の説明が長くなると `type_of` (pubs;types) から先に超える。
- **NARYAv3 の TLS**: 4.6。S3 を tls/ のルータで受け入れに入れるなら、sdkconfig の承認が先。
- **ブラウザ版の大きいプール**: 6 章。受け取りが on のブラウザ版では `large_memory` のアプリ (walk_scroller・麻雀) が起動できない見込み。P3c の範囲外として確かめていない。
- **確かめていない所**: 実機 (S3・P4) で protocol 2 の値が流れること、ACL のあるルータ、欠けからの取り直しの通し。
