# Phase 3b 実装計画書: BLE debug GATT サービス (デバイス側)

作成日: 2026-07-21
ステータス: 未着手 (本書が実装指示)
対象読者: 本フェーズを実装する AI / 開発者。本書のみで作業着手できる粒度で書くが、
背景が必要な場合は以下を参照する。

- 全体設計: doc/remote_debug/vm_remote_debug_design.md
- 前計画 (Phase 3 全体の位置づけ): doc/remote_debug/vm_remote_debug_impl_plan2.md sec 4 (本書はその詳細化・確定版)
- プロトコル仕様 (正): doc/remote_debug/vm_remote_debug_protocol.md
- 進捗ログ: doc/remote_debug/vm_remote_debug_progress.md

## 1. ゴールと完了条件

ESP32 実機 (S3 / P4) で、リモートデバッガ debugd が BLE GATT 経由でホストと
通信できるようにする。デバイス側のみが対象。

完了条件:

1. `rake build:esp32` (S3) が成功し、パーティションサイズチェックを通過する
   (factory は 3MB に拡張済み、現在の空きは約 35%)。増分サイズを記録する。
2. P4 ビルド (NARYAv4) が成功する。注意: Rakefile は `.env` を読んで
   `FMRB_HW_TARGET` を無条件上書きするため、シェルの環境変数指定は効かない
   (エラーにならず S3 がビルドされる)。`.env` の `FMRB_HW_TARGET` を一時的に
   `NARYAv4` へ書き換えてビルドし、終わったら `NARYAv3` に戻すこと。
3. Linux 回帰: `rake clean_all` 後 `rake build:linux` が成功し、
   リポジトリルートの `fmruby-core/tool/debug/test_phase1.sh` / `test_phase2.sh` が
   PASS する (TCP パスの無退行確認)。
4. ESP32 で `fmrb_debugd_init()` が起動し、BLE 未接続でも既存機能
   (アプリ起動・BLE ファイルサービス) が退行しないこと (ビルド+Linux回帰で
   確認できる範囲まで。実機での BLE E2E は Phase 3c でユーザが行う)。
5. ドキュメント更新 (sec 9) とコミット (数行の英文コミットログ)。

## 2. スコープ外 (やらないこと)

- ホスト側 (Python bleak バックエンド、VSCode 拡張の extensionKind 変更) → Phase 3c
- P4 実機での実測・調整 → Phase 3d
- evaluate / 変数展開 / log_stream push / DEBUGGING 状態 → Phase 4
- 実機での動作確認 (ユーザ作業)

## 3. プロジェクト規約 (fmruby-core/CLAUDE.md より、特に本作業に効くもの)

- ソースコードのコメントは英語。ASCII 外の文字・絵文字は使わない。
- main/ 以下の関数戻り値は `fmrb_err.h` (`fmrb_err_t`) を標準とする。
- ログは `fmrb_log.h` のラッパー (`FMRB_LOGI` 等) を使う。`esp_log` 直接使用禁止。
- malloc 直接使用禁止。今回は静的バッファ (PSRAM `EXT_RAM_BSS_ATTR`) で足りる。
- sdkconfig / sdkconfig.defaults は編集禁止 (NimBLE は両ターゲットで有効済み)。
- .gitsubmodule 配下 (components/picoruby-esp32/picoruby 等) は編集禁止。
  今回 submodule に触る必要はない。
- 編集した箇所の Legacy コードは残さず消す (旧実装のコメントアウト放置禁止)。
- ファイルをビルド対象から外して問題回避することは禁止。

## 4. 現状の構造 (2026-07-21 時点の facts)

行番号は目安 (ドリフトし得る)。シンボル名で探すこと。

### 4.1 debugd コア (main/drivers/debug/)

- `fmrb_debug_transport.h`: トランスポート vtable
  `fmrb_debug_transport_ops_t { init, poll, send, connected, close_client }`。
  poll は「フレーミング除去済みの msgpack body を 1 個返す」契約
  (>0 = body 長 / 0 = タイムアウト / <0 = トランスポートエラー)。
- `fmrb_debugd.c`: 単一タスク `debugd_main`。
  - トランスポート選択は先頭付近の `s_tp`:
    `#ifdef CONFIG_IDF_TARGET_LINUX -> &fmrb_debug_transport_tcp / #else -> NULL`。
  - ループ: `s_tp->poll(s_rx_body, sizeof, 50ms)` -> dispatch -> `forward_events()`
    -> `connected()` の立ち下がりで `fmrb_debug_ctx_detach_all()`。
    この既存ロジックは BLE でもそのまま機能する (切断 = 全 detach で安全側)。
  - `s_rx_body[FMRB_DEBUG_MAX_FRAME]` は `FMRB_DBG_BSS_ATTR` (ESP32 では
    `EXT_RAM_BSS_ATTR` = PSRAM) 済み。
- `fmrb_debug_proto.h`: `FMRB_DEBUG_MAX_FRAME = 4096` (msgpack body 最大長)。
- `fmrb_debug_transport_tcp.c`: TCP 実装 (linux 専用 TU)。フレーミングは
  u32 BE 長プレフィクス。BLE 実装の対比参考に。
- `main/boot/boot.c`: `fmrb_debugd_init()` 呼び出しが
  `#ifdef CONFIG_IDF_TARGET_LINUX` ガード内 (boot 後半、
  "Family mruby OS initialization complete" ログの直後)。

### 4.2 BLE (main/drivers/ble/ble_task.c、ESP32 系専用 TU、約 1400 行)

ファイル転送サービス (fs) が実装済みで、debug サービスはこれと同型に作る。

- フレーミング: `COBS([payload][CRC32 4B BE]) + 0x00 デリミタ`。
  CRC は COBS 前の平文に対して計算。
  `crc32_calc` / `cobs_encode` / `cobs_decode` は static 関数
  (crc32_table 256 エントリの const 表も static)。
- UUID: ベース `xxxxxxxx-4252-5942-4c45-...` ("FMARBYBLE")、byte[0] で区別。
  0x01 service / 0x02 device info / 0x03 fs RX (write) / 0x04 fs TX (notify)。
  `gatt_svr_svc_fmrb_uuid` 等、`BLE_UUID128_INIT` はリトルエンディアン記述なので
  配列の**先頭バイト**が byte[0]。
- サービス表: `gatt_svr_svcs[]`。1 サービス + 3 characteristics。
  ここに追記すれば登録処理 (`ble_gatts_count_cfg/add_svcs`、`ble_task_init` 内) は
  変更不要。
- 受信: `gatt_chr_fs_rx_cb` (NimBLE ホストタスク上で実行) が
  `g_fs_ctx.rx_buffer` (4KB) に蓄積し、0x00 検出で `frame_ready = true` +
  セマフォ give。重い処理 (COBS デコード・コマンド実行) は専用タスク
  `ble_fs_task_func` 側。**この役割分担 (access_cb を軽くする) を踏襲する。**
- 送信: `ble_fs_send_notify(data, len)` が MTU-3 で分割 notify。
  チャンク間 5ms delay、失敗時 20ms 後 1 リトライ。`g_fs_tx_val_handle` 直参照。
- 接続状態: `g_connected` / `g_tx_subscribed` (fs TX 用)。
  `BLE_GAP_EVENT_SUBSCRIBE` で `attr_handle == g_fs_tx_val_handle` を判定。
  `BLE_GAP_EVENT_DISCONNECT` で fs 受信状態をリセットし re-advertise。
- 初期化: `ble_task_init()` (boot.c から S3/P4 それぞれの経路で呼ばれる)。
  P4 は `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE` 分岐で C6 コンパニオンへの
  vHCI を使うが、GATT 層のコードは共通。
  P4 では radio init が遅延タスクで走るため、**debugd は BLE init の完了に
  依存してはならない** (後述 6.4 の設計で担保)。
- デバイス名: `ble_on_sync` が `Family-mruby-XXXXXX` (MAC 下位) に設定する。

### 4.3 ビルド登録 (main/CMakeLists.txt)

- `COMPONENT_SRCS`: debug コア 3 ファイル (proto/ctx/debugd) 登録済み (全ターゲット)。
- `COMPONENT_LINUX_SRCS`: `fmrb_debug_transport_tcp.c`。
- `COMPONENT_ESP32_SRCS`: `drivers/ble/ble_task.c` ほか。ここに新規 TU を足す。
- `msgpack-esp32` は全ターゲットで REQUIRES 済み。

## 5. ワイヤフォーマット仕様 (BLE debug サービス)

ホスト側 (Phase 3c) との契約になるため、この仕様どおりに実装し、
完了時に doc/remote_debug/vm_remote_debug_protocol.md へ転記する。

```
plain   = [len_hi][len_lo][msgpack body]          // len = body バイト長 (u16 BE)
frame   = COBS( plain + CRC32(plain) ) + 0x00     // CRC32 は 4B BE、fs 方式踏襲
```

- msgpack body はトランスポート非依存 (TCP と完全に同一。protocol.md の仕様)。
- 受信側検証: COBS デコード -> 末尾 4B の CRC32 検証 -> len と
  `decoded_len - 6` の一致検証。いずれか不一致なら破棄して警告ログ。
- body 最大長は `FMRB_DEBUG_MAX_FRAME` (4096)。
  エンコード後最大長 = 4102 + COBS オーバーヘッド (ceil(4102/254) = 17) + 1
  = 4120 バイト。バッファはこれを収める定数を定義する
  (例: `#define BLE_DBG_MAX_ENC (FMRB_DEBUG_MAX_FRAME + 2 + 4 + ((FMRB_DEBUG_MAX_FRAME + 6) / 254) + 2)`)。

UUID (byte[0]、既存の並びに追加):

- 0x05: debug service (primary)
- 0x06: debug RX characteristic (host -> device, WRITE | WRITE_NO_RSP)
- 0x07: debug TX characteristic (device -> host, NOTIFY)

## 6. 実装項目

依存順。各項目ごとにビルドが通る状態を保つこと。

### 6.1 フレーミング共通化: main/drivers/ble/ble_framing.{h,c}

- `ble_task.c` の `crc32_calc` / `cobs_encode` / `cobs_decode` / `crc32_table` を
  新規 TU `ble_framing.c` へ**移動**し (コピーを残さない)、`ble_framing.h` で公開する
  (プレフィクスは `ble_framing_` に改名: `ble_framing_crc32` /
  `ble_framing_cobs_encode` / `ble_framing_cobs_decode`)。
- `ble_task.c` 内の全呼び出し箇所を新名称に置換。
- `COMPONENT_ESP32_SRCS` に `drivers/ble/ble_framing.c` を追加。
- ヘッダは Doxygen コメント対象 (他モジュールから参照されるため)。

### 6.2 debug GATT サービス追加 (ble_task.c への追記)

1. UUID 3 本追加 (sec 5)。既存 UUID 定義群の直後に同型で。
2. グローバル追加 (fs の並びに合わせる):
   `static uint16_t g_dbg_tx_val_handle;` /
   `static bool g_dbg_tx_subscribed;`
3. 受信コンテキスト (fs の `ble_fs_context_t` とは独立の軽量版):

   ```c
   // Debug service RX: ping-pong raw-frame buffers handed to debugd via queues.
   #define BLE_DBG_RX_BUF_COUNT 2
   typedef struct {
       uint8_t  buf[BLE_DBG_RX_BUF_COUNT][BLE_DBG_MAX_ENC];
       uint16_t fill_len;      // bytes accumulated into buf[fill_idx]
       int8_t   fill_idx;      // buffer currently being filled, -1 = none held
   } ble_dbg_rx_t;
   EXT_RAM_BSS_ATTR static ble_dbg_rx_t g_dbg_rx;
   ```

4. access_cb `gatt_chr_dbg_rx_cb` (NimBLE ホストタスク上、軽く保つ):
   - 未登録 (6.3 の queue 未設定) なら受信データを捨てて 0 を返す。
   - `fill_idx < 0` なら free キューから buffer index を非ブロッキング取得。
     取れなければフレームを捨てる (警告ログ、レート制限は不要)。
   - mbuf からコピーして蓄積。バッファ溢れはリセットして
     `BLE_ATT_ERR_INSUFFICIENT_RES`。
   - 0x00 デリミタ検出で `{idx, len}` を ready キューへ送信 (非ブロッキング。
     失敗したら index を free キューへ返却してフレーム破棄)、`fill_idx = -1`。
   - **COBS デコード・CRC 検証はここでやらない** (debugd 側 poll でやる)。
   - デリミタ以降に同一 write で続きのデータが来るケースは、fs 実装同様
     「デリミタまでで打ち切り」でよい (ホストはフレーム単位で write する)。
5. TX 側 access_cb は fs 同様の空実装。
6. `gatt_svr_svcs[]` に debug service エントリ追加 (RX: WRITE | WRITE_NO_RSP、
   TX: NOTIFY + `.val_handle = &g_dbg_tx_val_handle`)。
7. `BLE_GAP_EVENT_SUBSCRIBE` に `g_dbg_tx_val_handle` の分岐追加 ->
   `g_dbg_tx_subscribed` 更新。
8. `BLE_GAP_EVENT_DISCONNECT` に debug 受信状態リセットを追加:
   保持中バッファ (`fill_idx >= 0`) を free キューへ返却し `fill_idx = -1`、
   `fill_len = 0`、`g_dbg_tx_subscribed = false`。

### 6.3 ble_task <-> transport の受け渡し API (main/drivers/ble/ble_debug_link.h)

新規小ヘッダ。実装は ble_task.c 内。トランスポート TU からのみ使う。

```c
// Registered by the BLE debug transport. Queues are owned by the transport;
// ble_task only holds the handles. Both queues carry ble_dbg_frame_ref_t.
typedef struct { uint8_t idx; uint16_t len; } ble_dbg_frame_ref_t;
fmrb_err_t ble_debug_register(fmrb_queue_t free_q, fmrb_queue_t ready_q);
// Encoded frame (COBS + delimiter already applied) -> notify on debug TX.
fmrb_err_t ble_debug_send(const uint8_t *encoded, size_t len);
// g_connected && g_dbg_tx_subscribed.
bool ble_debug_link_ready(void);
// Raw pointer to RX buffer idx (for the transport to decode from).
const uint8_t *ble_debug_rx_buf(uint8_t idx);
```

- `ble_debug_send` は既存 `ble_fs_send_notify` を val_handle と購読フラグを
  引数に取る共通関数 `ble_send_notify(val_handle, subscribed, data, len)` に
  一般化して両サービスで共用する (fs 側呼び出しも置換、Legacy は残さない)。
- 登録前 (`ble_debug_register` 未呼び出し) の受信は 6.2-4 のとおり捨てる。
  boot 順序上、S3 では ble_task_init -> (後で) debugd init となるため、
  その間に接続してきたホストのフレームは失われるが、ホスト側のリトライで
  回復する設計とする (仕様として protocol.md に明記)。

キュー運用 (所有はトランスポート):

- free キュー (深さ 2): 初期状態で idx 0,1 を投入。access_cb が取得。
- ready キュー (深さ 2): access_cb が完成フレームを投入、transport poll が取得。
- poll がデコード完了後に idx を free キューへ返却する。
  この一方向循環により、NimBLE タスクと debugd タスク間で同一バッファを
  同時に触ることはない (キューがメモリバリアを兼ねる)。

### 6.4 BLE トランスポート実装 (main/drivers/debug/fmrb_debug_transport_ble.c)

ESP32 系専用 TU。`fmrb_debug_transport_ops_t` を実装し
`extern const fmrb_debug_transport_ops_t fmrb_debug_transport_ble;` を
`fmrb_debug_transport.h` に追加 (TCP の宣言の並び)。

- `init`: free/ready キューを作成し `ble_debug_register()` を呼ぶ。
  **BLE 初期化状態に依存せず FMRB_OK を返す** (P4 では radio init が
  遅延するため。ble_task 側は登録ハンドルを保持するだけなので順序自由)。
- `poll(buf, cap, timeout_ms)`: ready キューを timeout 付き receive。
  取得したら `ble_debug_rx_buf(idx)` から COBS デコード -> CRC 検証 ->
  len 検証 -> body を `buf` へコピーし body 長を返す。idx は必ず free キューへ
  返却 (検証失敗時も)。検証失敗は警告ログ + 0 を返す (フレーム破棄)。
  デコード用中間バッファは static PSRAM (`EXT_RAM_BSS_ATTR`) 1 面 (4KB+α)。
  poll は debugd タスクからしか呼ばれない (排他不要)。
- `send(body, len)`: sec 5 のフォーマットで plain 構築 -> CRC -> COBS ->
  デリミタ付与 -> `ble_debug_send()`。作業バッファ 2 面 (plain / encoded、
  static PSRAM)。send も debugd タスク専用。
  `ble_debug_link_ready()` が false なら送らず FMRB_ERR_INVALID_STATE
  (debugd は送信失敗を致命扱いしないので戻り値だけ正しく返す)。
- `connected`: `ble_debug_link_ready()` をそのまま返す。
  切断 -> `connected() == false` -> debugd 既存ロジックが `detach_all` する。
- `close_client`: no-op に近いが、ready キューを drain して free へ戻し、
  受信の食い残しを捨てる。

### 6.5 配線 (debugd / boot / CMake)

1. `fmrb_debugd.c` の `s_tp` 選択を変更:

   ```c
   #ifdef CONFIG_IDF_TARGET_LINUX
   static const fmrb_debug_transport_ops_t *s_tp = &fmrb_debug_transport_tcp;
   #else
   static const fmrb_debug_transport_ops_t *s_tp = &fmrb_debug_transport_ble;
   #endif
   ```

   NULL 分岐が不要になるため `debugd_main` 冒頭の `if (!s_tp)` ガードと
   関連コメントは削除する (Legacy を残さない)。
2. `main/boot/boot.c`: `fmrb_debugd_init()` の `#ifdef CONFIG_IDF_TARGET_LINUX`
   ガードを外し全ターゲットで呼ぶ。コメントも現状に合わせ更新
   (Linux = TCP:5555、ESP32 = BLE debug service)。
3. `main/CMakeLists.txt`: `COMPONENT_ESP32_SRCS` に
   `drivers/ble/ble_framing.c` と `drivers/debug/fmrb_debug_transport_ble.c` を追加。

### 6.6 実装しないことの明確化

- 処理タスクの新設はしない (debugd タスクが既にある)。
- fs サービスのプロトコル (JSON) には触れない。debug は msgpack。
- NimBLE の設定変更 (MTU、接続パラメータ) はしない。

## 7. メモリ・並行性の要件まとめ

- 新規静的バッファはすべて PSRAM (`EXT_RAM_BSS_ATTR`)。概算:
  RX ping-pong 2 x 4.2KB + poll デコード 1 面 + send 2 面 = 約 21KB。
  内部 RAM には置かない。
- NimBLE ホストタスク (access_cb) と debugd タスクの共有データは
  キュー経由のみ。フラグ (`g_dbg_tx_subscribed` 等) は既存 fs と同じ
  素朴な bool でよい (最悪 1 フレーム落ちるだけで、正しさに影響しない)。
- `ble_gatts_notify_custom` は fs (ble_fs タスク) と debug (debugd タスク) から
  並行に呼ばれ得るが、NimBLE API はスレッドセーフであり mbuf も個別なので
  排他は不要。コメントで明記しておく。
- debugd のパーク方式・detach 時の扱い (タイムアウトでスロット放置) は
  既存実装のままで、本フェーズでは触らない。

## 8. 検証手順

作業ディレクトリは fmruby-core。ビルドは rake が docker を使う。

1. 各実装項目後: `rake build:esp32` (S3) でコンパイル確認。
2. 完了時:
   - S3: `rake build:esp32` -> check_sizes の出力 (バイナリサイズと空き) を記録。
   - P4: `.env` の `FMRB_HW_TARGET` を `NARYAv4` に書き換えて
     `rake clean_all && rake build:esp32`、確認後 `.env` を `NARYAv3` へ復元
     (シェルの環境変数指定は `.env` に上書きされて効かない。sec 1 参照)。
   - Linux 回帰: `rake clean_all && rake build:linux`、
     fmruby-graphics-audio 側は既ビルドがあればそのまま、無ければ
     `rake build:linux` (同名タスク)。その後リポジトリルート
     (family-mruby) で `fmruby-core/tool/debug/test_phase1.sh` と
     `test_phase2.sh` を実行し PASS を確認。
   - ターゲット切替のたびに `rake clean_all` を忘れない (ABI 混在防止)。
3. ビルドのみで確認できない項目 (BLE 実疎通、パーク中の挙動) は Phase 3c の
   実機検証へ引き継ぐ。本フェーズでは「未接続時に何も壊さない」ことが
   Linux 回帰で担保されていればよい。

## 9. ドキュメント更新とコミット

1. `doc/remote_debug/vm_remote_debug_protocol.md`: BLE フレーミング節を追加 (sec 5 の内容、
   UUID 表、登録前フレーム喪失とホストリトライの約束)。
2. `doc/remote_debug/vm_remote_debug_progress.md`: Phase 3b 実施記録 (日付、サイズ実測、
   設計判断で本書から逸脱した点があればその理由)。
3. `doc/remote_debug/vm_remote_debug_impl_plan2.md`: sec 4 の冒頭にステータス行を追記
   (「実装済み、詳細は impl_plan_phase3b.md」)。
4. コミット: 論理単位で分割 (目安: 6.1 framing 切り出し / 6.2-6.3 GATT サービス /
   6.4-6.5 トランスポート+配線+ドキュメント)。コミットログは数行の英文。
   git 操作はユーザの指示を得てから行うこと。

## 10. リスク・注意

| 項目 | 内容 | 対応 |
|---|---|---|
| ble_task.c の改変退行 | fs サービスは実運用中 (ファイル転送/ログ購読) | framing 切り出しと send_notify 一般化は機械的な置換に留め、フレーム形式・遅延値 (5ms/20ms) を変えない |
| P4 の boot 順序 | radio init 遅延中に debugd が先に走る | transport init を BLE 非依存にする (6.4)。ble_debug_register は単なるポインタ保存 |
| 登録前フレーム喪失 | S3 で ble_task_init と debugd init の間に接続された場合 | 仕様として許容しホストのリトライで回復 (protocol.md に明記) |
| notify の並行送信 | fs と debug が同時に notify | NimBLE はスレッドセーフ。コメント明記のみ |
| RX バッファ枯渇 | ホストがパイプライン送信し 2 面を超える | フレーム破棄 + 警告ログ。リクエスト/レスポンス型プロトコルなので実際にはほぼ起きない |
| サイズ増 | 数十 KB 見込み | factory 3MB 化済みで問題なし。実測値を progress.md に記録 |
