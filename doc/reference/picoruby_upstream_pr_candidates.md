# picoruby 上流PR候補メモ (ネットワークAPI検証で発見したバグ)

2026-07-09 作成。Ruby ネットワークAPI (doc/reference/ruby_network_api_design.md) の
Linux/実機(Tab5)検証で発見した picoruby 本体のバグと修正の一覧。
いずれ上流 (https://github.com/picoruby/picoruby) へPRを出すための引き継ぎ資料。

## 2026-07-13 棚卸し (picoruby c932f70b マージ後の状況)

upstream master c932f70b への追従マージ (doc/work_picoruby_merge/) で、各候補の
生死を実コードで確認した。**マージ後の pin は c932f70b0** (以下の旧文中の
c14aa4400 は旧 pin)。mruby 本体 (vm.c / task.c / mruby-task) は hasumikin fork が
本家 **mruby/mruby** に統合されたため、その領域の PR 先は mruby/mruby になる。

| # | 状況 | 備考 |
|---|------|------|
| 1 | **解消 (upstream が根本修正)** | ports API を vm 貫通型に refactor。PR 不要 |
| 2 | 残存 (要最新確認) | マージで我々の GC リーク修正を保持 (da09a4e) |
| 3 | **残存・有力** | upstream read 実装は簡略なまま。我々の版は HTTPS 実測済み |
| 4 | **残存・有力** | 同上 (dechunk 無し) |
| 5 | 解消 (upstream が独自修正) | decimal_divider 方式。PR 不要 |
| 6 | **残存・確認済み・最有力** | c932f70b でも gemdir が picoruby-pack のまま。1 行修正 |
| 7 | 解消 (upstream が実装) | recv で close_notify → return 0 済み |
| 8-9 | 残存 | net-http 系。#3/#4 とまとめて 1 PR が妥当 |
| 10 | **残存・確認済み・有力** | c932f70b でも base_socket 未割当で ready 常時 false |
| 11 | 残存 | freeaddrinfo リーク。tcp_socket 統合時にクリーン残存を確認 |
| 12 | 残存 (低) | ESP32_PLATFORM ガードで回避継続 |
| 13 | 一部改善 | upstream が build.platform?(:esp32) を導入。外部 CMake ビルド連携の論点は残る |
| 14 | ほぼ解消 | upstream が posix ports を nonblock recv 化。残課題は要確認 |

### 新規候補 (マージ作業で発生)

- **N1: mruby-task の FreeRTOS port + 外部 tick 源フック [PR先: mruby/mruby]** —
  価値最大。upstream の port は glib/posix/win のみで、「timer が mrb_tick() を直接
  呼ぶ」前提はマルチスレッド RTOS で task queue を破壊する (実測済み)。我々の
  ports/freertos/task_hal.c (純 FreeRTOS API、IDF 非依存、top/bottom-half 分割 +
  notification idle) と task.c への数行フック (mrb_hal_task_take_pending_ticks を
  task_run_body ループ先頭で適用) をセットで提案する。採用されれば vm/task 系の
  vendored パッチが恒久的に消え、次回マージのコストが激減する。設計根拠は
  doc/work_picoruby_merge/instruct_d7_b1_tick.md の「なぜ安定稼働するか」節。
- **N2: gem compiler が自 mrbgem.rake 実行前に確定する件 [issue先: picoruby]** —
  gem の mrbgem.rake 内の `build.cc.defines <<` は mruby core と依存 gem には効くが
  **自 gem のソースには効かない** (compiler が先に clone される)。picoruby-mruby の
  mrbgem.rake 自体がこのパターンで MRB_NO_BOXING 等を注入しており、
  libmruby 内で mrb_state レイアウトが分裂する実害を確認 (ABI 事故の第 2 原因)。
  修正は設計判断を伴うため、まず issue として報告。

## 前提と引き継ぎ手順

- 本リポジトリが参照する picoruby は **c932f70b0** (2026-07 マージ済み)。
  **PR前に必ず上流HEADで各ファイルの現状を確認**すること
  (既に直っている / 実装が書き換わっている可能性がある)
- こちらの修正の実体は全て `lib/patch/` 以下にある (rake setup がサブモジュールへ
  コピーする方式)。各ファイル先頭の "Family mruby patch" コメントに修正理由を記載済み
- 手順: (1) 上流HEADで該当箇所を確認 → (2) 残存していれば lib/patch との diff から
  fmrb固有部分 (後述) を除いた最小diffを作成 → (3) 再現手順を添えてPR
- 発見の経緯・再現ログは doc/reference/ruby_network_api_design.md の実装状況の節を参照

## 候補一覧 (優先度順)

| # | 対象 | 症状 | 重要度 |
|---|------|------|--------|
| 1 | picoruby-socket ports/esp32/{ssl_socket,tcp_server}.c | `picorb_alloc(NULL,...)` がmruby VMで `mrb_malloc(NULL,...)` になりNULLデリファレンスで即クラッシュ | 高 (クラッシュ) |
| 2 | picoruby-socket src/mruby/ssl_socket.c | GC free が「closed扱い」のソケットを解放せず、EOF/エラー後のTLSセッション(~21KB内部RAM+fd)が永久リーク | 高 (リーク) |
| 3 | picoruby-net-http mrblib/http_client.rb | `get_response`/`post_form` が `http.start` の戻り値(self)を返しレスポンスが取得不能 | 高 (API破損) |
| 4 | picoruby-net-http mrblib/http_client.rb | chunked転送のデコード無し (チャンク枠がボディに混入、`1d5\r\n{...`) | 高 |
| 5 | picoruby-json mrblib/json.rb | `parse_float` が小数点を無視 (`26.2`→`262.0`) | 高 (データ破損)・最小diffで出しやすい |
| 6 | picoruby-net-websocket mrbgem.rake | mruby VM時のpack依存が存在しないgemdir (`picoruby-mruby/lib/mruby/mrbgems/picoruby-pack`) を参照しビルド不能。正しくは `mruby-pack` | 高 (ビルド破損) |
| 7 | picoruby-socket ports/esp32/ssl_socket.c | `MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY` をEOFでなくエラー扱い → `Connection: close` 応答の読み切りが必ず例外 | 高 |
| 8 | picoruby-net-http mrblib/http_client.rb | `finish` が `closed?` ガードでcloseをスキップ (#2と合わせてリークの実経路) | 中 |
| 9 | picoruby-net-http mrblib/http_client.rb | URIオブジェクト非対応 (`get_response(URI.parse(...))` がホスト名扱いで:80へ) | 中 (CRuby互換) |
| 10 | picoruby-socket ports/esp32/ssl_socket.c | `SSLSocket_ready` が常にfalse (未割当の base_socket 依存) | 中 |
| 11 | picoruby-socket ports/esp32/tcp_socket.c | `getaddrinfo` の結果を `freeaddrinfo` せずリーク (接続毎) | 中 |
| 12 | picoruby-socket src/mruby/socket.c | `PICORB_PLATFORM_POSIX` 時に `<sys/socket.h>` を無条件include (ベアメタルツールチェーンに無い) | 低 (ESP-IDFビルド構成に依存) |
| 13 | picoruby-socket / picoruby-mbedtls mrbgem.rake | ESP-IDFビルド判定が `build.name == "esp32"` 固定 (esp32p4等で自前lwIP/mbedtlsをクローンし破綻) | 低 (ビルド名はプロジェクト依存。判定方法は上流と要相談) |
| 14 | picoruby-socket ports/posix/*.c | タイムアウト無し・EINTRリトライ無し | 低 (EINTRはposix一般の堅牢化として提案可) |

## 各候補の詳細

### 1. picorb_alloc(NULL) クラッシュ [ports/esp32/ssl_socket.c, tcp_server.c]

- 症状: 実機(mruby VM)で最初のHTTPS/TCPServer利用時に Load access fault
  (MTVAL=0x84、`mrb_realloc` 内)。`picorb_alloc(mrb,size)` は mruby では
  `mrb_malloc(mrb,size)` マクロのため、NULL vm は mruby/c でのみ合法
- こちらの修正: `fmrb_sys_malloc/free` に置換 (lib/patch/picoruby-socket/ports/esp32/)
- **上流PR時の注意**: fmrb_sys_malloc はfmrb固有。上流版は素の `malloc/free` か、
  ポート層向けのVM非依存アロケータAPIにするのが妥当 (上流方針に合わせる)

### 2. SSLSocket のGCリーク [src/mruby/ssl_socket.c]

- 症状: `SSLSocket_closed()` は `state != CONNECTED` を返すため、ピアEOFや
  エラー後は「closed」に見えるが、mbedTLSセッション(入出力バッファ16KB+4KB)と
  fd は生きている。`mrb_ssl_socket_free` が `if (!closed) close` としているため
  解放されず、**到達不能な恒久リーク** (GCでも回収不能)。lwIPのソケット上限
  (例: 16) に達すると通信自体が不能になる
- こちらの修正: free で状態に関わらず `SSLSocket_close` を呼ぶ
  (close は全状態で安全に全解放する実装。明示close済みは DATA_PTR=NULL で到達しない)
- 再現: `Connection: close` を返すサーバ (api.open-meteo.com等) へ
  `Net::HTTP.get_response` を繰り返し、ヒープ残量を観測

### 3. get_response/post_form がレスポンスを返さない [mrblib/http_client.rb]

- 症状: `res = Net::HTTP.get_response(...)` の戻り値が `Net::HTTP` インスタンス
  (`http.start { ... }` の戻り値はブロック値でなくself)。`res.code` で NoMethodError
- こちらの修正: ブロック内でレスポンスをローカルにキャプチャして返す

### 4. chunked転送未デコード [mrblib/http_client.rb read_response]

- 症状: `Transfer-Encoding: chunked` の応答でチャンクサイズ行(`1d5\r\n`)ごと
  ボディとして返る。JSONパース等が壊れる
- こちらの修正: `dechunk`/`hex_to_i` を追加し、終端(`0\r\n\r\n`)まで読み→枠を除去。
  ヘッダと同時に全ボディ到着済みの場合に余分なreadでブロックしないよう
  終端チェックを先行。Content-Length経路も複数read対応に

### 5. parse_float が小数点を無視 [picoruby-json mrblib/json.rb]

- 症状: `"26.2"` → `262.0`。小数部分岐の条件が `decimal_divider != 1.0` なのに
  `'.'` のcaseが何もしないため、小数部の桁が整数として連結される
- こちらの修正: `in_fraction` フラグを導入 (`'.'` でtrue、桁処理で分岐)。
  ホストRubyで 26.2 / -3.75 / 1.5e2 / 2.5e-3 の正常化を確認済み
- **最小diffで完結し、テストも書きやすいので最初のPRに最適**

### 6. net-websocket の mruby-pack gemdir誤り [mrbgem.rake]

- 症状: mruby VMビルドで
  `Can't find .../picoruby-mruby/lib/mruby/mrbgems/picoruby-pack/mrbgem.rake`。
  正しいパスは `.../mrbgems/mruby-pack`
- こちらの修正: gemdir を mruby-pack へ (lib/patch/picoruby-net-websocket/mrbgem.rake)

### 7. close_notify をエラー扱い [ports/esp32/ssl_socket.c SSLSocket_recv]

- 症状: サーバがTLS close_notify を送る(=`Connection: close` 応答の正常終了)と
  `MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY` が -1 (エラー) として返り、Ruby側で
  "SSL recv failed" 例外。example.com のようにclose_notify無しでFINするサーバでは
  発生しないため気づきにくい
- こちらの修正: EOF (0) を返し state を非CONNECTEDへ

### 8-9. finish のガード / URIオブジェクト対応 [mrblib/http_client.rb]

- 8: `finish` の `@socket.close if !@socket.closed?` は #2 と同じ意味論の罠で
  EOF後のセッションを解放しない。無条件close (解放済みはrescue) に変更
- 9: CRuby 互換の `Net::HTTP.get_response(URI.parse(...))` 形式を受理するよう
  ダックタイピング (`respond_to?(:host)`) で分岐を追加

### 10. SSLSocket_ready が常にfalse [ports/esp32/ssl_socket.c]

- 症状: `ready?` が未割当の `base_socket` に依存し常にfalse。
  net-websocket の `receive(timeout:)` ポーリングが機能しない
- こちらの修正: `mbedtls_ssl_get_bytes_avail` + fdの `select()` (0タイムアウト)。
  ESP-IDFのmbedtlsポートに `mbedtls_net_poll` が無い点に注意

### 11. getaddrinfo リーク [ports/esp32/tcp_socket.c]

- `TCPSocket_connect` が `getaddrinfo` の結果を `freeaddrinfo` していない
  (接続成功経路)。こちらのパッチでは修正済み

### 12-14. ビルド構成・堅牢化系 (上流と要相談)

- 12: `src/mruby/socket.c` の `<sys/socket.h>` includeはESP-IDFのrake側ビルド
  (ベアメタルnewlib) で失敗する。こちらは `ESP32_PLATFORM` 定義時に
  フォールバック定数を使う形で回避
- 13: mrbgem.rake の ESP-IDF 判定 (`build.name == "esp32"`)。ビルド名は
  プロジェクト側の自由なので、`spec` フラグや define による判定が望ましい。
  picoruby-mbedtls の同梱mbedtlsコンパイル除外リストも同様
- 14: posixポートのタイムアウト/EINTRリトライ。EINTRリトライは一般的な
  posix堅牢化として妥当 (fmrbではFreeRTOS Linuxポートの1ms SIGALRMで必須だった)。
  デフォルトタイムアウト値の是非は上流の設計判断

## 2026-09-02 追加: Content-Length を文字数で数えている [mrblib/http_client.rb]

**非 ASCII を含む本文が 1 バイトも取れない。** `read_response` の

```ruby
remaining = content_length - (response.length - header_end - 4)
...
remaining -= body_part.length
```

`Content-Length` はバイト数、`String#length` は UTF-8 の**文字数**。日本語が
混じると受信済みの量を少なく数えるので `remaining` が 0 にならず、もう空の
ソケットから読みに行き、`SSLSocket_recv` が `!connected` で -1 を返して
`SSL read failed` になる。

**内容依存で発火するので、ホストや TLS の問題に見える。** 実際 2026-09-02 の
切り分けでは、同じ raw.githubusercontent.com に対して

| 取得先 | 中身 | 結果 |
|---|---|---|
| `/` (301) | ASCII | ok |
| `LICENSE` | ASCII | ok 200 / 1069 B |
| `apps/paint_pad/paint_pad.app.toml` | 日本語を含む | SSL read failed |
| `registry.json` | 日本語を含む | SSL read failed |

example.com と api.github.com はどちらも ASCII なので通り、**ホストの差だと
誤診する道が開いている** (実際に SNI を疑って 30 分溶かした)。

修正は `bytesize` に置き換えるだけ。`header_end` は `index` が返す文字位置だが、
ヘッダは ASCII なのでバイト位置と一致する。

- fmrb 側: `lib/patch/picoruby-net-http/mrblib/http_client.rb` に適用済み
- 上流 PR: そのまま出せる (fmrb 固有の要素なし)。**優先度は高い** — 英語以外の
  応答を返す API が全部使えない

同じ罠は過去にも踏んでいる (NSF の再生ボタン)。
mruby の `String#length` を長さとして使うたびに踏む。

## fmrb固有でPRに含めないもの

- `fmrb_sys_malloc/free` (候補1) → 上流では malloc 等に読み替え
- `FMRB_SOCKET_*_TIMEOUT_MS` マクロ名 (タイムアウト系)
- esp_crt_bundle のデフォルトattach (ports/esp32/ssl_socket.c)。IDF固有機能なので
  上流に入れるなら `#ifdef ESP_PLATFORM` ガード付きの提案になる
- posix SSL の EINTR リトライの動機 (SIGALRM tick) はfmrb環境固有だが、
  リトライ自体は一般に正しい
