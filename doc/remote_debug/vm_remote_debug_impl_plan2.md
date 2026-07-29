# PicoRuby VM リモートデバッグ 実装計画書 その2 (ESP32 実機 + BLE)

作成日: 2026-07-17
更新日: 2026-07-19
ステータス: Phase 2 クローズ完了、Phase 3a 完了 (実機起動確認・性能計測はユーザ作業)。
  次の実装対象は Phase 3b (sec 4)。
前提ドキュメント:
- 設計: doc/remote_debug/vm_remote_debug_design.md
- 前計画 (Phase 0-2, 完了): doc/remote_debug/vm_remote_debug_impl_plan.md
- 進捗ログ: doc/remote_debug/vm_remote_debug_progress.md
- プロトコル仕様: doc/remote_debug/vm_remote_debug_protocol.md

本書は Phase 0-2 (Linux sim + TCP + DAP/VSCode) 完了を受けて、残作業と
Phase 3 (ESP32 実機 + BLE トランスポート)、Phase 4 (仕上げ) を実装可能な
粒度まで詳細化したもの。

## 1. 現状サマリ

完了済み (feature/vm-remote-debug ブランチ):

- デバッグコア (`main/drivers/debug/`): hook/パーク方式、BP/step/pause、
  stack_trace/frame_vars、msgpack プロトコル、トランスポート vtable、TCP 実装。
- ホスト側 (`tool/debug/`): テストクライアント、DAP アダプタ、自律テスト
  (test_phase1/2 PASS)。VSCode 拡張 (family-mruby/vscode-fmrb-debug)。
- ビルド: Linux 限定で `MRB_USE_DEBUG_HOOK` 有効 (build_config + abi_defines 同期済)、
  mrbc `-g`、combined.map.json 生成。

残作業と本書のスコープ:

1. Phase 2 クローズ: レビュー指摘修正、VSCode GUI 手動確認 (ユーザ)、ドキュメント清書
2. Phase 3a: ESP32-S3 実機でデバッグコアを動かす (トランスポートはまだ TCP 不可なので
   ビルド+フック検証まで) + 計測
3. Phase 3b: BLE debug GATT サービス (デバイス側)
4. Phase 3c: ホスト側 BLE バックエンド (bleak) + VSCode 接続
5. Phase 3d: P4 (esp-hosted vHCI) 対応と安定化
6. Phase 4: evaluate、変数展開、log_stream push、DEBUGGING 状態、パッケージング

## 2. Phase 2 クローズ (小粒・先行実施)

### 2.1 レビュー指摘の修正 (fmruby-core)

状況 (2026-07-19): 1-3 すべて実施済み。3 は 500ms 超過時に警告ログを出して
`FMRB_ERR_TIMEOUT` を返し、スロットを in_use のまま残すようにした
(次回の disconnect -> detach_all で回収される)。

1. `fmrb_debug_ctx.c` `fmrb_debug_ctx_bp_set()`: 書き込み順序を
   file -> line -> id -> (最後に) enabled にする。hook が走行中 VM 上で並行に
   `bps[]` を読むため、enabled を最後に立てて部分書き込みの観測を防ぐ。
2. `fmrb_debug_transport_tcp.c` `tcp_send()`: EAGAIN 時の `continue` ビジーループを
   「書き込み側 select (タイムアウト 1s) -> 再送、タイムアウトで drop_client」に変更。
3. `fmrb_debug_ctx.c` `fmrb_debug_ctx_detach()`: 500ms 待っても `stopped` が
   解けない場合、ctx_free (キュー削除) をせず警告ログを出してスロットを
   in_use のまま放置する (リーク容認)。ブロック中タスクのキューを消す
   未定義動作を避ける。armed=false は設定済みなので VM が起きれば自然復帰する。

### 2.2 その他

状況 (2026-07-19): ルート側コミット整理は完了 (989ac78 拡張 VSIX 対応 / 068ff72
fmrb_input 記号対応)。VSCode 拡張は VSIX インストール運用に移行し、履歴からの
再起動 (onDebugDynamicConfigurations) と VSIX 配置でのツールパス解決に対応済み。
VSCode GUI 確認 (ユーザ) も完了。残るのは design.md のステータス更新のみ。

- family-mruby ルートの未コミット分 (fmrb_input.py 記号キー対応、.gitignore、
  package.json の extensionKind) をコミットする。extensionKind は
  `["workspace", "ui"]` の順だと Remote-WSL 時に WSL 側で動く (TCP 開発には都合が
  よいが BLE では Windows 側必須)。**Phase 3c で "ui" 優先に戻す**前提のコメントを
  package.json 近く (README) に残す。
- VSCode GUI 確認 (ユーザ作業): vscode-fmrb-debug/README.md の手順で
  BP/停止線/変数/ステップ/disconnect、combined アプリ (system_desktop) の原本 BP。
- design.md のステータスを「実装反映済み」に更新し、実装と差異が出た点
  (g_tick_manager アクセサ不採用、DEBUGGING 状態見送り) を追記。

## 3. Phase 3a: ESP32-S3 でデバッグコアを有効化

目的: BLE を書き始める前に、実機で hook + デバッグコアがビルド・動作し、
性能とサイズが許容範囲であることを確認する。

状況 (2026-07-19): 3.1-3.4 のコード変更完了。Linux 回帰 (test_phase1/2) PASS、
S3 (NARYAv3) / P4 (NARYAv4) ビルド成功。実機起動確認と hook オーバーヘッド実測は
ユーザ作業として残っている。

作業中に判明した副次的な問題と対応:

- **msgpack のエンディアン判定が ESP32 で破綻していた**。
  `components/msgpack-esp32/CMakeLists.txt` の `MSGPACK_ENDIAN_*` が `PRIVATE`
  だったため利用側には効かず、`msgpack/predef` の自動判定に落ちていた。
  この判定は Xtensa/RISC-V を知らず、先行 include が `__BYTE_ORDER` を
  定義しているかどうかで結果が変わる (display_p4_task.cpp はたまたま通っていた)。
  `PUBLIC` にしてライブラリと利用側の認識を一致させた。
- **test_phase1/2.sh のランチャ座標が陳腐化していた** (489b467 の Debug Sample
  追加でアイコン位置がずれた)。クリックではなくデバッガの `spawn` コマンドで
  起動するようにして座標依存を排除した。

サイズ実測 (S3 / NARYAv3、変更前後の同条件ビルド):

| | バイナリ | パーティション空き |
|---|---|---|
| 変更前 | 0x1f34d0 (2,045,136 B) | 0xcb30 (52,016 B, 2%) |
| 変更後 | 0x1f4740 (2,049,856 B) | 0xb8c0 (47,296 B, 2%) |

増分 +4,720 B (+0.23%)。`-g` は変更前から全ターゲット共通だったため今回の増分には
含まれない。S3 の残り 2% は Phase 3a 以前からの既存状況であり、本変更が原因ではない。
現時点では `FMRB_RUBY_DEBUG_INFO` のオプション化は不要と判断する。ただし
BLE サービス追加 (3b) で数十 KB 増える見込みなので、その時点で再評価すること。
P4 (NARYAv4) は 0x36c2a0 (3,588,768 B)、4MB パーティションに 14% 空き。

RAM 実測 (P4、objdump -h でセクション確認):

- `s_dctx[4]` = 0x7420 (29,728 B) が `.ext_ram.bss` に配置されていることを確認。
  内訳は主に park_buf 4KB + handles 128 x mrb_value + bps 16 本 x4 VM。
  計画時の見積り (約 9KB) は park_buf を 2KB としていたための過小評価で、
  実際の `PARK_BUF_SIZE` は 4096。内部 RAM は消費しない。
- `s_rx_body` (4KB) は現状 ESP32 側で消えている。`s_tp` が NULL 固定なので
  `debugd_main` の本体ごと dead code 除去されるため (`debugd_main` は 0x56 バイト)。
  Phase 3b で BLE トランスポートを繋いだ時点で PSRAM に現れる。
- `fmrb_debug_ctx.c` のコードサイズは code_fetch_hook 0x1106 + 周辺で数 KB (フラッシュ)。

### 3.1 ビルド設定

1. `lib/add/family_mruby_esp32.rb`: defines 群 (15-32 行、`FMRB_NO_IO_CONSOLE`
   の直後) に `conf.cc.defines << 'MRB_USE_DEBUG_HOOK'` を追加。
2. `components/picoruby-esp32/mruby_abi_defines.cmake`: 現在 linux 分岐 (28-33 行)
   にある `MRB_USE_DEBUG_HOOK` を**共通部 (20-27 行) へ移動**する
   (linux/esp32/p4 全ターゲット有効化)。lib/add/family_mruby_esp32p4.rb も
   esp32 と同様に defines を追加。
   - 全ターゲット常時有効の根拠: hook NULL 時のコストは命令ディスパッチ毎の
     分岐 1 個。3.3 の実測で問題が出た場合のみ「debug ビルド変種」に切り替える。
3. `-g` は既に全ターゲット共通 (compile_ruby_to_bytecode.cmake)。フラッシュ増
   (bytecode +13-14%) が S3 のパーティションに収まるかをビルドで確認する。
   溢れる場合は `-g` を CMake オプション化 (`FMRB_RUBY_DEBUG_INFO=ON/OFF`) して
   ESP32 リリースビルドのみ落とす。

### 3.2 デバッグコアの共通化

1. `main/CMakeLists.txt`: `fmrb_debug_proto.c` / `fmrb_debug_ctx.c` /
   `fmrb_debugd.c` を `COMPONENT_LINUX_SRCS` から共通 `COMPONENT_SRCS` へ移動。
   `fmrb_debug_transport_tcp.c` は linux 専用のまま。`msgpack-esp32` の REQUIRES を
   共通化 (P4 では display_p4 が既に使用、S3 は要確認)。
2. `main/boot/boot.c`: `fmrb_debugd_init()` の呼び出しを
   `#ifdef CONFIG_IDF_TARGET_LINUX` から全ターゲットに広げる。ただし
   Phase 3b 完了までは ESP32 でトランスポートが無いので、
   `fmrb_debugd.c` のトランスポート選択を
   `#ifdef CONFIG_IDF_TARGET_LINUX -> tcp / #else -> ble (3b で実装)` とし、
   3a の時点では ESP32 では init を呼ばない (配線だけ準備)。
3. メモリ配置: `s_dctx[4]` (park_buf 2KB x4 含む約 9KB) と debugd の受信バッファ
   (4KB) を ESP32 では PSRAM に置く (`EXT_RAM_BSS_ATTR`、ble_task.c の
   `EXT_RAM_BSS_ATTR` 使用箇所と同様)。linux では通常 BSS。

### 3.3 メモリオーダリング対応 (実機必須)

Linux (x86 TSO) では問題にならなかった store 順序を、Xtensa/RISC-V 向けに保証する:

- attach: dctx フィールド初期化 -> **リリースバリア** -> `mrb->code_fetch_hook` 設定
  -> `armed = true`。GCC ビルトイン `__atomic_store_n(&d->armed, true,
  __ATOMIC_RELEASE)` と、hook 側の `__atomic_load_n(&d->armed, __ATOMIC_ACQUIRE)`
  を使う (FreeRTOS ポータブルマクロより明示的で linux でもそのまま動く)。
- `in_use` / `mrb` (ctx_by_mrb が読む) も同様に release/acquire ペアにするか、
  「`armed` の acquire 後にのみ他フィールドを読む」構造に整理する。
- bp_set の enabled も `__ATOMIC_RELEASE` store にする (2.1 の修正と同時に実施可)。

### 3.4 実機検証 (S3、トランスポート無しで)

- Phase 0 と同様の使い捨て検証は作らず、`fmrb_debugd_init()` を仮に
  UART コンソール等から呼べる形にはしない。代わりに **一時的に S3 でも TCP
  トランスポートがビルドできるか確認** ... は lwIP 差異が大きいため行わない。
  3a の完了条件は「ESP32 ビルドが通り、ABI ガードで abort せず起動し、
  既存機能 (アプリ起動・ps) が退行しないこと」+ 下記計測まで。
  hook の実機動作確認は 3b の BLE 経由で行う。
- 計測 (Phase 0 から延期していた項目):
  - hook NULL 時のオーバーヘッド: 実機で Ruby ベンチ (ループ) を
    MRB_USE_DEBUG_HOOK 有無ビルドで比較。5% を超えるなら debug ビルド変種化を検討。
  - フラッシュ増: `-g` 有無でのバイナリ/パーティション使用量。
  - RAM 増: sizeof(mrb_state) の増分 (ポインタ 2 本 x VM 数) は微小、
    PSRAM 配置の debugd バッファ約 13KB を記録。

## 4. Phase 3b: BLE debug GATT サービス (デバイス側)

**ステータス: 実装済み (2026-07-21)。確定した詳細仕様は
`doc/remote_debug/vm_remote_debug_impl_plan_phase3b.md`、ワイヤ形式は
`doc/remote_debug/vm_remote_debug_protocol.md` sec 1 を参照。以下は着手時の調査メモ。**

既存 BLE ファイル転送サービス (main/drivers/ble/ble_task.c) と同型で追加する。
調査で確定した構造 (根拠行番号は 2026-07-17 時点):

- UUID 群: `ble_task.c:260-288`。ベース UUID の byte[0] で用途区別
  (0x01 service / 0x02 info / 0x03 fs_rx / 0x04 fs_tx)。
- サービス登録: `gatt_svr_svcs[]` (348-375)。同配列に追記すれば
  `ble_gatts_count_cfg/add_svcs` (1299-1311) は変更不要。
- フレーミング: `COBS([payload]) + CRC32 + 0x00 デリミタ`。
  `cobs_encode/decode` (156-214)、`crc32_calc` (147-154)。
  受信は NimBLE ホストタスクの access_cb (307-339) がバッファ蓄積 + セマフォ give、
  処理は専用タスク `ble_fs_task_func` (1114-1166) で実行。
- 送信: `ble_fs_send_notify` (524-568) が MTU-3 で分割 notify、
  チャンク間 5ms delay + 失敗時 20ms 後 1 リトライ。
- 接続管理: `ble_gap_event_handler` (430-518)。SUBSCRIBE (496-504) で
  val_handle 毎の購読フラグ、DISCONNECT (453-465) で状態リセット。

### 4.1 フレーミング共通化

`cobs_encode/decode` / `crc32_calc` は ble_task.c の static 関数なので、
`main/drivers/ble/ble_framing.{h,c}` に切り出して両サービスから使う
(ble_task.c から該当関数を移動、Legacy コピーは残さない)。

### 4.2 debug サービスの追加 (ble_task.c への追記)

チェックリスト (調査 8 項に準拠):

1. UUID 追加: byte[0] = 0x05 (debug service) / 0x06 (debug RX, write) /
   0x07 (debug TX, notify)。
2. `g_dbg_tx_val_handle` / `g_dbg_tx_subscribed` を追加 (99, 45 行付近の並び)。
3. access_cb: `gatt_chr_dbg_rx_cb` を新規実装。fs 版と同様に受信バッファへ
   蓄積し 0x00 デリミタで完了フラグ + セマフォ give。TX 側 cb は空実装。
4. `gatt_svr_svcs[]` にサービスエントリ追加。
5. SUBSCRIBE イベントに `g_dbg_tx_val_handle` の分岐追加。
6. DISCONNECT に debug 受信状態のリセット追加。
7. debug 用受信コンテキスト (バッファ 4KB, PSRAM `EXT_RAM_BSS_ATTR`) と
   セマフォを `ble_task_init` で初期化。
8. 処理タスクは新設しない (4.3 のとおり debugd タスクが引き取る)。

フレーム内容: `COBS( [u16 len BE][msgpack body] ) + CRC32 + 0x00`。
TCP の 4byte 長プレフィクスと違い COBS+デリミタで境界が取れるため、
len は整合性チェック用 (CRC32 は COBS 前の平文に対して計算、fs 方式踏襲)。
JSON は使わない (プロトコルは TCP と同一の msgpack body)。

### 4.3 BLE トランスポート実装 (fmrb_debug_transport_ble.c)

`main/drivers/debug/fmrb_debug_transport_ble.c` (ESP32 系専用 TU、
`COMPONENT_ESP32_SRCS` に登録) で `fmrb_debug_transport_ops_t` を実装する:

- `init`: ble_task 側に用意する登録 API
  `ble_debug_register(rx_queue)` を呼び、受信フレーム受け渡し用キュー
  (深さ 2、要素 = 完成 msgpack body の長さ + PSRAM バッファ index) を渡す。
  BLE 初期化前でも FMRB_OK を返し、未接続扱いにする。
- `poll`: キューをタイムアウト付きで receive。access_cb 側 (NimBLE タスク) は
  デリミタ検出時に COBS デコード + CRC 検証まで行わず、**生フレームを
  キューに渡し、デコードは debugd タスク側 (poll 内) で行う**
  (NimBLE タスクを重くしない。fs 実装と同じ役割分担)。
- `send`: len ヘッダ + CRC 付与 -> COBS エンコード -> デリミタ ->
  `ble_gatts_notify_custom` を `g_dbg_tx_val_handle` に対して分割送信
  (fs の send_notify を val_handle 引数付きに一般化して共用)。
- `connected`: `g_connected && g_dbg_tx_subscribed`。
- `close_client`: BLE では能動切断しない (no-op + 受信バッファリセット)。

debugd 側の変更は `s_tp` の選択だけ:
`#ifdef CONFIG_IDF_TARGET_LINUX -> &fmrb_debug_transport_tcp
 #else -> &fmrb_debug_transport_ble`。

切断時の扱いは TCP と同じ: `connected()` が false に落ちたら
`fmrb_debug_ctx_detach_all()` (debugd 既存ロジックがそのまま機能する)。
パーク中 VM が切断で放置されない安全側の動作を維持する。

### 4.4 帯域とメッセージサイズの検討

- デバッグメッセージは通常 1KB 未満 (stack_trace/frame_vars は park_buf 2KB 上限)。
  NimBLE notify 実効 数KB/s-10KB/s で応答 1 秒未満に収まる想定。実測して
  protocol.md に記録する。
- `log_read` の linebuf 2KB はそのまま。ログ全量ストリームは Phase 4 の
  log_stream 実装時にレベルフィルタ必須 (design.md 記載どおり)。

## 5. Phase 3c: ホスト側 BLE バックエンド

**ステータス: 実装済み (2026-07-21)。確定した詳細仕様は
`doc/remote_debug/vm_remote_debug_impl_plan_phase3c.md`、ホスト側の約束は
`doc/remote_debug/vm_remote_debug_protocol.md` sec 1 "BLE GATT" を参照。以下は着手時の調査メモ。**

### 5.1 fmrb_dbg_client.py の transport 抽象化

- 現状 TCP ソケット直書きの部分を `Transport` クラス (connect/send_frame/
  recv_frame) に分離し、`TcpTransport` / `BleTransport` を実装。
- `BleTransport`: `bleak` 使用。スキャンで名前 `Family-mruby-*` (MAC 付き名。
  ble_on_sync が上書きするため "FamilyMruby" 固定ではない点に注意) を検索、
  debug TX characteristic (0x07 UUID) を notify 購読、RX (0x06) へ write。
  COBS+CRC32 フレーミングを Python 側にも実装 (小さいので自前実装、
  test でデバイス実装と突き合わせ)。
- 接続指定: `--transport tcp:host:port` (既定) / `--transport ble[:device-name]`。

### 5.2 DAP アダプタ / VSCode 拡張

- `fmrb_dap_adapter.py`: launch.json に `"transport": "ble"` /
  `"deviceName": "Family-mruby-XXXXXX"` を追加し、client の transport 選択に渡す。
  DAP 層・行マッパーは無変更。
- `vscode-fmrb-debug/package.json`: `extensionKind` を `["ui"]` 優先に戻す
  (BLE は Windows 側プロセス必須。TCP は Windows からも localhost で届くため
  ui 固定で両立する)。
- Windows 側 Python + bleak + msgpack のセットアップ手順を README に追記。
  アダプタの exe 化 (pyinstaller) は Phase 4。

### 5.3 検証 (S3 実機)

- S3 実機でアプリに attach -> BP -> step -> frame_vars -> detach の一連を
  BLE 経由で確認 (test_phase1.py に `--transport ble` を追加して流用)。
- 実機ならではの確認項目:
  - パーク中の WDT / 監視系の誤検知有無 (計画 1 のリスク表記載の再確認)
  - パーク中の描画停止がユーザ操作 (他アプリ) に波及しないこと
  - BLE 切断 -> 自動 detach -> VM 走行再開
  - 再接続 -> 再 attach -> BP 再設定 (アダプタは setBreakpoints を保持しているので
    VSCode 側の再起動なしで復帰できるかを確認、必要ならアダプタに再接続処理を追加)

## 6. Phase 3d: P4 (esp-hosted vHCI) 対応

- コード上は S3 と共通 (ble_task.c の `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE` 分岐が
  既に吸収)。デバッグサービス追加分に P4 固有コードは発生しない見込み。
- 作業は実測と安定化:
  1. P4 + C6 で疎通 (attach/BP/step)。
  2. スループット実測 (S3 native との比較)。応答が遅い場合は
     send_notify のチャンク間 delay (現行 5ms) の調整、stack_trace の
     max_frames 既定値の調整。
  3. 切断再接続の安定性 (esp-hosted の RPC タイミング注意点は
     boot.c の radio init 順序コメント参照)。
- boot 順序: P4 は `modern_radio_init_task` で BLE init が遅延するため、
  `fmrb_debugd_init()` は BLE init 完了に依存しない構造を維持する
  (transport の init/connected が未初期化状態を安全に返すこと。4.3 の設計で担保)。

## 7. Phase 4: 仕上げ (優先度順)

1. **evaluate**: park ループに PARK_EVAL を追加。mrc で式をコンパイルし
   停止フレームのコンテキストで実行 (vendored `mruby-binding` /
   `mruby-proc-binding` を利用)。結果は format_value で整形。
   GC・例外の後始末 (`mrb_protect_error`) を必ず入れる。副作用のある式は
   ユーザ責任 (DAP の evaluate と同じ扱い)。
2. **変数の詳細展開**: frame_vars に `expand` (変数名/パス指定) を追加し、
   Array/Hash/ivar を 1 階層ずつ展開。DAP の variablesReference に対応させる。
3. **log_stream push**: `log_stream` コマンド (on/off + レベル) と `output`
   イベント。ble_task の `ble_fs_poll_logs` (819-862) と同じ差分読み出しを
   debugd ループに追加。DAP の output イベントへ中継。
4. **DEBUGGING プロセス状態**: 実機でカーネル UI からの kill/suspend と
   パークの競合が実際に問題になるかを 5.3 で観察してから導入判断。
   導入する場合は plan1 の 2.3 に記載した 3 点セット
   (enum 末尾 + state_names + is_valid_transition) + ウィンドウ操作ガード
   7 箇所 (fmrb_app.c 1794 以降) に DEBUGGING を追加。
5. **パッケージング**: vsix 化、アダプタの pyinstaller exe 化、
   セットアップドキュメント (Windows / WSL 両構成)。
6. **ドキュメント**: protocol.md への BLE フレーミング追記、design.md 最終更新、
   進捗ログのクローズ。

## 8. 作業順序ブレークダウン

依存関係順。各項目は独立にビルド・検証可能な単位。

1. [P2] レビュー指摘 3 件修正 + ルートリポジトリの未コミット分整理 (2.1, 2.2) -> 済
2. [P2] VSCode GUI 確認 (ユーザ) -> 済 (2026-07-19 確認)
3. [P3a] ESP32 ビルド設定 (define 共通化 + esp32/p4 build_config) + S3 ビルド確認 -> 済
4. [P3a] メモリオーダリング対応 (atomic 化) + PSRAM 配置 + Linux で回帰テスト
   (test_phase1/2 再実行) -> 済
5. [P3a] S3 実機で起動・退行確認 + hook/フラッシュ計測 (ユーザ協力) -> 未 (次の作業)
6. [P3b] ble_framing 切り出し + debug GATT サービス + BLE トランスポート
7. [P3c] Python BLE バックエンド + extensionKind 復帰 + S3 実機 E2E (ユーザ協力)
8. [P3d] P4 疎通・実測・調整 (ユーザ協力)
9. [P4] evaluate -> 変数展開 -> log_stream -> (判断) DEBUGGING 状態
10. [P4] パッケージング + ドキュメントクローズ

Linux での自律検証は 4 の回帰テストまで。5 以降の実機/BLE/GUI 検証は
ユーザの協力が必要 (ヘッドレスハーネスの対象外)。

## 9. リスク・要検証事項

| 項目 | 内容 | 対応 |
|---|---|---|
| hook 常時有効の実機性能 | 命令毎の NULL チェック分岐が S3/P4 で無視できるか | 3a で実測。5% 超なら debug ビルド変種 (rake タスク分岐) に切替 |
| フラッシュ増 (-g) | bytecode +13-14% が S3 パーティションに収まるか | 3a でビルド確認。溢れたら -g を CMake オプション化 |
| BLE 帯域 (特に P4 vHCI) | stack_trace/vars の応答遅延 | 3d で実測。チャンク delay 調整、必要なら payload 圧縮は将来課題 |
| NimBLE タスクと debugd の分担 | access_cb で重い処理をしない | 生フレームをキューで渡しデコードは debugd 側 (4.3) |
| 再接続時のセッション再同期 | BLE は TCP よりも切断が起きやすい | 切断 = detach_all で安全側。再 attach はアダプタが担う。5.3 で動作確認し、不足ならアダプタに自動再接続を実装 |
| msgpack-esp32 の S3 ビルド | S3 ターゲットでの使用実績が P4 より少ない | 3a のビルド確認で早期検出 |
| カーネル UI からの kill と パークの競合 | Linux では未発生だが実機 UI 操作頻度は高い | 5.3 で観察。問題が出たら Phase 4 の DEBUGGING 状態を前倒し |
| bleak の Windows 安定性 | スキャン/再接続の挙動が環境依存 | デバイス名直指定 (スキャン省略) のオプションを用意 |
