# picoruby 上流PR候補メモ (ネットワークAPI検証で発見したバグ)

2026-07-09 作成。Ruby ネットワークAPI (doc/ruby_network_api_design.md) の
Linux/実機(Tab5)検証で発見した picoruby 本体のバグと修正の一覧。
いずれ上流 (https://github.com/picoruby/picoruby) へPRを出すための引き継ぎ資料。

## 前提と引き継ぎ手順

- 本リポジトリが参照する picoruby は **c14aa4400cbdb54956de234fc7534fa642c356cc
  (3.0.1-2402-gc14aa440)** で古い。**PR前に必ず上流HEADで各ファイルの現状を確認**
  すること (既に直っている / 実装が書き換わっている可能性がある)
- こちらの修正の実体は全て `lib/patch/` 以下にある (rake setup がサブモジュールへ
  コピーする方式)。各ファイル先頭の "Family mruby patch" コメントに修正理由を記載済み
- 手順: (1) 上流HEADで該当箇所を確認 → (2) 残存していれば lib/patch との diff から
  fmrb固有部分 (後述) を除いた最小diffを作成 → (3) 再現手順を添えてPR
- 発見の経緯・再現ログは doc/ruby_network_api_design.md の実装状況の節を参照

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

## fmrb固有でPRに含めないもの

- `fmrb_sys_malloc/free` (候補1) → 上流では malloc 等に読み替え
- `FMRB_SOCKET_*_TIMEOUT_MS` マクロ名 (タイムアウト系)
- esp_crt_bundle のデフォルトattach (ports/esp32/ssl_socket.c)。IDF固有機能なので
  上流に入れるなら `#ifdef ESP_PLATFORM` ガード付きの提案になる
- posix SSL の EINTR リトライの動機 (SIGALRM tick) はfmrb環境固有だが、
  リトライ自体は一般に正しい
