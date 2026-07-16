# PicoRuby VM リモートデバッグ 実装進捗ログ

このファイルは実装作業の進捗を随時記録し、セッションが切れても再開しやすくするための
作業ログである。正となる設計/計画は以下:

- 設計: `doc/vm_remote_debug_design.md`
- 実装計画: `doc/vm_remote_debug_impl_plan.md` (これに従って実装中)
- プロトコル仕様: `doc/vm_remote_debug_protocol.md` (Phase 1 で作成予定)

最終更新: 2026-07-16

## 現在のステータス

**Phase 0 + Phase 1 完了・全検証済み。次は Phase 2 (DAP アダプタ + VSCode 拡張)。**

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

Python: `tools/debug/test_phase1.py` (フロー検証) + `test_phase1.sh` (ヘッドレス自律ラッパ)。

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
- `tools/debug/fmrb_dbg_client.py`: Python テストクライアント兼ライブラリ
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
2. `tools/debug/fmrb_dap_adapter.py`: stdio DAP <-> TCP。initialize/attach/threads/
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
| 9 | P2 | combined.map.json 生成 (cmake拡張) | 未着手 |
| 10 | P2 | fmrb_dap_adapter.py | 未着手 |
| 11 | P2 | VSCode拡張 + E2E確認 | 未着手 |
| 12 | - | protocol.md 清書、design.md ステータス更新 | 未着手 |

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
  hook 型警告を __typeof__ キャストで解消。Phase 1 完了。次は Phase 2。
</content>
