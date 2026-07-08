# Ruby ネットワークAPI 設計書 (Net::HTTP / WebSocket / TLS)

2026-07-08 設計検討。対象は ESP32-P4 / Modern (Tab5)。

## 実装状況

- **Phase 1〜3 実装済み・esp32p4 ビルドOK (2026-07-08)、実機検証待ち**
  - TCPSocket / UDPSocket / TCPServer / SSLSocket / Net::HTTP /
    Net::WebSocket::Client を esp32p4 と linux のビルドに組込み
  - バイナリ: 3.38MB / 4MB (19% free)。組込み前 3.10MB から +0.28MB
    (主に証明書バンドルと socket/net gem 群)
- 実装で判明した事実 (設計からの変更点・注意点):
  - **ビルド名分岐の罠**: picoruby-socket / picoruby-mbedtls / net-websocket の
    mrbgem.rake は `build.name == "esp32"` 前提。P4 はビルド名 `esp32p4` の
    ため、そのままでは自前 lwIP のクローンや mbedtls ライブラリの二重リンクが
    走る。lib/patch の mrbgem.rake 3 本で esp32p4 を esp32 相当に扱うよう修正
  - **構造体レイアウト整合**: ports/esp32/*.c は `PICORB_PLATFORM_POSIX` を
    定義して fd ベースの `picorb_socket_t` を使うが、gem 本体 (src/) は rake
    ビルドで同 define が無いと LwIP 版レイアウトになり不整合を起こす。
    パッチ済み mrbgem.rake が ESP-IDF ビルドで同 define を gem スコープに追加。
    クロスツールチェーンに `<sys/socket.h>` が無いため src/mruby/socket.c も
    パッチ (ESP32_PLATFORM 時はフォールバック定数を使用)
  - **picoruby-socket/include を component の INCLUDE_DIRS に追加してはいけない**:
    gem 同梱の lwipopts.h (rp2040 用) が ESP-IDF lwIP の設定を破壊する。
    ポートソースは相対 include で socket.h を参照している
  - **picoruby-mbedtls の ports/common/*.c** (digest/cmac/hmac/md/pkey) は
    PICORUBY_SRCS への明示追加が必要 (cipher.c だけでは MbedTLS_* シンボル不足)
  - **ESP-IDF の mbedtls ポートに `mbedtls_net_poll` は無い** →
    SSLSocket_ready は select() (VFS経由) で実装
  - **net-websocket の mruby-pack gemdir パスが上流バグ** (存在しない
    picoruby-pack を参照) → lib/patch で mruby-pack に修正。
    `require 'pack'` は mruby-* gem が prebuilt_gems[] に "pack" として
    登録されるため実行時も解決される
  - **Linux ビルドの SSL は OpenSSL 実装 (2026-07-08 更新)**: 当初はビルド
    コンテナに OpenSSL ヘッダが無くスタブ化していたが、docker/Dockerfile に
    libssl-dev を追加して上流の ports/posix/ssl_socket.c (OpenSSL) をそのまま
    使用する構成に変更。デフォルトで `SSL_CTX_set_default_verify_paths`
    (システム CA ストア) + VERIFY_PEER なので、esp32p4 (esp_crt_bundle) と
    同じ「デフォルトで検証付き HTTPS」の挙動になる。
    Dockerfile 変更は develop への push で CI (docker-publish.yml) が
    ghcr:latest を再公開する。ローカルには同タグの派生イメージを構築済み

## 目的

- Ruby アプリから WiFi を使って外部の Web サーバと通信できるようにする
  - HTTP クライアント (GET/POST 等)
  - メモリに余裕があれば HTTPS (TLS)、WebSocket / WebSocket over TLS (wss)
- API は CRuby になるべく似せる (`Net::HTTP`, `TCPSocket`, `URI` 等)

リモートデスクトップ (doc/remote_desktop_design.md) で WiFi 基盤 (esp_hosted /
esp_wifi_remote / wifi_task) は既に実機動作済みであり、その上に Ruby 向けの
クライアント API を載せる位置付け。

## 前提となる調査事実 (2026-07-08 調査)

### 結論サマリ

**CRuby 互換の socket / Net::HTTP / WebSocket スタックは picoruby サブモジュール内に
既に存在し、ESP32 ポート (lwIP BSD ソケット + mbedTLS の SSLSocket) も実装済み。
現在のファームウェアのビルドに組み込まれていないだけ**である。
したがって本件は「新規実装」ではなく「有効化 + 統合 + 非ブロッキング対応」の作業になる。

### WiFi / ネットワーク基盤 (実装済み・実機確認済み)

- P4 側で esp_netif + lwIP がローカルに動作 (radio のみ C6 に RPC)。
  BSD ソケット・DNS (getaddrinfo) は P4 上で普通に使える
- STA 接続は C 側 `main/drivers/wifi/wifi_task.c` が起動時に確立
  (`/etc/wifi.toml`、指数バックオフ再接続、mDNS `fmruby.local`)。
  Ruby 側は「IP は既に取得済み (または wifi_task が再接続中)」を前提にできる
- 公開 API: `wifi_wait_for_ip()`, `wifi_is_connected()`, `wifi_get_ip_str()` (wifi_task.h)
- 実効スループット ~36Mbps (C6/HT20 律速)。BLE 併用時は約半減

### picoruby 側の既存資産 (未組み込み)

`components/picoruby-esp32/picoruby/mrbgems/` 以下:

| gem | 提供クラス | 実装 | esp32 ポート |
|---|---|---|---|
| picoruby-socket | BasicSocket, TCPSocket, TCPServer, UDPSocket, SSLSocket, SSLContext | C + mrblib | あり (`ports/esp32/`: lwIP BSD ソケット、ssl_socket.c は mbedTLS 直叩き) |
| picoruby-net-http | Net::HTTP, Net::HTTPResponse, Net::HTTP::Get/Post/…, URI | pure Ruby (mrblib のみ) | 不要 (socket に依存) |
| picoruby-net-websocket | Net::WebSocket::Client / Server | pure Ruby | 不要 (socket に依存) |
| picoruby-net-ntp / -net-mqtt | NTP / MQTT | pure Ruby | 不要 |
| picoruby-mbedtls | Digest 等の暗号プリミティブ | C | timing_alt.c 等は既に PICORUBY_SRCS に記載あり |

- `mrbgems/networking.gembox` (net-http + net-ntp + net-websocket) が用意されているが、
  `lib/add/family_mruby.gembox` には含まれていない
- `picoruby-socket` の mrbgem.rake は `build.name == "esp32"` のとき自前 lwIP/mbedtls の
  クローンをスキップし、ESP-IDF の lwIP / mbedtls を使う前提になっている
- 依存: net-websocket は picoruby-base64 / picoruby-rng / picoruby-pack / picoruby-mbedtls に依存。
  picoruby-net (別系統の Pico W 向けスタック) とは **conflict 宣言があるため併用不可**
  (今回は picoruby-socket 系を採用し picoruby-net は使わない)
- ライフサイクル: `require 'net/http'` / `require 'socket'` / `require 'net/websocket'`
  (require_name 定義済み)

### TLS 関連のビルド状況 (sdkconfig)

- mbedTLS + esp-tls はビルド済み。TLS 1.2 のみ (TLS 1.3 は無効)
- **証明書バンドル有効**: `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` (DEFAULT_FULL, MAX_CERTS=200)。
  esp_crt_bundle が使える状態
- TLS バッファ: IN 16KB / OUT 4KB、`MBEDTLS_DYNAMIC_BUFFER` 無効、
  `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y` → **TLS セッションは内部RAMから確保される**
  (PSRAM は約25MB空いているが TLS には使われない設定)
- HW アクセラレータ (AES/GCM/SHA/ECC/MPI) 有効
- `CONFIG_LWIP_MAX_SOCKETS=16` — httpd (リモートデスクトップ)、WS、mDNS、
  Ruby クライアントの全てがこの16を共有する

### esp32 ポート ssl_socket.c の現状の制約

- `mbedtls_ssl_conf_authmode(VERIFY_REQUIRED)` がデフォルト (方向性は CRuby と同じ)
- ただし CA は `SSLContext_set_ca` (PEM をメモリで渡す) のみ対応で、
  **esp_crt_bundle を使っていない**。つまり現状のままでは
  「CA を自分で埋め込まない限り一般の HTTPS サイトに接続できない」
- `set_ca_file` / `set_cert_file` は esp32 では未対応 (false を返す)

### 実行モデルとブロッキング問題 (設計上の最重要課題)

- VM は mruby (microruby, PICORB_VM_MRUBY=1) + 協調スケジューラ mruby-task
- アプリの `main_loop` は `on_update` → `Task.pass` → `_spin(timeout)` の繰り返し。
  `_spin` が per-app の FreeRTOS メッセージキューを排出して HID イベント等を
  `on_event` にディスパッチする (picoruby-fmrb-app/ports/esp32/app.c)
- esp32 ソケットポートは **ブロッキング** BSD ソケット (`connect`/`recv`)。
  `on_update` 内で長時間ブロックすると、そのアプリの VM タスクが停止し
  イベント処理 (キー/マウス) と描画が止まる
- 緩和材料: `TCPSocket#ready?` (poll 相当) が C 実装済み。
  net-websocket の `receive(timeout:)` は `ready?` ポーリングベースで実装されており
  協調スケジューラと相性が良い

### メモリ・リソース見積もり

- PSRAM: ~25MB 空き (潤沢)。内部RAM: BLE+esp_hosted で 80-100KB 消費済み、
  新規コードは IRAM 使用不可の状況 (remote_desktop_design.md)
- TLS 1セッションの内部RAM消費 (概算): SSL in/out バッファ 16KB+4KB +
  ハンドシェイク時ピーク (証明書チェーン検証) 10-30KB + コンテキスト数KB
  → **1セッションあたり約 30-50KB (内部RAM)**。同時 TLS セッションは 1-2 に制限すべき
- 証明書バンドル (FULL) は Flash 側 ~70-80KB。factory パーティションは
  約1.2MB 空きなので問題なし
- 平文 TCP/HTTP のみなら 1 接続あたり数KB (lwIP PCB + Ruby 側バッファ) で軽量

## 方針 (提案)

### 設計判断: 新規実装ではなく既存 gem の有効化を軸にする

- CRuby 互換 API (`Net::HTTP.get`, `start`/`request`, `TCPSocket`, `URI`) は
  picoruby-net-http / picoruby-socket が既に提供しており、独自 API を発明しない
- fmruby 固有の作業は「ビルド組込み」「esp_crt_bundle 対応」「協調スケジューラとの
  親和性確保」に集中する
- サブモジュール直接編集は禁止のため、ssl_socket.c 等の修正は `lib/patch/picoruby-socket/`
  に置き Rakefile で上書きする (既存の picoruby-i2c 等と同じ方式)

### 設計判断: 同期 API を基本とし、長時間ブロックはタイムアウトで抑制する

- CRuby の `Net::HTTP.get` も同期 API であり、「同期で書ける」こと自体が
  CRuby 互換の価値。まずは同期 API をそのまま出す
- ただしデフォルトで SO_RCVTIMEO / SO_SNDTIMEO (例: 10秒) と接続タイムアウトを
  esp32 ポートに設定し、「無限に固まる」ことだけは防ぐ (lib/patch で対応)
- 「HTTP リクエスト中はそのアプリのイベント処理が止まる」ことは
  ドキュメント化された制限とする (ゲーム的アプリはローディング画面を挟む想定)
- 非同期化 (専用ネットワークタスク + fmrb_msg で on_event 配信) は Phase 4 の
  拡張として設計だけ示し、需要が出てから実装する

### 設計判断: TLS はデフォルトで証明書検証 + esp_crt_bundle

- CRuby 同様「デフォルトで安全」にする: `use_ssl = true` だけで
  esp_crt_bundle (Mozilla CA 相当) による検証付き HTTPS が通ること
- `SSLContext#set_ca(pem)` による独自 CA 指定 (オレオレ証明書サーバ) も残す
- `verify_mode = SSL_VERIFY_NONE` も CRuby 同様に指定可能とする (非推奨)
- 実装: lib/patch の ssl_socket.c で、ユーザ CA 未指定時に
  `esp_crt_bundle_attach(&ssl_config)` を呼ぶ。`mbedtls_ssl_set_hostname` による
  SNI/ホスト名検証が入っていることも実装時に確認する

### 設計判断: WiFi 状態は既存 wifi_task を参照する薄い API を用意

- WiFi の init/接続管理は C 側 wifi_task の責務のまま変えない
  (picoruby-esp32 gem の `ESP32::WiFi.init` のような Ruby 主導の接続は使わない)
- Ruby からは状態参照のみできる小さな API を picoruby-fmrb-kernel (または新規
  picoruby-fmrb-net) に追加する:

```ruby
FmrbNet.connected?   # => true/false      (wifi_is_connected)
FmrbNet.ip_address   # => "192.168.10.15" (wifi_get_ip_str)
FmrbNet.hostname     # => "fmruby"
FmrbNet.wait_for_ip(timeout_ms) # => true/false (wifi_wait_for_ip)
```

## アーキテクチャ

```
Ruby アプリ (FmrbApp)
  |  require 'net/http' / 'net/websocket' / 'socket'
  v
Net::HTTP / Net::WebSocket::Client / URI        [pure Ruby / mrblib]
  v
TCPSocket / SSLSocket / SSLContext              [picoruby-socket src/mruby]
  v
ports/esp32/{tcp_socket,udp_socket,ssl_socket,net_helpers}.c
  |            (lwIP BSD sockets)   (mbedTLS + esp_crt_bundle)
  v
esp_netif + lwIP (P4 ローカル)
  v
esp_wifi_remote / esp_hosted (SDIO) --- ESP32-C6 (radio)
```

- WiFi 接続確立・再接続: 既存 wifi_task (変更なし)
- リモートデスクトップ (httpd) とはソケット数 (16) と帯域 (~36Mbps) を共有

## API 仕様 (Ruby から見た姿)

基本的に picoruby-net-http / -net-websocket の既存 API をそのまま公開する。
CRuby と同じ書き方ができる範囲の例:

```ruby
require 'net/http'

# 単発 GET
body = Net::HTTP.get(URI.parse("http://example.com/api/status"))

# レスポンスオブジェクト
res = Net::HTTP.get_response(URI.parse("https://example.com/data.json"))
if res.code == "200"
  data = JSON.parse(res.body)   # picoruby-json は組込済み
end

# POST (フォーム)
res = Net::HTTP.post_form(URI.parse("https://example.com/post"), {"key" => "value"})

# セッション再利用 (start/request)
http = Net::HTTP.new("example.com", 443)
http.use_ssl = true
http.start do |h|
  res = h.get("/index.html")
  res = h.post("/api", '{"a":1}', {"Content-Type" => "application/json"})
end
```

```ruby
require 'net/websocket'

Net::WebSocket::Client.connect("wss://echo.example.com/ws") do |ws|
  ws.send_text("hello")
  msg = ws.receive(timeout: 5)   # ready? ポーリングなので VM を長時間止めない
  ws.close
end
```

### CRuby との主な差分 (ドキュメント化する制限)

- TLS 1.2 のみ (TLS 1.3 非対応。現状の sdkconfig 準拠)
- `Net::HTTP` はチャンク転送・リダイレクト追跡・keep-alive 等で簡易実装
  (picoruby-net-http の実装範囲に準ずる。実装範囲は検証時に一覧化する)
- `SSLContext` の CA/クライアント証明書はファイルパス指定不可、PEM 文字列渡しのみ
- 大きなレスポンスはメモリ制約に注意 (アプリの fmrb_mem プール上限、
  USER_APP プール 500KB/1MB)。ストリーミング読みは `dest`/block 引数の対応状況を確認
- 同期 API 実行中はそのアプリのイベント処理が止まる (タイムアウト上限あり)

## 変更ファイル一覧 (実装済み)

- `lib/add/family_mruby_esp32p4.rb` / `lib/add/family_mruby_linux.rb`:
  picoruby-socket / picoruby-net-http / picoruby-net-websocket を追加
- `lib/patch/picoruby-socket/mrbgem.rake`: esp32p4 対応 (lwIP クローン抑止、
  PICORB_PLATFORM_POSIX を gem スコープで定義)
- `lib/patch/picoruby-socket/src/mruby/socket.c`: sys/socket.h 回避
- `lib/patch/picoruby-socket/ports/esp32/tcp_socket.c`: 接続/送受信の
  デフォルトタイムアウト (10秒、FMRB_SOCKET_*_TIMEOUT_MS)、freeaddrinfo リーク修正
- `lib/patch/picoruby-socket/ports/esp32/ssl_socket.c`: esp_crt_bundle による
  デフォルト証明書検証 (set_ca で独自CAに切替)、SSLSocket_ready の実装修正
  (旧実装は常に false)、read タイムアウト、mbedtls エラーコードのログ出力
- `lib/patch/picoruby-mbedtls/mrbgem.rake`: esp32p4 で同梱 mbedtls の
  コンパイルをスキップ (ESP-IDF の mbedtls を使用)
- `lib/patch/picoruby-net-websocket/mrbgem.rake`: mruby-pack の gemdir 修正
- `docker/Dockerfile`: libssl-dev を追加 (Linux ビルドの OpenSSL SSL ポート用)。
  posix ビルドは picoruby ビルドが ports/posix を自動コンパイルするため、
  上流の OpenSSL 実装がそのまま使われる (パッチ不要)
- `lib/add/picoruby-fmrb-kernel/ports/esp32/net.c`: FmrbNet モジュール
  (connected? / ip_address / hostname / ssid / wait_for_ip)
- `lib/add/picoruby-fmrb-kernel/src/picoruby_fmrb_kernel.c`: FmrbNet init 呼出し
- `Rakefile`: 上記パッチのコピーを setup タスクへ追加
- `components/picoruby-esp32/CMakeLists.txt`: socket ポート (esp32p4/posix)、
  picoruby-mbedtls ports/common/*.c、lwip PRIV_REQUIRES を追加

## 実装ステップ

### Phase 1: 平文 TCP / HTTP の有効化

1. `lib/add/family_mruby.gembox` に追加:
   - `picoruby-socket`, `picoruby-net-http` (まず最小構成)
2. `components/picoruby-esp32/CMakeLists.txt` の esp32 側 `PICORUBY_SRCS` に
   `picoruby-socket/ports/esp32/*.c` (tcp_socket / udp_socket / tcp_server /
   net_helpers、ssl_socket は Phase 2) を追加
   - linux ターゲット側は posix ポートが gem ビルドで賄われるか確認し、
     必要なら linux 側 PICORUBY_SRCS にも追加
3. `FmrbNet` 状態参照 API を追加 (wifi_task.h の関数を薄く公開)
4. esp32 ポートに接続/送受信タイムアウト (SO_RCVTIMEO 等) を lib/patch で追加
5. 動作確認: LAN 内の HTTP サーバ + 外部サイトへの GET/POST

- ビルド規約: lib/ 変更後は `rake clean`、gembox 変更はビルド3系統
  (IDF / rake / PICORUBY_SRCS、doc/core_build_structure.md) への影響を確認

### Phase 2: TLS (HTTPS)

1. `picoruby-mbedtls` を gembox 依存として整理 (timing_alt.c 等は記載済み)
2. `lib/patch/picoruby-socket/ports/esp32/ssl_socket.c` を作成:
   - ユーザ CA 未指定時に `esp_crt_bundle_attach()` を使用
   - `mbedtls_ssl_set_hostname` の設定確認 (SNI + ホスト名検証)
   - エラー時に mbedtls エラーコードを Ruby 例外メッセージへ含める
3. `main`/`picoruby-esp32` コンポーネントの REQUIRES に esp-tls (esp_crt_bundle) を追加
4. メモリ計測: ハンドシェイク中/接続維持中の内部RAM消費を
   `fmrb_mem_print_psram_info` + heap_caps で実測し、本書に追記
5. 動作確認: `https://` の実サイト GET (証明書検証成功/失敗の両方)

### Phase 3: WebSocket / wss

1. gembox に `picoruby-net-websocket` (+依存: picoruby-base64) を追加
2. `ws://` はローカルサーバ、`wss://` は外部サービスで送受信確認
3. `receive(timeout:)` ポーリングと `on_update` ループの組合せ方を
   サンプルアプリとして flash/ 以下に用意 (例: WebSocket チャット/エコー)

### Phase 4 (オプション、需要が出たら): 非同期ヘルパ

- 専用 FreeRTOS ネットワークサービスタスクを立て、Ruby からは
  `FmrbNet.async_get(url) { |res| ... }` のような形で発行、完了は
  fmrb_msg (per-app キュー) 経由で `on_event` (`:net_response`) に配信する
- audio / file_transfer と同じ「Ruby → キュー → サービスタスク」パターン
  (host_task.c) を踏襲
- 同期 API で実運用上困るケースが確認できてから着手する

## sdkconfig 変更提案 (現時点では変更不要、必要になった場合の候補)

sdkconfig は直接編集せず、必要が確定した時点で提案する (ビルド規約)。

- `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`: アイドル時の TLS バッファを解放し内部RAM を節約
  (ハンドシェイク性能とのトレードオフ)
- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`: TLS ヒープを PSRAM に逃がす
  (内部RAM 逼迫が実測で確認された場合。速度低下は HW アクセラレータで概ね吸収可能か要実測)
- `CONFIG_LWIP_MAX_SOCKETS` 増加: リモートデスクトップ併用でソケット枯渇が起きた場合
- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN`: Flash を節約したい場合に
  バンドルを主要CAのみへ縮小
- いずれも変更時は `rake clean_all` が必要

## リスクと対策

| リスク | 影響 | 対策 |
|---|---|---|
| 同期 API のブロックで UI 停止 | アプリの操作性低下 | デフォルトタイムアウト + 制限のドキュメント化 + Phase 4 非同期ヘルパ |
| TLS の内部RAM 消費 (30-50KB/セッション) | 他機能 (BLE, RD) と競合し確保失敗 | 同時 TLS 1-2 セッション制限、実測して必要なら EXTERNAL_MEM_ALLOC 提案 |
| ソケット16個の共有枯渇 | RD 配信中に接続失敗 | Ruby 側の同時接続数を制限、枯渇時は Errno 相当の例外で通知 |
| picoruby-socket esp32 ポートの品質 (実戦投入例が少ない可能性) | ハング/リーク | Phase 毎に長時間ソーク試験、close 漏れ検査 (lsof 相当の lwIP stats) |
| BLE 併用時の帯域半減 (~18Mbps) | 大きなダウンロードが遅い | 制限として記載。RD 同時使用時も同様 |
| gembox 追加によるバイナリサイズ増 | factory 4MB を圧迫 (現在 3.10MB) | Phase 毎にサイズ計測。net-http/websocket は pure Ruby で小さい見込み |
| picoruby-net との conflict | ビルドエラー | picoruby-net 系は採用しない (本書で明記) |

## 実機検証手順 (Phase 1-3 のスモークテスト)

アプリの on_update などから以下を実行して確認する:

```ruby
# 0. WiFi 状態
p FmrbNet.connected?    # => true (wifi.toml 設定済みなら)
p FmrbNet.ip_address    # => "192.168.x.x"

# 1. 平文 HTTP GET
require 'net/http'
res = Net::HTTP.get_response(URI.parse("http://example.com/"))
p res.code              # => "200"
p res.body[0, 60]

# 2. HTTPS (証明書バンドル検証)
res = Net::HTTP.get_response(URI.parse("https://example.com/"))
p res.code              # => "200"
# 検証失敗系: 不正証明書のサイト (https://self-signed.badssl.com/ など) で
# RuntimeError になること

# 3. WebSocket (エコーサーバを PC 側で用意: 例 python -m websockets)
require 'net/websocket'
Net::WebSocket::Client.connect("ws://<PCのIP>:8765/") do |ws|
  ws.send_text("hello")
  p ws.receive(timeout: 5)   # => "hello"
end

# 4. タイムアウト (応答しないアドレスに接続して約10秒で例外)
require 'socket'
t0 = Time.now.to_i
begin
  TCPSocket.new("192.0.2.1", 81)   # TEST-NET-1: 応答しない
rescue => e
  p [e.class, Time.now.to_i - t0]
end
```

確認ポイント:
- リモートデスクトップ併用時に双方が動作すること (ソケット16個の共有)
- HTTPS ハンドシェイク中の内部RAM残量 (`fmrb_mem_print_psram_info` /
  デバイスログ) を記録して本書のメモリ見積もりを実測値で更新する
- 接続/切断を繰り返してヒープが減り続けないこと

## 検証計画

1. **Linux ターゲット (WSL2)**: posix ポートで Net::HTTP / WebSocket の
   ロジック検証。HTTPS も OpenSSL + システム CA で実機同様に検証可能
   (GUI 不要のスクリプトなら CLAUDE Code 環境でも実行可能か確認)
2. **実機 Phase 1**: LAN 内 HTTP サーバへ GET/POST、外部サイト GET。
   RD 併用状態での動作、タイムアウト動作 (サーバ無応答時に指定秒で例外)
3. **実機 Phase 2**: 実在 HTTPS サイトへの GET (バンドル検証成功)、
   不正証明書サイトで検証失敗すること、`set_ca` での自己署名サーバ接続。
   ハンドシェイク前後の内部RAM 実測
4. **実機 Phase 3**: wss:// エコーサーバで送受信、`receive(timeout:)` 中に
   キー/マウスイベントが処理されること (UI が固まらないこと) を確認
5. **ソーク試験**: 接続/切断を数百回繰り返しヒープ残量が安定していること

## 実装時確認事項 (設計段階では未確認)

- linux (posix) ビルドで picoruby-socket の ports/posix が gem ビルドに含まれる経路
- esp32 ポート ssl_socket.c が `mbedtls_ssl_set_hostname` を呼んでいるか
- picoruby-net-http の実装範囲 (チャンク転送、リダイレクト、Content-Length 無し応答)
- `TCPSocket#read` のブロック粒度と `ready?` の実装 (poll/select どちらか)
- mruby-task の `Task.pass` とソケットポーリングを組み合わせた際の CPU 占有率
- 大きなボディ受信時のメモリプール (fmrb_malloc) からの確保挙動
