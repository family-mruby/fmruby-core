# P2e report: 版を生存の印と呼び出しに載せた、分割の上限を 16 KB に

> 状態: 済 (P4 への引き継ぎあり) | 更新: 2026-11-13 | 決定 35: アプリの生存の印を `asterism/<node>/<app>/protocol/2` にし、呼び出しの頼みに `"protocol" => 2` を足した。版の違う呼び出しは種類 `protocol` のエラーで断り、値は流れたまま。どの接続も版の違うアプリを 1 回ずつログに出す (CRuby だけの見張りは消した)。決定 34: zenoh-pico の `Z_FRAG_MAX_SIZE` を 4096 から 16384 にした (静的な RAM は 0、組み立ての buffer は分割された値が来たときだけ、端末では PSRAM)。承認済みの sdkconfig (S3 の画面つきに TLS 1.3 と mbedTLS の PSRAM) も入れた。内蔵 RAM の静的な増分はどれも 0。ネイティブ版とブラウザ版が 12 KB・16 KB の答えを受け取った。console の図に版の違うアプリの印、ロールの migration。回帰は全部通過

## 1. 決めた形とその理由

### 1.1 生存の印: `asterism/<node>/<app>/protocol/<n>`

アプリの生存の印のキーを 2 段延ばし、4 つ目の区切りを版の番号 (10 進) にした。protocol 1 の `asterism/<node>/<app>` は「protocol 1」として読む。

| 案 | 見え方 | 退けた理由 / 採った理由 |
|---|---|---|
| `asterism/<node>/<app>/@2` (verbatim) | `asterism/**` に掛からない | 読み手の見張り (`asterism/**`)、重複の確認 (`asterism/<node>/<app>/**`)、ロールの `<app>/**` の行のどれにも掛からなくなり、全部に別の行が要る。退けた |
| 印を 2 つ (今の印 + 版の印) | 今の印はそのまま | 端末の印が 1 つ増え、版の印は結局同じ問題を持つ。退けた |
| `asterism/<node>/<app>/<n>` (3 段) | オブジェクトの印と同じ段 | `2` という名前のオブジェクトと区別できない (名前の決まりは数字だけの名前を禁じていない)。退けた |
| **`asterism/<node>/<app>/protocol/<n>`** (4 段) | 普通の区切りなので `asterism/**` と `<app>/**` に掛かる | オブジェクトの印は必ず 3 段なので出会わない (`protocol` という名前のオブジェクトがあっても 3 段)。見張り・重複の確認・ロールの `**` の行がそのまま働く。**採った** |

- 読み手 (`follow_liveliness`) は `asterism/` の後ろを区切って、3 段はオブジェクト、4 段で 3 つ目が `protocol`・4 つ目が数字 (符号なし、先頭の 0 なし、9 桁まで、Regexp を使わない) ならその版のアプリ、2 段は protocol 1 のアプリ、ほかは無視する。
- 版の違うアプリもアプリとして扱う (`apps`・`on_app_join`・値・leave の掃除)。断らない。
- 次の版も同じ形 (`.../protocol/3`) で名乗る。版 2 の読み手は版 3 のアプリを「版 3 のアプリ」として見る。
- ACL: 普通の区切りなので、`asterism/${node}/<app>/**` の書き手の行で印を出せ、`asterism/*/<app>/**` の読み手の行で見える。アプリのキーをちょうどで書いた行 (`asterism/*/game`、games-host の Read) だけは届かなくなるので、console の migration で `<行>/protocol/*` を足した (3 章)。

### 1.2 呼び出し: 頼みの map に `"protocol" => 2`

```
{"method" => "status", "protocol" => 2}      82 a6 6d6574686f64 a6 737461747573 a8 70726f746f636f6c 02
```

- 1 回の呼び出しで 10 バイト増える。attachment にしなかったのは、(1) どの版の受け手も頼みと一緒に読める、(2) 部品 (zenoh-pico の get) に attachment の Ruby の API が要らない、(3) 断りの答えが普通の `{"error" => {...}}` で、protocol 1 の呼び手も `RemoteError` (`remote_kind` `"protocol"`) として読める、ため。
- 受け手は頼みが 1 つの値の map なら、**形を調べる前に** `protocol` を見る。整数の 2 でなければ (無い・別の数・文字列・浮動小数)、種類 `protocol`・class `Asterism::Error`・message `<node>/<app> speaks Asterism protocol 2, the call says protocol 1` (無いときは `says no protocol (protocol 1 or older)`) で答え、メソッドは走らせない。別の版の頼みは形が違いうるので先に見る。map でない値は今どおり `malformed`。
- 呼び手の側は他の種類と同じ `RemoteError` (今の仕組みのまま)。版 2 の呼び手が protocol 1 の受け手を呼ぶと、受け手は知らない欄を無視して答える (今の protocol 1 の振る舞い)。
- 同じアプリの中の呼び出し (網に出ない) も同じ頼みを作るので通る。
- protocol.md 4.4 の表の `protocol` の行に「頼みの `protocol` が 2 でない (4.3 の 3)」を足した。

### 1.3 版の違いを知る・伝える

- `net.protocol_of("node/app")`: 印の番号 (居なければ nil)。`net.protocol_of(node)`: そのノードのアプリの印の番号 (違えば一番小さいもの)、アプリが 1 つも見えないノードだけ今どおり自己紹介に聞く (無ければ 0)。
- 警告: 印が来たとき、版が違えばプログラムで 1 回だけ `asterism: <node>/<app> speaks Asterism protocol 1, this program 2: calls between them are refused (error "protocol"), its values still flow but may be read wrong (docs/protocol.md 6.1)`。CRuby は警告の出力、端末は `puts` (ログ)。購読を足さないので端末でも既定で出る。
- 消したもの: `@profile` の put の購読 (`@profile_watch`)、`Asterism.watch_protocols=`、`follow_profiles`、`protocol_seen`。どれも未公開 (P2d で足し、gem は出していない)。
- CLI: `asterism watch` が版の違うアプリを `app (protocol N)` と出し、`survey` の木 (MCP の `asterism_nodes`) が版の違うアプリに `"protocol"` を付ける。

## 2. 分割の上限 (決定 34)

### 2.1 どこで決まっているか

- picoruby-asterism-zenoh の `include/zenoh_generic_config.h` の `#ifndef Z_FRAG_MAX_SIZE` (zenoh-pico の既定と同じ 4096 だった)。
- fmruby-core の構成 (esp32 の S3・P4、linux の sim、native、wasm) はどれも上書きしていない (`lib/add/family_mruby_*.rb`・rakelib・CMake を見た)。`rake setup` が gem の `include/` を picoruby の木に写すので、gem の 1 箇所で全部が変わる。gem のホストの試験も同じ見出し。

### 2.2 RAM への効き方 (zenoh-pico 1.10.1 を読んだ)

- 使うのは `src/transport/unicast/rx.c` (と multicast) の組み立てだけ。分割された値の**最初のかけらが来たとき**に `_z_wbuf_make(Z_FRAG_MAX_SIZE, false)` で確保し、最後のかけらで解いた後に捨てる (`_z_zbuf_clear`)。上限を超える値は溢れとして捨て、答えなしになる。
- 1 つの相手 (peer) につき、信頼する流れとしない流れの 2 つまで同時に持ちうる。client の相手はルータ 1 つなので、最大 2 × 16 KB が一時的に。分割されない値 (2 KB の batch に収まるもの) では確保しない。
- 確保は `z_malloc`。ESP-IDF では gem の `ports/esp32/zp_system_esp32.c` が PSRAM から出す (内蔵 RAM に落ちない)。ブラウザ版は emscripten の heap (`ALLOW_MEMORY_GROWTH`、初期 128 MB)。
- 定数は配列の大きさや静的な領域に使われていないので、静的な RAM は変わらない (下の測定で確認)。

### 2.3 値: 16384

- 4 KB の答えが落ちていたので、今の宣言・型の大きさ (本物のゲームホストで `?pubs;types` 2.9 KB) の数倍を受けられる値。PSRAM の一時 16 KB は、端末の TLS 1 本 (約 21 KB) より小さい。
- 32 KB にしない理由: 一時の確保が倍になり、端末の VM のプールにも同じ大きさの String が入る。決まりの全体の上限は決めない (design.md の「まだ決めていないこと」のまま、ユーザが決める)。

### 2.4 測った数字

`regress:size` (基準 4e7e7e61、手元の asterism・picoruby-asterism-zenoh)。分割の上限だけの木 (sdkconfig を外した状態) と、sdkconfig を入れた木を順に測った。

| 端末 | 何を入れた | DIRAM | IRAM | ほかの内蔵 | ファーム | 区画の空き |
|---|---|---|---|---|---|---|
| NARYAv3 | 分割の上限 | 125,403 (−56) | 16,384 (+0) | 104 (+0) | 5,427,072 | 864,384 (14%) |
| TAB5 | 分割の上限 | 129,338 (−360) | - | 948 (+0) | 7,446,528 | 876,544 (11%) |
| NARYAv3 | + sdkconfig (TLS 1.3・PSRAM) | 125,403 (−56) | 16,384 (+0) | 104 (+0) | 5,471,440 | 820,016 (13%) |

- 内蔵 RAM は P3c (p3c.md 4.2) と同じ数字で、分割の上限による変化は 0。ファームは P3c から +1,088 B (両方の端末で同じ。この段の mrblib の版の処理の分)。
- `libmruby.a` の内蔵は TAB5 −232・NARYAv3 +0 (P3c と同じ、基準との差)。
- ATOM_DISPLAY (n8r8) はビルドが通らない (サポート中断、fmruby-core/CLAUDE.md) うえ、regress:size の表に無いので測っていない。

### 2.5 受け取れることの確認

- gem: `rake test_cruby` に、zenoh-c のルータが 12,000 B と 20,000 B を答える試験を足した。12,000 B は届き、20,000 B は落ちて答えなしになる (上限が効いている)。`rake test` 167 checks 0 failed。
  - 注意: gem の mruby のビルドは見出しの変更を依存として追わない。**`rake clean` しないと古い 4096 のまま**だった (1 回目の test_cruby が 12,000 B を受けられなかった)。fmruby-core も同じで、`clean_all`・`native:clean`・wasm の libmruby の作り直し (clean_all が消す) を経て作った。
- 通し: 手元の zenohd (eclipse/zenoh:1.10.1、tcp と ws) に、CRuby のホスト `p2ebig/big` (`blob.get(n)` が n バイト、説明の長いトピック 40 個と型 1 つ: `@profile?pubs` 10,719 B、`?types` 1,539 B、`?pubs;types` 12,257 B) と、protocol 1 の印 `asterism/p2eold/app` を置き、試しのアプリ (scratchpad、コミットしていない) をネイティブ版とブラウザ版で走らせた。

| | ネイティブ版 | ブラウザ版 |
|---|---|---|
| `blob.get` 4,000 / 8,000 / 12,000 / 16,000 B | 全部受け取れた | 全部受け取れた |
| `pubs_of` (10.7 KB の答え) | 40 トピック | 40 トピック |
| `types_of` | 1 型 | 1 型 |
| `protocol_of("p2ebig/big")` / `("p2eold/app")` | 2 / 1 | 2 / 1 |
| 版の違うアプリのログ | 1 行 (`p2eold/app speaks Asterism protocol 1 ...`) | (画面のみ確認) |
| `apps` | 自分・`p2ebig/big`・`p2eold/app` | 同じ |

## 3. console (asterism-console)

- 図 (`Bridge::Graph`): 印の 3 つの形 (版つき・protocol 1・オブジェクト) を読み、アプリに data `"protocol"`、2 でなければ `"other_protocol"`。ルームの中の印も同じ。ページは版の違うアプリを赤い枠と `<app> (protocol N)` の名前で描き、詳しい欄に版と「呼び出しは断られ、値は流れる」を出す。bridge のアプリの一覧 (`asterism_apps`) が新しい印の長さを数える。
- ロール: アプリのキーをちょうどで書いた Read and write・Read の行 (`asterism/` の下の 3 段、`@` で始まる区切りなし) に、同じ区画に `<行>/**` が無ければ `<行>/protocol/*` を足す migration (`20261113500000_relay_role_app_token_protocol`)。足すだけで、もう一度流しても変わらない。games-host の `asterism/*/game` が対象 (ACL の評価器で、前は新しい印が見えず、後は見えることを確かめた)。match・viewer・spectator は変わらない。docs/rooms.md の games-host の例と docs/install.md の更新の説明に書いた。
- `bin/rails test` 665 runs 0 failures、rubocop 違反なし。

## 4. 試験と回帰

| 何を | 結果 |
|---|---|
| asterism `rake` (msgs・transfer・objects・api・cli・protocol) | 全部通過 (cli は `ASTERISM_CREDENTIALS`・`ASTERISM_ROUTER` を外して。環境に残っていると `test_usage` が落ちる、変更前から) |
| asterism 適合 (版 8、CRuby) | 250 passed、0 failed |
| asterism `test:mruby`・`test:cross`・`test:protocol_mruby` (250 passed)・`test:reconnect` | 全部通過 |
| picoruby-asterism-zenoh `rake test`・`test_cruby` | 167 checks 0 failed、ok |
| `rake regress` | host 26 suites、229 runs 全部通過 |
| `rake regress:size` | ok (2.4 の表) |
| `rake regress:live` | modern 16/16 |
| `rake regress:transfer:quick` | ok (native) |
| `rake regress:checkpoint` | ok (standard・mruby・web、asterism move を含む、6.8 min) |
| `rake regress:screens ONLY=web` | 6 passed / 0 failed |

- 適合の見本 (版 8): 印の形、頼みの `protocol` (呼び手が送るものの確認を含む)、断りの 6 例 (無い・1・3・文字列・浮動小数・形より先)。malformed の見本は `protocol` 2 を付けた形に直した。driver の印も版つきに。

## 5. コミット

| リポジトリ | コミット |
|---|---|
| asterism | 63f5129 feat(protocol)、7bc438d docs(protocol) |
| picoruby-asterism-zenoh | 70c94a0 feat(zenoh) |
| asterism-console | daceadc feat(graph)、649b210 feat(relay) |
| family-mruby (直下) | 83785d9 fix(tools) (`tools/fmrb_zenoh.rb` の call に `protocol`) |
| fmruby-core | 8c961c1f build(sdkconfig)、docs(asterism_protocol2) (この report と plan) |

ピン (`lib/add/ASTERISM_PIN`・`PICORUBY_ASTERISM_ZENOH_PIN`) は変えていない。

## 6. sdkconfig (承認済み、2026-11-13)

- `config/sdkconfig.defaults.n16r8` (NARYAv3 と板を名指さない S3) と `n8r8` (ATOM_DISPLAY、中断中) に、AtomS3R と同じ `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y`・`# CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC is not set`・`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` を足した。
- NARYAv3: 内蔵 RAM の変化なし、ファーム +44,368 B (`libmbedtls` +24,901、`libmbedcrypto` +18,561)、区画の空き 820,016 B (13%)。
- n8r8 は測れない (2.4)。**実機はつないでいないので、TLS のルータ (VPS) へつながることは確かめていない**。つないだときに確かめる。

## 7. 見立てと違った点

- gem の mruby のビルドが見出しの変更で作り直されず、最初の test_cruby が 12,000 B を受けられなかった。`rake clean` の後は通った (2.5)。
- 印を版つきにすると、アプリのキーをちょうどで書いたロールの行 (games-host の Read) からは印が見えなくなる。`**` の行は無事。console の migration で足した。
- console の live の試験 (`app_profiles_live_test`) は印のキーを手で聞いていたので、新しい形に直した。games・fmruby-core には手で印や頼みを作る所が無かった (直下の `tools/fmrb_zenoh.rb` の call だけ直した)。

## 8. 止まらずに決めたこと (確認してほしい点)

1. 印の形 `asterism/<node>/<app>/protocol/<n>` と、区切りの語 `protocol` (1.1)。
2. 印の無い (2 段の) アプリを「protocol 1」と読み、console の図でも版の違うアプリとして印を付ける。
3. 頼みの `protocol` の検査を形の検査より先にした (別の版の頼みには `malformed` でなく `protocol` を返す)。
4. `protocol_of(node)` は、版の違うアプリが混ざるノードでは一番小さい版を返す。
5. 分割の上限 16384 (2.3)。決まりの全体の上限は置いていない。
6. CLI の survey の木に、版の違うアプリだけ `"protocol"` を付けた (MCP の `asterism_nodes` の出力に出る)。

## 9. P4 への注意

- **一斉の入れ替え**: protocol 1 の呼び手 (公開中の gem 0.11.0、古いブラウザ版のキャッシュ、古いファーム) が版 2 の受け手を呼ぶと、今は `protocol` のエラーで断られる (review 5 章の 1 の「半分動く混在」は、呼び出しの側では起きなくなった)。逆向き (版 2 → protocol 1 の受け手) は通る。
- **VPS の台帳**: ロールの migration が games-host の行を足す (足すだけ)。games のホストが自分のロールを console の文書と違う形で持っているなら、`asterism/*/game` のような行に `/protocol/*` が要る。asterism-games の文書のロールの例は見ていない (p3b の 11.6 の食い違いと合わせて P4 で見る)。
- **転送の 1 呼び出しの大きさ**: asterism の `data/transfer/transfer.rb` は `CALL_BYTES = 4096` (zenoh-pico の 4 KB に合わせた) のまま。新しい部品どうしなら大きくできるが、古い端末と混ざる間は変えない方が安全。変えるかは P4 の後で決める。
- **ピン**: P4 で asterism・picoruby-asterism-zenoh を push した後にピンを上げ、`clean_all` (見出しの変更を追わないため) で作り直す。
- **残っているコンテナ**: `asterism-restart-402908-cruby` (asterism の `test:reconnect` の restart の確認のルータの名前の形) が動いたまま。この段の試験 (18:57 に終わった) より前の 16:17 に作られたもので、止めていない。
- **n8r8 の sdkconfig**: ATOM_DISPLAY の再開時に区画に収まるかを測る。

## 10. 終わりの状態

- `build/` は手元の asterism・picoruby-asterism-zenoh で `clean_all` から作った Linux (sim、Retro)。`file build/fmruby-core.elf` は x86-64。ネイティブ版 (native/build) とブラウザ版 (wasm/build) も同じ手元のもので作り直した。
- sim・ブラウザ版・ネイティブ版は止めた。試しのアプリとその設定 (ネイティブ版の /home、ブラウザ版の /flash/home) は消した。試しの zenohd と CRuby のホストも止めた。
