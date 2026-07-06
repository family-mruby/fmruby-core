# リモートデスクトップ機能 設計書 (ESP32-P4 / Modern)

2026-07-06 設計。

## 実装状況

- **Phase 1: 実装済み・ビルドOK (2026-07-06)、実機検証待ち**
  - 実装で判明した事実:
    - esp_hosted 1.4.0 では `esp_hosted_setup()` が `#if 0` で無効化されて
      いる → 代わりに `is_transport_rx_ready()/is_transport_tx_ready()`
      (transport_drv.h) をポーリングして transport-up を待つ (RPC無し)
    - sdkconfig.defaults 変更時は `rake clean_all` が必要 (ビルド規約)
    - EMBED_FILES のシンボル名は `_binary_<basename>_start` (パス除去)
- **Phase 2: 実装済み・ビルドOK (2026-07-07)、実機検証待ち**
  - 実装で判明した事実 (設計からの変更点):
    - **esp_h264 v1.1.x の HWエンコーダは `ESP_H264_RAW_FMT_RGB565_LE` を
      直接サポート** (旧READMEのYUV限定情報は誤り/古い)。よって
      **PPA/CPUの色変換は不要**になり、キャプチャバッファ(RGB565
      non-swapped LE)を 432px 行パディングしてそのまま投入する。
      色の正しさ(赤青反転)は実機検証項目
    - IDR フレームには SPS/PPS が自動付与される (esp_h264 仕様) →
      サーバ側での SPS/PPS キャッシュ/前置は不要
    - IDR オンデマンドの直接APIは無い → `esp_h264_enc_set_gop(param, 1)`
      で1フレームだけGOP=1にして復元するトリックで実装 (要実機検証。
      効かない場合も GOP=30 で最悪2秒待ちで復帰)
    - display_p4_capture_enable は参照カウント化 (MJPEG と H.264 が併用
      するため)
  - バイナリ: 3.10MB / 4MB (26% free)。Phase1: 3.06MB
- **実機検証: 未実施** (検証計画 A〜D を参照)

Tab5 (ESP32-P4) のデスクトップ画面をPCブラウザへ配信し、マウス/キーボード
入力を送り返すブラウザ経由のリモートデスクトップ機能。

## 方針 (ユーザ決定事項)

- **段階実装**: Phase 1 = WiFi + MJPEG (JPEG HWエンコーダ) + リモート入力。
  Phase 2 = H.264 HWエンコーダ + WebSocket + WebCodecs (低帯域・低遅延)。
  MJPEG は非Chromeブラウザ向けフォールバックとして残す
- **ビューアはデバイス内蔵配信**: P4 の esp_http_server が HTML/JS を配信。
  `http://<デバイスIP>/` を開くだけで使える。ソースは tool/web/remote/、
  ビルド時に EMBED_FILES で埋め込み
- **WiFi資格情報は TOML**: 機密は /etc/wifi.toml に分離し git に入れない
- esp_hosted 1.4.0 + ストックC6ファーム 1.4.1 の組は**維持**
  (BLE Webコンソールで実績のある構成を崩さない)

## 前提となる調査事実

### P4 のエンコーダ

- **H.264 HWエンコーダ (espressif/esp_h264 v1.1.x) は YUV入力のみ**
  (ESP_H264_RAW_FMT_O_UYY_E_VYY が native。VUY/UYVY も可)。RGB は不可。
  RGB565 からの変換は **PPA のHWカラーコンバート (RGB565→YUV420)** を使う。
  PPA出力レイアウトと O_UYY_E_VYY の一致は Phase 2 の最初に単体検証し、
  不一致なら CPU 固定小数点変換 (~102k px/frame、15fps 可) にフォールバック
- 幅は 16px マクロブロック整列が必要: 426 → **432 にパディング** (高さ240はOK)
- ビットレート/GOP: esp_h264_enc_set_bitrate / set_gop。この解像度なら
  0.5-1.5Mbps、GOP 15-30 が妥当
- **JPEG HWエンコーダ (esp_driver_jpeg) は RGB565 直入力可**
  (RGB→YUVを内蔵)。432x240 なら数百fps 相当でボトルネックにならない。
  MJPEG 1フレーム ~8-20KB → 15fps で 1-2.4Mbps

### WiFi (esp_hosted / esp_wifi_remote)

- `CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED=y` (設定済み) で **esp_wifi_* API が
  そのまま C6 へ RPC** される。esp_netif + lwIP は P4 側で動くので
  ソケット/HTTPサーバは通常どおり使える
- 既知の罠:
  - `WIFI_INIT_CONFIG_DEFAULT()` を使う (ゼロ初期化構成は INVALID_ARG)
  - `esp_event_loop_create_default()` の ESP_ERR_INVALID_STATE は許容
    (esp_hosted が先に作っていることがある)
  - **RPC はトランスポート確立後に発行** (esp_hosted 1.4.0 の
    pre-transport RPC ヒープ破壊。BLE で実績のある罠 → doc/ble_c6_web_console.md)
- 実効スループット ~36Mbps (C6律速、**HT20固定**)。BLE 併用時は
  C6 の時分割で ~50% 減 (MJPEG 2Mbps には十分)
- BLE + WiFi は同一 SDIO トランスポートに多重化され同時利用可

### フレームバッファ / 入力注入 (fmruby-core 側)

- キャプチャポイント: display_p4_task.cpp render_frame() の**合成完了時点**
  (SRM転送前) の g_framebuffer = 426x240 RGB565 non-swapped (PSRAM)。
  描画は**イベント駆動**でアイドル時はフレームが来ない
- **カーソルは g_framebuffer に含まれない** (g_dsi_fb への直接パッチ)。
  → カーソル位置はメタデータで送りクライアント側描画
- 入力注入: `fmrb_host_send_key_down/up(key, scancode, mod)`,
  `fmrb_host_send_mouse_move(x, y)`, `fmrb_host_send_mouse_click(x, y, btn, state)`
  (host_task.c、キュー式でスレッドセーフ)。座標系 = 仮想426x240、
  scancode = HID usage ID (usb_task / tab5_keyboard と同一規約)
- リソース: PSRAM ~25MB free、IRAM ~185KB (新規コードは IRAM 禁止)、
  factory パーティション 4MB 中 ~1.2MB 残

## アーキテクチャ

```
[display_p4 task core1] render_frame合成完了
      │ capture有効時: ダブルバッファへcopy(~200KB/1-2ms) + seq++ + sem give
      ▼
[capture double buffer PSRAM 2x200KB]
      │ acquire/release (シングルリーダ)
      ▼
P1: [httpdハンドラ core0] JPEG HWエンコード → multipart/x-mixed-replace (/stream)
P2: [rd_stream task core1 prio4] PPA色変換(432x240 YUV420)
      → esp_h264 HW → /ws_video (Annex B、httpd_ws_send_frame_async)
      ▲
[browser] <img>(P1) / WebCodecs VideoDecoder(P2)
      │ /ws: 入力(バイナリ) + カーソル/統計メタ(JSON)
      ▼
[rd_input] → fmrb_host_send_* → HOSTタスク → カーネル → フォーカスアプリ
```

- **アイドル対策**: ストリーマは「新フレーム semaphore 待ち + 500ms タイム
  アウトで直近フレーム再送」(キープアライブ + 遅参クライアント対応)。
  起動直後のみ display_p4_capture_kick() で 1 フレーム強制描画
- **カーソル**: 送信時に display_p4_get_cursor() をサンプリングし、
  "cur" JSON を 33ms 間隔 (変化時のみ) で配信。クライアント側でオーバレイ描画

## モジュール構成

```
main/drivers/wifi/
  wifi_task.c/.h        # netif/イベントループ/STA接続/再接続(指数バックオフ1s..30s)
                        # /etc/wifi.toml 読込、mDNS(fmruby.local)
main/drivers/remote_desktop/
  rd_task.c/.h          # ライフサイクル (IP取得待ち→設定読込→httpd起動)
  rd_http.c/.h          # "/"(ビューア) "/stream"(MJPEG) "/ws" "/status"
  rd_encoder_jpeg.c/.h  # Phase1: esp_driver_jpeg ラッパ
  rd_encoder_h264.c/.h  # Phase2: PPA色変換 + esp_h264
  rd_input.c/.h         # WSバイナリ入力 → fmrb_host_send_* ブリッジ
main/drivers/display_p4/     # capture API 追加 (下記)
main/boot/boot.c             # modern_ble_init_task → modern_radio_init_task
                             # (BLE init → WiFi init を逐次実行)
tool/web/remote/
  index.html / remote.js / keymap.js   # ビューア (EMBED_FILES 埋込)
config/wifi_p4.toml.example  # テンプレート (実物 config/wifi_p4.toml は
                             # .gitignore + Rakefile 条件付きコピー)
```

### display_p4 に追加する capture API (extern "C")

```c
typedef struct {
    const uint16_t *pixels;   // RGB565 non-swapped, 426x240
    uint16_t width, height;
    uint32_t seq;
} display_p4_capture_frame_t;

fmrb_err_t display_p4_capture_enable(bool enable);   // バッファ確保/解放
fmrb_err_t display_p4_capture_acquire(uint32_t min_seq, uint32_t timeout_ms,
                                      display_p4_capture_frame_t *out);
void       display_p4_capture_release(void);
void       display_p4_capture_kick(void);            // 1フレーム再描画要求
void       display_p4_get_cursor(int *x, int *y, bool *visible);
```

- render_frame() 合成完了直後に、capture 有効かつフロントバッファ未ロック時
  のみ memcpy → インデックス反転 → seq++ → semaphore give。
  リーダがロック中はライタはバックバッファに書き続けフレームドロップ
- バッファは jpeg_alloc_encoder_mem (キャッシュライン整列PSRAM) で確保し、
  JPEG エンコーダの DMA 入力にそのまま使う

### タスク配置

| タスク | core | prio | stack | 備考 |
|---|---|---|---|---|
| modern_radio_init_task (one-shot) | - | 4 | 6144 | BLE init → WiFi init 逐次。既存 ble init タスクを改名拡張 |
| httpd (esp_http_server内部) | 0 | 5 | 8192 | hosted/lwip と同じ core0。Phase1 は MJPEG エンコードもこの文脈 (追加タスクゼロ) |
| rd_stream (Phase2のみ) | 1 | 4 | 8192 | display(prio5) より下。core1 でフレームバッファに近接 |

## WiFi ライフサイクル

- boot.c: display_p4_is_ready() 待ち → ble_task_init() → wifi_task_init() の
  **逐次実行** (BLE の nimble_port_init() が SDIO トランスポートを立ち上げる
  実績パスを踏襲し、WiFi RPC は必ずトランスポート確立後)
- BLE 無効/失敗時: wifi_task_init 内で esp_hosted_setup() (RPCを発行せず
  transport_up セマフォを待つだけ) でトランスポート確立を保証してから
  esp_wifi_init する
- 初期化列: esp_netif_init → esp_event_loop_create_default(INVALID_STATE許容)
  → esp_netif_create_default_wifi_sta → esp_wifi_init(WIFI_INIT_CONFIG_DEFAULT())
  → STA設定 → start。STA_START/DISCONNECTED で connect、指数バックオフで無限リトライ
- GOT_IP: ログ表示 + イベントグループで rd_task へ通知。mDNS
  (`fmruby.local`、wifi.toml の hostname) を初期化

## 設定 (TOML)

`/etc/wifi.toml` (機密分離。system_conf.toml はビルド毎に config/ から
上書きコピーされるため資格情報を置けない):

```toml
[wifi]
enable = true
ssid = "myssid"
password = "mypassword"
hostname = "fmruby"        # mDNS: http://fmruby.local/
```

system_conf.toml (config/system_conf_p4.toml) に追加:

```toml
[remote_desktop]
enable = true
mode = "mjpeg"             # "mjpeg" | "h264" (Phase2)
fps_cap = 15
jpeg_quality = 80
h264_bitrate_kbps = 1000
h264_gop = 30
```

読込は wifi_task / rd_task が fmrb_toml API で自前パース
(FS init は display init より前なので順序問題なし)。fmrb_sys_malloc /
static バッファ使用 (CLAUDE.md 準拠)。

## プロトコル定義

### MJPEG (GET /stream)

```
Content-Type: multipart/x-mixed-replace; boundary=fmrbframe

--fmrbframe\r\n
Content-Type: image/jpeg\r\n
Content-Length: <n>\r\n
X-Seq: <seq>\r\n
\r\n
<JPEG>\r\n
```

同時 1 クライアント (atomic フラグ、超過は 503)。

### WS 入力 (/ws、バイナリ little-endian)

| type(u8) | ペイロード | 合計 |
|---|---|---|
| 0x01 mouse_move | i16 x, i16 y (仮想426x240) | 5B |
| 0x02 mouse_button | i16 x, i16 y, u8 button, u8 state(1=down) | 7B |
| 0x03 key | u8 state, u8 hid_scancode, u8 fmrb_mod_mask | 4B |
| 0x04 keyframe_req (P2) | なし | 1B |
| 0x05 ping | なし | 1B |

サーバ→クライアント (テキストJSON): 接続時 `{"t":"info","w":426,"h":240,"mode":...}`、
カーソル `{"t":"cur","x":..,"y":..,"v":1}`、統計 `{"t":"stat","fps":..,"kbps":..}`。

### 入力ブリッジ規約

- usb_task / tab5_keyboard と同一: `fmrb_host_send_key_down(scancode, scancode, fmrb_mod)`
  (key_code=scancode=HID usage ID、modifier は FMRB_KEYMAP_MOD_* マスク)
- JS 側は KeyboardEvent.code → HID usage の静的テーブル (keymap.js ~60キー)。
  `e.repeat === true` は破棄 (USBキーボードの挙動に一致させる)
- マウスは仮想 426x240 の絶対座標を送る (スロットリングは host_task 側に既存)

### H.264 WS フレーミング (Phase 2、/ws_video バイナリ)

```
[u8 type=0x01][u8 flags(bit0=keyframe)][u16 width=432][u32 pts_ms][Annex B access unit]
```

IDR には SPS/PPS を前置 (エンコーダ出力に含まれなければサーバ側でキャッシュ
して前置)。クライアント join / keyframe_req 受信で IDR を要求。

## ビューア (tool/web/remote/)

- index.html: `<img src="/stream">` (P1) / `<canvas>` (P2) を 852x480
  (2x, image-rendering: pixelated) 表示 + ステータスバー
- remote.js: WS 接続/再接続、mousemove(~30msスロットル)/mousedown/up/keydown/up、
  座標変換、カーソルオーバレイ。
  Phase2: `VideoDecoder({codec:'avc1.42e01e', optimizeForLatency:true})`、
  description なし (Annex B モード)、WebCodecs 非対応なら MJPEG に自動フォールバック

## sdkconfig.defaults.p4 追記 (計画承認済みブロック、既存行は不変更)

```
# --- WiFi STA via esp_wifi_remote (radio on ESP32-C6 over esp_hosted SDIO) ---
CONFIG_HTTPD_WS_SUPPORT=y
CONFIG_LWIP_IRAM_OPTIMIZATION=n
CONFIG_LWIP_MAX_SOCKETS=16
```

(esp_wifi_remote / SLAVE_TARGET 等は BLE 対応時に設定済み。BT/NimBLE/hosted
行には触れない)

依存追加: idf_component.yml に espressif/mdns、Phase2 で
espressif/esp_h264 ^1.1.0 (いずれも `if: "target == esp32p4"`)。
main/CMakeLists.txt P4 分岐に REQUIRES esp_wifi esp_wifi_remote esp_netif
esp_event esp_http_server esp_driver_jpeg (+esp_h264)、EMBED_FILES で
ビューア3ファイル埋込。

## 実装ステップ

Phase 1 (各ステップでビルド可能を維持):
1. sdkconfig 追記 + mdns 依存 + CMake REQUIRES
2. wifi_task + wifi.toml example + Rakefile 条件付きコピー + .gitignore +
   boot.c 拡張 → 検証A
3. display_p4 capture API
4. rd_encoder_jpeg (RGB565 バイト順の赤青反転チェックを実機手順に)
5. rd_task + rd_http → 検証B
6. rd_input + /ws + remote.js/keymap.js → 検証C

Phase 2:
1. esp_h264 追加、**PPA の YUV420 出力と O_UYY_E_VYY の一致を最初に単体検証**、
   426→432 パディング (出力バッファ事前黒クリア)
2. rd_stream + /ws_video、join 時 IDR
3. remote.js WebCodecs パス + mode 自動選択 + MJPEG フォールバック維持

## リスクと対策

| リスク | 対策 |
|---|---|
| esp_hosted 1.4.0 pre-transport RPC ヒープ破壊 | WiFi init は BLE init 後に逐次。BLE 無効時は esp_hosted_setup() でトランスポート確立を保証 |
| BLE 併用で帯域半減 | MJPEG 15fps ~2Mbps << 18Mbps。fps/quality を TOML で調整可 |
| JPEG/H.264 の色順序・レイアウト不一致 | 実機カラーバー確認を検証手順化。PPA YUV 検証を Phase2 の独立最初ステップに |
| キャプチャ copy が描画を遅延 | capture 有効時のみ・~1-2ms/フレーム。render 統計ログで回帰確認 |
| httpd 長寿命ハンドラによるワーカ枯渇 | /stream 1 クライアント制限 (503) |
| IRAM 逼迫 / Flash 残量 | LWIP_IRAM_OPTIMIZATION=n、新規コードに IRAM 属性禁止。P1 +~500KB / P2 +~300KB 見込み、各フェーズでサイズ計測 |
| セキュリティ (平文HTTP・無認証) | LAN のホビー用途として許容し明記。将来 URL トークン等を検討 |

## 検証計画

- **A (WiFi)**: `connected, ip=...` ログ、`ping fmruby.local`、BLE Web
  コンソール回帰、AP断→自動再接続
- **B (MJPEG)**: `http://<ip>/` で画面表示、デバイス側操作が ~100ms で反映、
  アイドルでも切断しない、2本目 /stream は 503、render 統計の悪化なし
- **C (入力)**: マウス移動/クリック/キーがフォーカスアプリへ、Ctrl+Q 割込、
  JP/US レイアウト主要キー、ブラウザオートリピートの二重入力なし
- **D (Phase2)**: WebCodecs で H.264 表示、途中 join ~1s 以内、実測 ~1Mbps、
  MJPEG フォールバック
