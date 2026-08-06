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
    - ~~esp_h264 の HWエンコーダは RGB565_LE を直接サポート~~ →
      **実機で否定 (2026-07-08)**。RGB565_LE 直接入力は chip rev v3.0
      以降のみで、Tab5 (rev v1.0) は O_UYY_E_VYY のみ。当初設計どおり
      PPA 色変換を実装。詳細は
      「H.264 HW 入力フォーマットとチップリビジョン」参照
    - IDR フレームには SPS/PPS が自動付与される (esp_h264 仕様) →
      サーバ側での SPS/PPS キャッシュ/前置は不要
    - IDR オンデマンドの直接APIは無い → `esp_h264_enc_set_gop(param, 1)`
      で1フレームだけGOP=1にして復元するトリックで実装 (要実機検証。
      効かない場合も GOP=30 で最悪2秒待ちで復帰)
    - display_p4_capture_enable は参照カウント化 (MJPEG と H.264 が併用
      するため)
  - バイナリ: 3.10MB / 4MB (26% free)。Phase1: 3.06MB
- **実機検証: A〜D 全て完了 (2026-07-08)**
  - 検証A (WiFi): OK。指数バックオフ再接続も動作確認 (connect failed 2回後に接続成功)
  - 検証B (MJPEG): OK。画面表示 + async 化後のカーソルオーバレイ/入力も確認
  - 検証C (入力): OK。マウス/キーがリアルタイムに実機反映
  - 検証D (H.264): OK。PPA YUV420 変換で映像・入力とも動作。
    体感遅延も定常レート送出 + prefer-software で解消 (下記4)。
    検証過程で判明した問題と対処:
    1. ブラウザが MJPEG にフォールバック → 下記「WebCodecs と Secure Context」
    2. Secure Context 解決後、`esp_h264_enc_hw_new` が RGB565_LE を拒否
       (`Un-supported h264 picture type parameter, pic_type: 4c424752`="RGBL")
       → 下記「H.264 HW 入力フォーマットとチップリビジョン」。PPA 変換で修正済
       (ビルドOK・実機確認待ち)
    3. MJPEG 配信中にカーソルオーバレイが表示されない (WS入力も同時に
       飢餓) → 下記「MJPEG ハンドラによる httpd タスク占有」。async 化で
       修正済 (ビルドOK・実機確認待ち)
    4. H.264 映像の体感遅延 ~3秒 (カーソル/入力はリアルタイム)。
       原因: 描画がイベント駆動でアイドル時 1-2fps しか流れず、
       ブラウザの H.264 デコーダ (特にHW) は数フレームをパイプラインに
       抱えて次の入力が来るまで出力しない → 低fpsストリームでは
       パイプライン滞留分が秒単位の遅延になる。
       対処: (a) rd_stream は静止中も frame_interval 毎に最新フレームを
       再エンコードして **fps_cap の定常レートで送出** (無変化Pフレームは
       ほぼスキップMBで数百バイト)。(b) remote.js は
       `hardwareAcceleration: 'prefer-software'` を isConfigSupported
       ガード付きで優先 (この解像度ではSWデコードが軽く、リオーダ遅延なし)
       → **実機で遅延解消を確認済み**

### MJPEG ハンドラによる httpd タスク占有 (実機検証で判明)

esp_http_server は**シングルタスク**であり、/stream ハンドラ内で
capture→encode→send を無限ループする Phase 1 設計では、配信中は同じ
httpd タスクで処理されるものが全て止まる:
- `httpd_queue_work` によるカーソル "cur" JSON push → **カーソル不可視**
- /ws の受信フレーム処理 → **リモート入力が配信中は効かない**
- /status 応答、新規接続の accept

リスク表の「httpd 長寿命ハンドラによるワーカ枯渇」を 1 クライアント制限
(503) で対策したつもりだったが、ワーカプールではなく単一タスクなので
制限では防げない。

対処 (rd_http.c): `httpd_req_async_handler_begin()` でリクエストを
デタッチし、専用タスク rd_mjpeg (core0, prio4, stack 8192) が配信ループを
実行。ハンドラは即 return し httpd タスクは解放される。終了時に
`httpd_req_async_handler_complete()`。rd_http_stop は配信タスクの完了を
待ってから httpd_stop する。

### H.264 HW 入力フォーマットとチップリビジョン (実機検証で判明)

esp_h264 (v1.3.6) の対応表は HW encoder = RGB565_LE 対応と書いてあるが、
`ESP_H264_HW_IS_SUPPORTED_PIC_TYPE` (esp_h264_types.h) は
**`CONFIG_ESP_REV_MIN_FULL < 300` (チップ rev < v3.0) では
O_UYY_E_VYY のみ許可**するリビジョン分岐になっている。
Tab5 の P4 は chip rev v1.0 のため **RGB565 直接入力は使えず、
YUV420 (O_UYY_E_VYY) 変換が必須** (Phase 2 設計当初の想定が正しかった。
「RGB565_LE 直接サポート」は rev v3.0 以降のみの話)。

対処 (rd_encoder_h264.c 実装):
- PPA SRM で RGB565 426x240 → YUV420 432x240 に HWカラーコンバート
  (`PPA_SRM_COLOR_MODE_YUV420` 出力、BT.601 limited range、426→432
  パディングは出力 pic_w=432 の (0,0) にブロック書込 + 右6px は
  事前黒クリア Y=16/UV=128)
- P4 の PPA/2D-DMA の YUV420 packed レイアウトが esp_h264 の
  O_UYY_E_VYY (奇数行 UYY.../偶数行 VYY...) と一致する前提。
  **色が破綻していないかは実機検証項目**
- 併せてエンコーダ初期化失敗時の後始末を修正:
  rd_stream はクライアント解放 + ソケットclose + rd_http_disable_h264()
  (以後の info は h264:false)。remote.js はデータ無しclose 3回連続で
  MJPEG にフォールバック (従来は黒画面のまま2秒毎に無限リトライ)

### WebCodecs と Secure Context (実機検証で判明した制約)

**WebCodecs API (`VideoDecoder`) は Secure Context 限定**。
`http://192.168.10.15/` や `http://fmruby.local/` のような平文 HTTP の
LAN オリジンでは Chrome でも `window.VideoDecoder` が undefined になり、
remote.js の判定 (`useH264 = !!msg.h264 && typeof window.VideoDecoder === 'function'`)
が false → 常に MJPEG フォールバックになる。設計時のリスク表 (平文HTTP) では
見落としていた点。

確認方法:
- ビューアのステータスバー表示が `mode: mjpeg` / `mode: h264` のどちらか
- DevTools コンソールで `typeof VideoDecoder` → `"undefined"` なら Secure
  Context 外
- デバイス側ログ: MJPEG なら `rd_http: MJPEG client connected`、H.264 なら
  /ws_video 接続 (rd_stream クライアント登録) のログが出る

H.264 パスを使うための手順 (いずれか):

1. **Chrome フラグで origin を信頼させる (開発用に推奨)**
   - `chrome://flags/#unsafely-treat-insecure-origin-as-secure` を開く
   - テキスト欄に `http://192.168.10.15,http://fmruby.local` を入力し
     Enabled に設定 → Chrome 再起動
2. **localhost 経由でアクセスする** (`localhost` は Secure Context 扱い)
   - PC 側でポートフォワードして `http://localhost:8080/` を開く:
     ```
     ssh -L 8080:192.168.10.15:80 localhost -N
     # または
     socat TCP-LISTEN:8080,fork TCP:192.168.10.15:80
     ```
   - 注意: WS も同一オリジン (`ws://localhost:8080/...`) 経由になるため
     追加設定は不要
3. **デバイス側 HTTPS 化** (esp_https_server + 自己署名証明書):
   TLS のメモリ/CPU 負荷と証明書警告があり、ホビー用途では非推奨。採らない

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
config/wifi.toml.example  # テンプレート (実物 config/wifi.toml は
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
| httpd (esp_http_server内部) | 0 | 5 | 8192 | hosted/lwip と同じ core0。長寿命ハンドラ禁止 (シングルタスク) |
| rd_mjpeg (MJPEGクライアント接続中のみ) | 0 | 4 | 8192 | async handler でデタッチした /stream 配信ループ。当初の「httpdハンドラ内でループ」はWS入力/カーソルpushを飢餓させたため変更 |
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

- index.html: `<img src="/stream">` (P1) / `<canvas>` (P2) を窓表示 852x480
  (2x, image-rendering: pixelated) + ステータスバー。全画面切替あり (下記)
- remote.js: WS 接続/再接続、mousemove(~30msスロットル)/mousedown/up/keydown/up、
  座標変換、カーソルオーバレイ。
  Phase2: `VideoDecoder({codec:'avc1.42e01e', optimizeForLatency:true})`、
  description なし (Annex B モード)、WebCodecs 非対応なら MJPEG に自動フォールバック

### 全画面表示 (配信側は無変更、ビューアのみ)

- DOM は外枠 `#wrap` (全画面時は画面全体・黒の余白担当) と内枠 `#view`
  (見せる 426x240 の領域そのもの) の二段。表示倍率 `scale` から view/映像/
  カーソルの大きさを全て導出するので、窓表示 (2 倍固定) と全画面は同じ
  コードを通る
- 全画面の倍率は `min(innerW/426, innerH/240)`。**縦横比を保ったまま最大化**
  する。426x240 = 1.775 は 16:9 (1.778) とほぼ同じなので 1920x1080 では
  1917x1080 (左右 1.5px の余白) で実質全画面になる
- 倍率が整数のときだけ `image-rendering: pixelated`、端数のときは補間
  (`auto`)。端数倍で最近傍にすると画素の大きさが不揃いになるため。
  1704x1200 の画面なら 4 倍ちょうどになり自動的に pixelated
- 座標の逆変換は `#view` の実寸 (`getBoundingClientRect`) 基準。`#wrap` は
  全画面時に余白まで含むので原点がずれる
- 切替は状態バーのボタンと `Ctrl+Alt+F` (F11 はブラウザが横取りするため
  独自に用意)。局所処理したキーは keyup も握り潰す (機器側に対応する
  keydown の無い keyup を送らないため)
- **Esc の扱い**: 全画面中の Esc はブラウザの解除に使われるので
  `navigator.keyboard.lock(['Escape'])` で機器側へ回す。この場合の解除は
  Esc 長押し。Keyboard Lock も**安全なオリジン限定**で、H.264 経路が既に
  要求する条件と同じ (「WebCodecs と Secure Context」参照)。ロックに失敗
  する環境では Esc は従来どおり解除に使われる
- 配信解像度は 426x240 のままなので全画面は拡大表示。粗が気になる場合は
  `h264_bitrate_kbps` を上げる (既定 1000)

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
| httpd 長寿命ハンドラによるワーカ枯渇 | /stream 1 クライアント制限 (503)。**実機で発覚: esp_http_server は単一タスクのため制限では不十分** → async handler + rd_mjpeg タスク化で解消 |
| IRAM 逼迫 / Flash 残量 | LWIP_IRAM_OPTIMIZATION=n、新規コードに IRAM 属性禁止。P1 +~500KB / P2 +~300KB 見込み、各フェーズでサイズ計測 |
| セキュリティ (平文HTTP・無認証) | LAN のホビー用途として許容し明記。将来 URL トークン等を検討 |
| 平文HTTPでは WebCodecs が使えず H.264 表示不可 | 「WebCodecs と Secure Context」の手順 (Chromeフラグ or localhostフォワード) で回避。実機検証で判明 |

## 検証計画

- **A (WiFi)**: `connected, ip=...` ログ、`ping fmruby.local`、BLE Web
  コンソール回帰、AP断→自動再接続
- **B (MJPEG)**: `http://<ip>/` で画面表示、デバイス側操作が ~100ms で反映、
  アイドルでも切断しない、2本目 /stream は 503、render 統計の悪化なし
- **C (入力)**: マウス移動/クリック/キーがフォーカスアプリへ、Ctrl+Q 割込、
  JP/US レイアウト主要キー、ブラウザオートリピートの二重入力なし
- **D (Phase2)**: WebCodecs で H.264 表示、途中 join ~1s 以内、実測 ~1Mbps、
  MJPEG フォールバック
