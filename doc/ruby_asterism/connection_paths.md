# Asterism の網への接続経路の整理 (端末・ブラウザ版・ゲームホスト・console)

> 状態: 参照 | 更新: 2026-10-19 | VPS の Asterism の網 (zenohd) に、誰がどの経路・どの身元でつながっているかの整理。門 (door) が要るのは証明書を持てないブラウザ版だけで、端末とサーバは自分の証明書で zenohd に直接入る

関連: `room_auth_design.md` (部屋の入り方の設計)、asterism-console の `deploy/vps/docker-compose.yml` (待ち受けの一覧)、`docs/rooms.md`、`lib/relay/room_door.rb` と `bin/ws_door` (門)、`wasm/zenoh/zp_ws_emscripten.c` (ブラウザ版の WebSocket のつなぎ)。

## 1. 一覧

| 参加者 | Zenoh の口 | つなぎ先 | 身元 | 門 |
|---|---|---|---|---|
| 端末 (Tab5・P4・S3) | zenoh-pico (C) + `Asterism::Zenoh` (picoruby-asterism-zenoh) | VPS の zenohd の mTLS の待ち受けへ直接 (公開の番号 `ROUTER_TLS_PORT` → コンテナの 7448) | 端末の証明書 (console が渡す tar、`/home/asterism/`)。証明書の名前がノード ID | 通らない |
| ブラウザ版 (パスで入る) | zenoh-pico を wasm にしたもの (WebSocket だけでビルド) | `wss://` → nginx → パスの門 (7451) → zenohd の WebSocket の待ち受け (`ws/`、7449) | パス (console が発行、nginx が確かめる)。ノード ID はブラウザが作る `wasm-<6 hex>` かリンクが名指すもの | 通る (中継と寿命) |
| ブラウザ版 (部屋で入る) | 同上 | `wss://` → nginx → 部屋の門 (7452) → zenohd の mTLS の待ち受け (`tls/`、7448) | 部屋の鍵 (Ed25519 の署名) と席。門が**席の証明書**で代わりにつなぐ。ノード ID は席 (`wasm-<枠><席>`) | 通る (確かめと変換) |
| ゲームホスト (CRuby) | `asterism` gem + `asterism-zenoh` (zenoh-c の上) | docker の網 (`<project>_router`) の中から `tls/zenohd:7448` | ゲームホストの証明書 (`credentials/games.tar`) | 通らない |
| console (bridge) | 同上 | docker の網の中から `tls/zenohd:7448` | console の証明書 (`/certs/console.pem`、ノード `console`) | 通らない |
| console (Rails の画面) | なし (Asterism を読み込まない) | bridge と Action Cable・データベースでやり取り | — | — |

家の中の LAN では、端末は家のルータに TCP でつなぐこともできる (`/home/asterism/config.toml`)。

## 2. ブラウザ版の経路の詳しいところ

### 2.1 wss と nginx

- ブラウザの WebSocket が TLS を担い、`wss://<router>:<nginx の wss の待ち受け>` に入る。nginx が TLS を終え、同じ VPS の中の門へ平文の WebSocket で渡す。
- ページの指定は `ws/<host>:<port>` で書くが、https で配信されたページからは自動で `wss://` を選ぶ。パスや部屋の鍵は wss のときだけ送り、平文の ws では送らない。

### 2.2 パスの門 (7451)

- バイトをそのまま流す TCP の中継。WebSocket のまま zenohd の `ws/` の待ち受けへ渡す。
- パスは nginx が console に問い合わせて確かめる。門は接続の寿命だけを持つ: 最大 4 時間 (`WS_DOOR_MAX_SECONDS`)、またはパスの期限 (nginx が `X-Asterism-Until` で渡す) で両側を切る。Zenoh は数秒ごとに生存確認を送るので、nginx の無通信のタイムアウトでは切れないため。
- zenohd が再起動中なら、門が 503 を返す (nginx がパス入りのリクエスト行を error log に書かないように)。

### 2.3 部屋の門 (7452)

- 門が WebSocket を自分で終え、部屋の鍵と席を確かめてから、席の証明書で zenohd の mTLS の待ち受けにつなぐ。
- 形式の変換: WebSocket の 1 メッセージ = Zenoh の 1 まとまり (batch) を、TLS 側の「2 バイトの長さ (リトルエンディアン) + まとまり」にする。最初の InitAck は、長さの 2 バイトの分だけ batch の大きさを下げて渡す。
- 席の要求 (`POST /room/seat`)、入り直し、期限と取り消しの詳細は `room_auth_design.md` と asterism-console の `docs/rooms.md`。

### 2.4 ブラウザ版の zenoh-pico

- `Z_FEATURE_LINK_TCP=0`、`Z_FEATURE_LINK_WS=1` でビルド (`wasm/CMakeLists.txt`)。
- WebSocket のつなぎは、zenoh-pico 1.10.1 の Emscripten 用のファイルを元に書き直した `wasm/zenoh/zp_ws_emscripten.c`。ブラウザの WebSocket はページの主スレッドで非同期に動き、zenoh-pico は Web Worker の中の FreeRTOS のタスクから同期的に読み書きする。その食い違いをつなぐ (経緯は `report/b1.md`)。

## 3. VPS の待ち受け

| どこ | 中 (コンテナ) | 外への公開 |
|---|---|---|
| zenohd | `tls/7448` (mTLS、自前の CA) | `ROUTER_TLS_PORT` で公開 (端末が直接つなぐ)。さくらのパケットフィルタで送信元を絞れる |
| zenohd | `ws/7449` (ブラウザ版) | 公開しない (門だけがつなぐ) |
| door | 7451 (パスの門) | `127.0.0.1` だけ (nginx が前に立つ) |
| door | 7452 (部屋の門) | `127.0.0.1` だけ (nginx の `/room`・`/room/seat`) |
| door | 7453 (接続の数、`GET /status`) | 公開しない (console が docker の網の中から聞く) |
| games | 7460 (`GET /status`) | 公開しない (console が docker の網の中から聞く) |
| nginx (VPS 本体) | — | 443 (console の画面) と wss の待ち受け (門へ) |

## 4. まとめ

- 門が要るのは、証明書を持てないブラウザ版だけ。パスなら寿命つきの中継、部屋なら鍵の確かめと席の証明書への読み替え。
- 端末・ゲームホスト・console (bridge) は、それぞれの証明書で zenohd の mTLS に直接入り、zenohd の ACL がノード ID (証明書の名前) ごとに権限を決める。
- Zenoh の口は、端末とブラウザ版が zenoh-pico、サーバが zenoh-c。その上の Asterism の層は、端末 (picoruby-asterism) とサーバ (asterism gem) で同じ Ruby のソース。
