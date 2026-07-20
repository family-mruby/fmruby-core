# PicoRuby VM リモートデバッグ 実装進捗ログ

このファイルは実装作業の進捗を随時記録し、セッションが切れても再開しやすくするための
作業ログである。正となる設計/計画は以下:

- 設計: `doc/vm_remote_debug_design.md`
- 実装計画: `doc/vm_remote_debug_impl_plan.md` (これに従って実装中)
- プロトコル仕様: `doc/vm_remote_debug_protocol.md` (Phase 1 で作成予定)

最終更新: 2026-07-21

## 現在のステータス

**Phase 0 + 1 + 2 + 3a + 3b + 3c 完了。残るユーザ作業は VSCode GUI 操作確認と
BLE 実機 E2E (手順は vscode-fmrb-debug/README.md)。次は Phase 3d (P4 実機調整)。**

### Phase 3c 完了 — ホスト側 BLE バックエンド + VSCode 接続 (2026-07-21)

計画書: `doc/vm_remote_debug_impl_plan_phase3c.md` (逸脱なし)。デバイス側は無変更。

fmruby-core/tool/debug:
- `fmrb_ble_framing.py` (新規): COBS + CRC32 の純関数 (`cobs_encode/decode`,
  `encode_frame`, `decode_frame`)。bleak 非依存。`__main__` にセルフテスト同梱。
- `fmrb_ble_transport.py` (新規): `BleTransport`。bleak の asyncio を
  デーモンスレッドのイベントループに隠蔽し、同期 API へブリッジ
  (全 `run_coroutine_threadsafe` にタイムアウト付き)。スキャン (名前完全一致 /
  `Family-mruby-` プレフィクスで 1 台 / MAC 指定はスキャン省略)、
  notify 再結合、MTU-3 分割 write、`disconnected_callback`。
- `fmrb_dbg_client.py`: `TcpTransport` を切り出し、`FmrbDebugClient(transport, ...)`
  + `from_target(target)` に変更 (旧 `(host, port)` シグネチャは削除)。
  target 書式は `host[:port]` / `ble` / `ble:<name-or-address>`。
  BLE モジュールは BLE target のときだけ import (TCP 利用者に bleak 不要)。
- `fmrb_dap_adapter.py`: launch 引数 `transport` ("tcp"|"ble"、既定 tcp) と
  `deviceName` を追加。接続失敗 (bleak 未導入・スキャン 0/複数台・ハンドシェイク
  枯渇) は attach 失敗の message に載せて VSCode に表示。
- `test_phase1.py`: `from_target` 利用に変更 (`ble:...` を渡せば実機にも流用可)。

**接続確立 (登録前フレーム喪失対策)**: `FmrbDebugClient.connect()` が
`version` をタイムアウト 2 秒 x 最大 3 回リトライしてから返る。TCP も同じ経路。

family-mruby/vscode-fmrb-debug:
- `package.json`: `extensionKind` を `["ui"]` のみに (WSL2 に Bluetooth が無いため
  拡張とアダプタを Windows 側で動かす)。`transport` / `deviceName` 属性と
  BLE attach スニペットを追加。version 0.0.2。
- README.md / README.ja.md: BLE セクション (Windows 側 pip、launch.json 例、
  pythonPath の注意、UNC パスの逃げ道、BLE 固有のトラブルシューティング) を追記。
- `fmrb-debug-0.0.2.vsix` を生成済み。**インストールはユーザ操作** (再パッケージ
  しただけでは反映されない)。

**検証**:
- コーデックのセルフテスト `python3 tool/debug/fmrb_ble_framing.py` PASS。
- **C 実装とのバイト一致 (クロス検証)**: `ble_framing.c` をホストでビルドした
  ベクタ生成ツールと Python を、body 長 0-4096 のランダム 2000 本で比較し
  **全フレームがバイト単位で一致**、かつ C 製フレームを Python デコーダが全て復元。
  固定ベクタ (version リクエスト) `01030c940f01a776657273696f6ec0654980c900` は
  C の出力そのものをセルフテストに埋め込んである。
  検証ハーネスは `tool/debug/` に収録: `framing_vec.c` (ビルド手順はヘッダ参照) +
  `test_ble_framing_cross.py` で再現でき、偽 BleakClient による
  BleTransport ロジックテストは `test_ble_transport.py` (bleak 必要、電波不要)。
- bleak 未導入環境で `import fmrb_dbg_client` / `fmrb_dap_adapter` が通ること、
  BLE target 指定時のみ分かりやすい ImportError になることを確認。
- TCP 回帰: `test_phase1.sh` / `test_phase2.sh` ともに PASS。
- `npx @vscode/vsce package` 成功。

**ユーザ E2E 待ち**: BLE 実疎通 (S3)。手順は vscode-fmrb-debug/README.md
"Debugging hardware over BLE" / README.ja.md "実機を BLE でデバッグする"。
確認項目は impl_plan_phase3c.md sec 8.2 参照。

### Phase 3b 完了 — BLE debug GATT サービス (デバイス側, 2026-07-21)

計画書: `doc/vm_remote_debug_impl_plan_phase3b.md` (逸脱なし)。

追加/変更ファイル:
- `main/drivers/ble/ble_framing.{h,c}` (新規): `ble_task.c` にあった
  `crc32_calc` / `cobs_encode` / `cobs_decode` + crc32 表を移動し
  `ble_framing_*` に改名。ファイル転送サービスと debug サービスで共用。
- `main/drivers/ble/ble_debug_link.h` (新規): ble_task <-> トランスポート間の
  受け渡し API (`ble_debug_register` / `ble_debug_send` / `ble_debug_link_ready` /
  `ble_debug_rx_buf`) と `BLE_DBG_MAX_ENC` (4120) / `ble_dbg_frame_ref_t`。
- `main/drivers/ble/ble_task.c`: UUID 0x05/0x06/0x07 で debug service を追加
  (`gatt_svr_svcs[]` に 2 つ目の primary service)。`gatt_chr_dbg_rx_cb` は
  NimBLE ホストタスク上で走るため蓄積と ready キュー投入のみ (COBS/CRC は触らない)。
  `ble_fs_send_notify` を `ble_send_notify(val_handle, subscribed, ...)` に一般化し
  両サービスで共用。SUBSCRIBE / DISCONNECT に debug 側の分岐を追加。
- `main/drivers/debug/fmrb_debug_transport_ble.c` (新規): `fmrb_debug_transport_ops_t`
  実装。free/ready キュー (深さ 2) を所有し、poll でデコード + CRC/len 検証、
  send で plain 構築 -> CRC -> COBS -> デリミタ。作業バッファは PSRAM。
- 配線: `fmrb_debugd.c` の `s_tp` を ESP32 で BLE に (NULL 分岐と `if (!s_tp)`
  ガードを削除)、`boot.c` の `fmrb_debugd_init()` を全ターゲットで呼ぶよう
  `#ifdef CONFIG_IDF_TARGET_LINUX` を撤去、`main/CMakeLists.txt` の
  `COMPONENT_ESP32_SRCS` に新規 2 TU を追加。

**設計上のポイント**: RX バッファは free -> access_cb -> ready -> poll -> free の
一方向循環で、NimBLE タスクと debugd タスクが同一バッファを同時に触らない
(キューがメモリバリアも兼ねる)。トランスポート init は BLE 初期化状態に依存しない
(登録はハンドル保存のみ) ため、P4 の遅延 radio init と boot 順序が競合しない。

**サイズ実測 (S3 / Retro)**: 0x1f4740 -> 0x1f89b0 バイト (+16,496, 約 +0.8%)。
factory 3MB に対し 34% 空き。新規静的バッファは全て PSRAM で内部 RAM を消費しない
(RX ping-pong 2 x 4120 + poll デコード 4102 + send encoded 4120 = 約 16.5KB。
計画書の見積 21KB より小さい: send 側は plain と encoded の 2 面で足りた)。

**フレーミング検証 (ネイティブ, ASan/UBSan)**: `ble_framing.c` + トランスポートの
frame 構築/解析ロジックをホストでビルドし、body 長 0-4096 の全域 (0-600 全長 +
境界値 + ランダム 4000 回 x 4 パターン: 全ゼロ/ゼロ無し/ランダム/疎ゼロ) で
ラウンドトリップ PASS。**最悪エンコード長は 4119 + デリミタ = 4120 で
`BLE_DBG_MAX_ENC` にちょうど収まる (余裕ゼロだが証明済み)**。COBS 出力に
内部 0x00 が現れないこと、破損 (144/144) と切り詰め (39/39) が全て拒否されることも確認。

**検証**:
- S3: `rake build:esp32` 成功、check_sizes 通過。
- P4: `rake clean_all` 後 NARYAv4 で成功 (0x370ca0 バイト、4MB に対し 14% 空き)。
  注: `.env` が shell の `FMRB_HW_TARGET` を上書きするので、ターゲット切替は
  `.env` の編集が必要 (環境変数を渡すだけでは効かない)。
- Linux 回帰: `rake clean_all` 後 `rake build:linux` 成功、
  `test_phase1.sh` / `test_phase2.sh` ともに PASS (TCP パスに退行なし)。

**レビューで見つけて直した点**:
- 切断時に ready キューの読み残しが残ると、次のクライアント接続後に旧セッションの
  リクエストが 1 回実行され得た (spawn 等は副作用あり)。計画書は close_client で
  破棄する想定だったが、**debugd は close_client を呼ばない** (TCP も同様の既存仕様)
  ため、BLE_GAP_EVENT_DISCONNECT 側で ready -> free に drain するよう修正。
- `ble_poll` の `!s_ready_q` 防御分岐が即 return 0 で、万一到達すると debugd が
  ビジーループする。timeout 分 delay してから返すよう修正 (init 成功後は到達しない)。

**ユーザ確認待ち (ヘッドレス不可)**: BLE 実疎通は Phase 3c のホスト側実装後に
実機で行う。本フェーズで担保したのは「未接続時に何も壊さない」ことまで。

### 追加機能 — 変数の詳細展開 (nested variables, 2026-07-17)

Array/Hash/Object の子要素を variables ペインで展開できるようにした。

- `fmrb_debug_ctx.c`: stop ごとの `handles[]` テーブル (MAX_HANDLES=128, park 開始で reset)。
  `expand_handle_for` が展開可能値 (要素>0 の Array/Hash, ivar を持つ Object) に
  1-based ハンドルを割当。`pack_one_var` で `{name,type,value,truncated,ref}` を共通生成
  (`build_frame_vars` も共通化)。`build_expand`:
  Array→`[i]` / Hash→`mrb_hash_keys`+`mrb_hash_get` / Object→`mrb_iv_foreach`。
  MAX_CHILDREN=20 で打ち切り、超過分は `...: (N more)` 集約エントリで通知。
  park_buf を 4096 (=MAX_FRAME) に拡大。GC は park 中に走らずハンドル値は到達可能なので安全。
- proto: `DBG_CMD_EXPAND` + req `handle` フィールド + `"expand"` デコード。
- debugd: `handle_inspect` で EXPAND を分岐 (`fmrb_debug_ctx_expand`)。
- `fmrb_dbg_client.py`: `expand(pid, handle)`。
- `fmrb_dap_adapter.py`: variablesReference をマップ化
  (`_varmap`: DAP ref -> ("frame",idx) | ("handle",dev_handle))。stop で reset。
  `req_scopes`/`req_variables` が frame/handle 両対応、子の `ref>0` に新 DAP ref を発行。

**検証 (ヘッドレス自律, test_phase1.sh PASS)**: top の `app` (KamonApp) を展開 ->
ivar 20 件 + `(12 more)` (全32件, 打ち切り+集約ノート動作)。入れ子 `@gfx` -> ivar、
その中の `@current_font: Array(len=1) ref=4` も展開可能フラグ付き。スカラーは ref=0。
不正ハンドルは INVALID_PARAM で拒否。step/continue/detach に回帰なし。

**既知の制限**: 1 応答あたり子要素 20 件 (DAP paging 未対応)。超過は集約ノートで表示。

### Phase 2 完了 — DAP アダプタ + VSCode 拡張 (2026-07-17)

追加ファイル:
- `tool/debug/gen_combined_rb.py`: combined.rb 連結 + `*_combined.map.json` 生成
  (原本<->combined 行変換用)。`compile_ruby_to_bytecode.cmake` の `cat` を置換。
  ビルドで system_desktop/shell/fmrb_kernel の map.json が生成されることを確認。
- `tool/debug/fmrb_dap_adapter.py`: stdio DAP <-> TCP。実装リクエスト:
  initialize/attach/setBreakpoints/configurationDone/threads/stackTrace/scopes/
  variables/continue/next/stepIn/stepOut/pause/disconnect/source。
  `Mapper` クラスで pathMappings(device<->local) + projectMappings + combinedMaps を処理。
  fmrb イベント (stopped/resumed/exited) を DAP イベント (stopped/continued/thread+terminated) へ中継。
- `tool/debug/test_phase2.py` / `test_phase2.sh`: 自律テスト
  (Part A: 行マッパー単体 / Part B: アダプタを VSCode 相当で stdio 駆動)。
- `family-mruby/vscode-fmrb-debug/`: 最小 VSCode 拡張
  (`package.json` contributes.debuggers type=fmrb + extensionKind:["ui"],
   `extension.js` DebugAdapterExecutable で python アダプタ起動, `README.md`)。

**検証 (ヘッドレス自律, test_phase2.py PASS)**:
- Part A: `clock_setting.rb:5` <-> `system_desktop_combined.rb:103` 往復、standalone basename 変換。
- Part B: DAP フロー全部通過。stackTrace の source.path が
  `.../flash/app/demo/kamon.app.rb:53` にホストパス変換される。step/continue/stopped イベント中継OK。

**ユーザ確認待ち (ヘッドレス不可)**: VSCode で拡張を F5 起動 -> launch.json (type:fmrb, attach) ->
エディタ余白 BP -> 停止線表示 -> 変数ペイン -> ステップ -> continue -> disconnect の GUI 操作。
手順は `vscode-fmrb-debug/README.md` 参照。combined アプリ (system_desktop) の原本 BP も要確認。

### Phase 1 完了 — パーク方式デバッグコア (2026-07-17)

追加ファイル: `main/drivers/debug/fmrb_debug_ctx.{h,c}`
- per-VM デバッグコンテキスト (static 配列 `s_dctx[FMRB_DEBUG_MAX_ATTACH]`)。
- `code_fetch_hook` 本体: mrb->dctx 逆引き (線形) -> armed 高速パス -> debug_info チェック ->
  pause/step/BP 判定 -> park。
- BP 判定: 行頭判定 (`is_line_start`, mrdb 移植) + basename 照合 + at_stop ラッチで
  多重ヒット防止 (ループ内 BP は再ヒットする)。
- step (in/over/out): ci アドレス比較で深さ判定。`step_c` (context) 一致チェックで
  mruby-task の task 切替による誤停止を防止。
- park ループ: VM タスク自身が hook 内でブロック。cmd_q でコマンド受信、
  stack_trace/frame_vars は固定バッファ msgpack packer (malloc 不使用) で payload を組んで resp_q へ。
- stack_trace: `mrb->c->cibase..ci` を降順走査。Ruby フレームのみ (proc && !CFUNC)。
  top フレームは ci->pc、それ以外は ci->pc[-1] で行番号解決。C フレームは file=""/line=-1。
- frame_vars: 型別安全フォーマッタ (mrb_inspect 不使用)。arena save/restore で GC 保護。
  Integer/Float/Symbol/String(64B+truncated)/Array/Hash サマリ/その他 #<Class>。
- attach/detach: gen チェックで PID 再利用/終了検知。detach はパーク中なら DETACH 投入で復帰。
- hook->debugd イベントキュー (stopped/resumed/exited)。

debugd 接続: hook系コマンド (attach/detach/bp_set/bp_clear/pause/continue/step_*/
stack_trace/frame_vars) を ctx へ委譲。events を毎ループ forward。クライアント切断で detach_all。
attach 中 pid への kill/stop/suspend は BUSY 拒否。

Python: `tool/debug/test_phase1.py` (フロー検証) + `test_phase1.sh` (ヘッドレス自律ラッパ)。

**検証 (ヘッドレス自律)**: `test_phase1.sh` が PASS。Kamon (オンデバイスコンパイル,
FILE モード) に対し:
- attach -> bp_set kamon.app.rb:53 -> **BP ヒット** (stopped, line=53)
- stack_trace: `#0 on_update@53 / #1-4 native(main_loop/loop/start, file=""/line=-1) / #5 (top)@392`
- step_over -> line 48 で停止 (reason=step)、continue -> 再度 BP、detach -> 走行再開
- frame_vars 実データ確認 (on_event:70): `ev: Hash(size=4) / close_btn_x: Integer 290 / b: nil`
- **パーク中も kernel/desktop VM は動作継続** (別 mrb_state なので波及なし; 操作/撮影が並行動作)。

既知の軽微事項:
- ABI: picoruby.h の `#define mrb_irep mrc_irep` により hook 関数型が名目上 mrc_irep。
  フィールドへ `__typeof__` キャストで代入 (レイアウト互換、実行時は mrc_irep が渡る)。
- ネイティブフレーム (mruby-task の main_loop/loop/start) は file=""/line=-1 で返す。
  DAP 側で source なしフレームとして扱う。

### Phase 1 進捗 (2026-07-16)

作成した新規ファイル (`main/drivers/debug/`):
- `fmrb_debug_proto.h/.c`: msgpack プロトコル。設定定数 (TCP_PORT=5555, MAX_FRAME=4096,
  MAX_ATTACH=4, MAX_BP=16)、コマンド enum、`fmrb_dbg_proto_decode_req()`、
  response/event ライタ (`fmrb_dbg_writer_t`) と pack ヘルパ。
- `fmrb_debug_transport.h`: トランスポート vtable (`fmrb_debug_transport_ops_t`)。
- `fmrb_debug_transport_tcp.c`: Linux TCP 実装。`0.0.0.0:5555`、SO_REUSEADDR、
  非ブロッキング + select、単一クライアント。`[u32 BE len][body]` フレーミングを
  トランスポート内で完結 (poll は body 単位で返す、send は prefix 付与)。
- `fmrb_debugd.h/.c`: debugd タスク。transport init -> poll -> decode -> dispatch。
  非hook系コマンド実装済み: version / ps / log_read / kill / stop / suspend / resume / spawn。
  hook系 (attach/detach/bp/pause/step/stack_trace/frame_vars) は現状 NOT_SUPPORTED(-4)。

その他:
- `main/CMakeLists.txt`: 上記3 .c を `COMPONENT_LINUX_SRCS` に、`drivers/debug` を
  INCLUDE_DIRS に、linux 分岐で `msgpack-esp32` を REQUIRES に追加。
- `main/boot/boot.c`: linux 限定で `fmrb_debugd_init()` を kernel 起動後に呼ぶ。
  **Phase 0 PoC (fmrb_debug_poc.{c,h}) は削除** (検証完了・Legacy 排除規約)。
- `family-mruby/docker-compose.yml`: core サービスに `ports: ["5555:5555"]` 追加
  (WSL2->Windows localhost フォワードで Phase 2 からも届く)。
- `tool/debug/fmrb_dbg_client.py`: Python テストクライアント兼ライブラリ
  (`FmrbDebugClient` クラス + CLI/REPL)。要 `msgpack` パッケージ (pip)。
- `doc/vm_remote_debug_protocol.md`: プロトコル仕様書 (正)。

疎通確認 (ヘッドレス起動 + `fmrb_dbg_client.py localhost:5555 <cmd>`):
- `version` -> `{'proto':1,'fw':'1.0.0'}`
- `ps` -> kernel/system_desktop を state/mem/stack_hw 付きで列挙
- `log_read` -> ログ行 + pos + overrun 返却
- `attach` -> `-4` (未実装につき想定通り)

### 次アクション (Phase 2)

1. `main/prebuild_scripts/compile_ruby_to_bytecode.cmake` を拡張し combined.rb 連結時に
   `<name>_combined.map.json` (`[{file,start_line,lines}]`) を生成 (原本<->combined 行変換用)。
2. `tool/debug/fmrb_dap_adapter.py`: stdio DAP <-> TCP。initialize/attach/threads/
   setBreakpoints/configurationDone/pause/continue/next/stepIn/stepOut/stackTrace/
   scopes/variables/source。pathMappings + basename フォールバック。map.json 行補正。
2. `family-mruby/vscode-fmrb-debug/`: 最小 VSCode 拡張 (contributes.debuggers,
   extensionKind:["ui"], DebugAdapterExecutable で python 起動)。
3. E2E: VSCode(Windows) -> WSL localhost:5555。combined アプリ (system_desktop 等) の
   原本ファイル BP 検証。

### Phase 0 検証結果 (すべてOK)

Linux sim + ヘッドレスハーネスで PoC (`main/drivers/debug/fmrb_debug_poc.c`) を走らせて確認:

- **hook 発火**: `code_fetch_hook` が走行中 VM で命令フェッチ毎に発火。ABI ガードも abort せず起動 (`main_loop started` 到達)。
- **file:line (bytecode `-g` 経路)**: kernel/system_desktop で `fmrb_kernel_combined.rb:1086` 等が取得可。
- **file:line (オンデバイスコンパイル経路)**: `mrc_ccontext_filename` 追加により、ランチャーから起動した Kamon (FILE モード) で `/app/demo/kamon.app.rb:48/49/53` を取得可。
- **変数名 (`irep->lv` / keep_lv)**: デフォルトで取得可。`version_ok`/`attempt`/`files`/`entry`/`result`/`icon_data` 等。**最大リスクだった keep_lv 問題は解消 (追加設定不要)**。
  - `pc - irep->iseq` でオフセット化 -> `mrb_debug_get_line/filename` で解決、を実コードで確認。
  - `irep->debug_info == NULL` の irep はスキップする必要あり (行情報なし)。

### Phase 0 計測結果

- `-g` によるバイトコード .c サイズ増 (同一 combined.rb を mrbc `-g` 有無で比較):
  - `fmrb_kernel_combined`: 111,169 -> 126,756 バイト (+15,587, 約 +14%)
  - `system_desktop_combined`: 461,709 -> 522,429 バイト (+60,720, 約 +13%)
  - .c はバイト配列テキストなので実バイナリ irep 増もほぼ同率。ESP32 フラッシュ判断材料。
- hook NULL 時オーバーヘッド A/B 計測: **Phase 3 (ESP32 有効化時) に延期**。
  理由: `CODE_FETCH_HOOK` は命令ディスパッチ毎に分岐1個 (`if (mrb->code_fetch_hook)`) で
  アーキ的に軽微、かつ現状 linux 限定で linux の数値は実機に転用不可。計画リスク表の
  「ESP32 実機では再確認」方針と整合。

### 未解決/次アクション

- PoC (`fmrb_debug_poc.c`, boot.c の呼び出し, CMake 登録) は Phase 1 の
  `fmrb_debug_ctx.c` 実装後に削除する (Legacy を残さない規約)。それまでは linux 起動時に
  60秒だけ走る一時コードとして残置。
- 次: Phase 1 実装順 3 (`fmrb_debug_proto` msgpack) から着手。

## タスク一覧 (実装計画 セクション7 準拠)

| # | Phase | 内容 | 状態 |
|---|---|---|---|
| 1 | P0 | ビルド設定4点 (define x2, -g, ccontext_filename) + ビルド確認 | 完了 |
| 2 | P0 | PoC フック + 行番号/変数名検証 + 計測 -> doc追記 | 完了 (PoC削除は Phase 1 で) |
| 3 | P1 | fmrb_debug_proto (msgpack エンコード/デコード) | 完了 |
| 4 | P1 | fmrb_debug_transport_tcp + docker compose ポート公開 | 完了 |
| 5 | P1 | fmrb_debugd タスク + 非hook系コマンド | 完了 (version/ps/log_read/kill系/spawn 疎通OK) |
| 6 | P1 | fmrb_debug_ctx (attach/detach/hook/BP/park) + stack_trace/frame_vars | 完了 |
| 7 | P1 | step系 + pause + イベント通知 | 完了 |
| 8 | P1 | fmrb_dbg_client.py + test_phase1.sh | 完了 (自律 PASS) |
| 9 | P2 | combined.map.json 生成 (cmake拡張) | 完了 |
| 10 | P2 | fmrb_dap_adapter.py | 完了 (自律 PASS) |
| 11 | P2 | VSCode拡張 + E2E確認 | 拡張作成済み / VSCode GUI 確認はユーザ |
| 12 | - | protocol.md 清書、design.md ステータス更新 | 進行中 |
| 13 | P3b | ble_framing 切り出し + debug GATT サービス + BLE トランスポート + 配線 | 完了 (S3/P4 ビルド + Linux 回帰 PASS) |
| 14 | P3c | ホスト側 BLE バックエンド (bleak) + VSCode 拡張 extensionKind | 完了 (コーデック C 突合 + TCP 回帰 PASS / 実機 E2E はユーザ) |

## 確定した調査事実 (コードで確認済み)

- `mrb_state` の hook フィールド: `code_fetch_hook` / `debug_op_hook` (mruby.h、`MRB_USE_DEBUG_HOOK` ガード内)。
- 発火: `vm.c:1603 CALL_CODE_HOOKS()` -> `CODE_FETCH_HOOK(mrb, irep, ci->pc, regs)`。pc は `ci->pc` (絶対ポインタ)。
- デバッグ API 実名 (`include/mruby/debug.h`):
  - `const char *mrb_debug_get_filename(mrb_state*, const mrb_irep*, uint32_t pc)`
  - `int32_t mrb_debug_get_line(mrb_state*, const mrb_irep*, uint32_t pc)` (失敗 -1)
  - `mrb_bool mrb_debug_get_position(mrb_state*, const mrb_irep*, uint32_t pc, int32_t *lp, const char **fp)`
- `ctx->filepath` は FILE モードで `attr->filepath` からコピー (fmrb_app.c:1256)、`load_data` も同じポインタ (1273)。BYTECODE モードは空文字。
- `mrc_ccontext_filename(mrc_ccontext*, const char*)` あり (mrc_ccontext.h:82)。`keep_lv:1` ビットフィールドあり (mrc_ccontext.h:41) — 変数名取得可否は PoC で検証。

## 作業ログ (時系列)

### 2026-07-16

- 計画書/設計書を読み込み、前提ファイルの現状を確認 (すべて計画記載と一致)。
- 本進捗ログを作成。
- Phase 0 ビルド設定4点を実施:
  1. `lib/add/family_mruby_linux.rb`: `MRB_USE_DEBUG_HOOK` 追加。
  2. `components/picoruby-esp32/mruby_abi_defines.cmake`: linux 分岐に同 define 追加 (ABI 同期)。
  3. `main/prebuild_scripts/compile_ruby_to_bytecode.cmake`: mrbc 2箇所に `-g` 追加。
  4. `main/app/fmrb_app.c`: `execute_mruby_script()` で `mrc_ccontext_filename(cc, ctx->filepath)` 挿入 (filepath 非空時)。
- `rake clean_all` (ターゲット切替のため) 後 `rake build:linux` -> 成功 (EXIT=0)。
  - 注: 最初 `rake clean` のみだと sdkconfig が esp32p4 向けのままで失敗。ターゲット切替は `clean_all` 必須。
- PoC (`main/drivers/debug/fmrb_debug_poc.{c,h}`) を作成。走行中の全 mruby VM に
  自動 attach し、VM ごとに 40 命令分 file:line + 変数名をログ出力する使い捨てフック。
  CMake の `COMPONENT_LINUX_SRCS` + `INCLUDE_DIRS` に登録、boot.c から linux 限定で起動。
- ヘッドレス起動 + ランチャーから Kamon 起動で、bytecode / オンデバイス両経路を検証 -> 全項目OK。
- `-g` サイズ増を docker 内 mrbc で実測。
- Phase 0 完了。
- Phase 1: プロトコル仕様書作成 -> proto/transport_tcp/debugd 実装 ->
  CMake/boot 配線 -> docker ポート公開 -> python client -> 疎通確認 (version/ps/log_read OK)。
  PoC 削除。ここまでを2リポジトリにコミット (core 22cc240 / root c4712ea)。
- Phase 1 残り: fmrb_debug_ctx (hook/park/BP/step/frame_vars) 実装 -> debugd 接続 ->
  test_phase1.py/.sh 作成 -> Kamon で自律 E2E テスト PASS。frame_vars 実データ確認。
  hook 型警告を __typeof__ キャストで解消。Phase 1 完了・コミット (core 0e49de2)。
- Phase 2: gen_combined_rb.py + cmake で map.json 生成 -> fmrb_dap_adapter.py 実装 ->
  test_phase2.py/.sh 作成 -> 自律 PASS (行マッパー往復 + DAP フル)。
  vscode-fmrb-debug 拡張作成。VSCode GUI 操作のみユーザ確認待ち。

### 2026-07-21

- Phase 3b 実装 (計画書 `vm_remote_debug_impl_plan_phase3b.md` に従い逸脱なし):
  ble_framing 切り出し -> debug GATT サービス + link API -> BLE トランスポート ->
  debugd/boot/CMake 配線 -> ドキュメント更新。
- S3 (+16,496 バイト, 34% 空き) / P4 いずれもビルド成功。
  Linux 回帰 test_phase1.sh / test_phase2.sh 両方 PASS。
- Phase 3b レビューで 2 件修正 (切断時の ready キュー drain / poll のビジーループ
  防止)。コミット 011e9a2。
- Phase 3c 実装: フレーミングコーデック (Python) -> transport 抽象化 ->
  BleTransport -> DAP アダプタ引数 -> VSCode 拡張 (ui 化, 0.0.2) -> ドキュメント。
  コーデックは C 実装と 2000 フレームでバイト一致を確認。TCP 回帰 PASS。
