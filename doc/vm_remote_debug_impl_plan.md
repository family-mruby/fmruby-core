# PicoRuby VM リモートデバッグ 実装計画書 (Linux ビルド先行)

作成日: 2026-07-16
ステータス: 実装計画 (着手前)
前提ドキュメント: doc/vm_remote_debug_design.md (検討ドラフト)

本書は設計ドキュメントの Phase 0〜2 (Linux sim + TCP) を、コード調査で確定した
事実に基づいて実装可能な粒度まで詳細化したもの。BLE (Phase 3) は対象外。

## 1. スコープ

- 対象: Linux ターゲット (`rake build:linux`, ESP-IDF linux target + docker compose)
- ゴール:
  1. Phase 0: デバッグ基盤 (フック/行番号情報) の有効化と PoC・性能計測
  2. Phase 1: `fmrb_debugd` デバッグコア + TCP トランスポート + Python テストクライアント
  3. Phase 2: Python DAP アダプタ + 最小 VSCode 拡張 (TCP 接続)
- 非ゴール: BLE トランスポート、ESP32 実機対応 (ただし ESP32 で壊れない構造にする)、
  `evaluate` (式評価) は Phase 2 完了後の追加項目とする

## 2. 調査で確定した事実 (実装の前提)

実装時に再調査不要なように、根拠となる箇所を列挙する。

### 2.1 ビルド機構

- Linux ビルドの build_config 実体は `lib/add/family_mruby_linux.rb`。
  `components/picoruby-esp32/CMakeLists.txt:5` が `MRUBY_CONFIG` として lib/add の
  原本を直接参照する (Rakefile が submodule へコピーする分は実ビルドでは未使用)。
  defines 追加箇所は同ファイル 10-30 行付近の `conf.cc.defines` 群。
- **ABI 同期規約**: `MRB_USE_DEBUG_HOOK` は `mrb_state` に関数ポインタ 2 本を追加し
  sizeof(mrb_state) を変える。`components/picoruby-esp32/mruby_abi_defines.cmake` の
  `fmrb_add_mruby_abi_defines()` (20-38 行) にも追加しないと、main/ 側 C コードと
  libmruby.a で構造体レイアウトが分裂し、起動時 ABI ガードで abort する。
  **build_config と abi_defines.cmake の両方に追加が必須。**
- 組み込みアプリのバイトコード化は `main/prebuild_scripts/compile_ruby_to_bytecode.cmake`。
  mrbc 呼び出しは 75 行 (連結後) と 86 行 (単一ファイル) の 2 箇所のみ。`-g` はここに追加。
  combined.rb 連結は同ファイル 64 行の `cat ${SUB_RB_FILES} ${RB_FILE}` (GLOB + SORT で決定的順序)。
- lib/ 以下を編集したら `rake clean` が必要 (CLAUDE.md 規約)。

### 2.2 mruby 4.0 デバッグ機構

パス略記: `<MRB>` = `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby`,
`<MRC>` = `components/picoruby-esp32/picoruby/mrbgems/mruby-compiler`

- フック型 (`<MRB>/include/mruby.h:358-361`, `MRB_USE_DEBUG_HOOK` ガード内):
  ```c
  void (*code_fetch_hook)(mrb_state* mrb, const struct mrb_irep *irep,
                          const mrb_code *pc, mrb_value *regs);
  ```
  発火点は `<MRB>/src/vm.c:1603` の `CALL_CODE_HOOKS()` で、全命令フェッチ毎。
  NULL チェック付きマクロなので未装着時のコストは分岐 1 個。
- **pc は絶対ポインタ**。debug API へは必ず `pc - irep->iseq` (uint32_t オフセット) に変換:
  ```c
  int32_t     mrb_debug_get_line(mrb_state*, const mrb_irep*, uint32_t pc_off);      // 失敗時 -1
  const char *mrb_debug_get_filename(mrb_state*, const mrb_irep*, uint32_t pc_off);  // 失敗時 NULL
  ```
  いずれも `irep->debug_info` が NULL なら失敗する。
- ローカル変数: register n (1..nlocals-1) の名前は `irep->lv[n-1]` (`mrb_sym`、0 は無名)、
  値は hook 引数の `regs[n]`。名前文字列化は `mrb_sym_name()`。
  参照実装は `<MRB>/src/codedump.c:11-27`。
- BP 判定ロジック (mrdb から移植、`<MRB>/mrbgems/mruby-bin-debugger/tools/mrdb/`):
  - 行頭判定 `check_start_pc_for_line` (apibreak.c:431-439): 直前命令
    (`pc_off - 1`) が同じ行なら停止しない (1 行 1 ヒット化)
  - 多重ヒット防止: 前回停止の (file, line) と同一なら照合スキップ (mrdb.c:571)
  - step over: 停止時の `mrb->c->ci` を保存し、`(intptr_t)saved_ci < (intptr_t)mrb->c->ci`
    (= 現在の方が深い) の間は素通り (mrdb.c:605-624)。ci は cibase 配列上を進むので
    アドレス比較が深さ比較になる
  - step in: 行が変わったら停止。step out: 現在 ci が保存 ci より浅くなったら停止 (自作)
- スタックトレース (`<MRB>/src/backtrace.c:50-96` 参照):
  `mrb->c->cibase[0 .. ci-cibase]` を降順走査。Ruby フレームは
  `ci->proc && !MRB_PROC_CFUNC_P(ci->proc)` で判定し `irep = ci->proc->body.irep`、
  現在行は `ci->pc[-1]` のオフセットで取得。C フレームは行情報なしとして扱う。
- mruby-task: Linux ビルドの port 指定は posix だが実体は空スタブで、
  実 HAL は `ports/freertos/task_hal.c` (ESP-IDF linux target の FreeRTOS POSIX
  シミュレータ上で動く)。**task 実行中は `mrb->c` が当該 task の context に差し替わる**
  (`<MRB>/mrbgems/mruby-task/src/task.c:424-466`) ため、hook 内の `regs` / `mrb->c->ci`
  は常に「いま走っている Ruby task」のものを指す。
- コンパイル時ファイル名設定 API (`<MRC>/include/mrc_ccontext.h:82`):
  ```c
  const char *mrc_ccontext_filename(mrc_ccontext *c, const char *s);
  ```
  codegen は filename がある場合のみ debug_info を確保する (`<MRC>/src/codegen.c:609-613`)。
  未設定だと行番号 API は全滅する。
- 要検証: `mrc_ccontext.keep_lv` フラグ (`mrc_ccontext.h:41`)。これが立っていないと
  `irep->lv` が落ちて変数名が取れない可能性がある (Phase 0 で確認)。

### 2.3 アプリ実行基盤 (fmruby-core)

- PID から `mrb_state` へは `fmrb_app_get_context_by_id(id)` (fmrb_app.c:1753) →
  `ctx->mrb` で到達できる。**picoruby 側 (task_hal.c の g_tick_manager) への
  アクセサ追加は不要** — 設計ドキュメント 5.2 の「g_tick_manager アクセサ追加」は
  採用しない (submodule パッチを増やさない)。
- `fmrb_proc_state_t` (fmrb_app.h:25-31): FREE/INIT/RUNNING/SUSPENDED/STOPPING。
  DEBUGGING を追加する場合は enum 末尾 + `state_names[]` (fmrb_app.c:83-85) +
  `is_valid_transition()` (fmrb_app.c:102-119) の 3 点セット。さらに
  RUNNING/SUSPENDED を前提にしたウィンドウ操作ガードが fmrb_app.c の
  1794, 1831, 1894, 1914, 1938, 1991, 2054 にある。
- カーネル (Ruby) 側の監視はウィンドウリスト駆動 (`app_lifecycle.rb` の
  `check_terminated_apps`)。VM がパークしてもウィンドウが残っていれば
  「生存」扱いなので誤 reap はされない。C 側 reaper (`fmrb_app_reap`) は
  STOPPING/INIT のみ受理するため RUNNING のままパークしても回収されない。
- デバイス上コンパイル経路: `execute_mruby_script()` (fmrb_app.c:469-590)。
  `mrc_ccontext_new` は 479 行、`mrc_load_string_cxt` は 517 行。
  この間に `mrc_ccontext_filename(cc, ctx->filepath)` を挿入する。
- キュー: 生の xQueue* は使わず `fmrb_rtos.h` の `fmrb_queue_create/send/receive/delete`
  マクロを使う (プロジェクト規約)。タスク生成は `fmrb_task_create_ex`。
- メモリ: debugd は OS 側なので `fmrb_sys_malloc`。固定小容量はファイルスコープ static。
- 戻り値: `fmrb_err_t` (FMRB_OK=0, 負値がエラー)。
- msgpack: `components/msgpack-esp32` (msgpack-c)。エンコード例
  `main/drivers/display_p4/display_p4_task.cpp:897-909`、デコード例 同 1788-1818。
- ログ読み出し: `fmrb_log_buffer_read_lines(buf, size, max_lines, &read_pos)`。
  差分検出 + オーバーラン対応の実装例は `main/drivers/ble/ble_task.c:820-844`。
- Linux でのソケット前例は `main/drivers/usb/usb_task_linux.c` (POSIX AF_UNIX)。
  TCP サーバの前例はないが、同スタイル (Linux 専用 TU + 素の POSIX socket) を踏襲する。
- Linux 専用ソースは `main/CMakeLists.txt` の `COMPONENT_LINUX_SRCS` (12-14 行、
  106-108 行の else 分岐) に追加する。
- debugd 起動地点: `fmrb_os_init()` (boot.c:402-459) の `fmrb_kernel_start()` 呼び出し
  (435 行) 後。fmrb_msg / fmrb_app 初期化済みの地点。
- docker compose (family-mruby ルート) は現状 TCP ポート公開なし
  (Unix ソケット + ipc: host 構成)。デバッグポートの公開設定を追加する必要がある。

## 3. 全体構成 (成果物一覧)

```
fmruby-core/
  lib/add/family_mruby_linux.rb            [変更] MRB_USE_DEBUG_HOOK 追加
  components/picoruby-esp32/
    mruby_abi_defines.cmake                [変更] 同 define の ABI 同期 (linux 限定条件付き)
  main/prebuild_scripts/
    compile_ruby_to_bytecode.cmake         [変更] mrbc に -g (+ 行オフセット表生成)
  main/app/fmrb_app.c                      [変更] mrc_ccontext_filename 設定
  main/drivers/debug/                      [新規] デバッグコア + トランスポート
    fmrb_debugd.h                          公開 API (init のみ)
    fmrb_debugd.c                          debugd タスク・セッション・コマンド分配
    fmrb_debug_ctx.h / .c                  per-VM デバッグコンテキストと hook 本体
    fmrb_debug_proto.h / .c                msgpack プロトコルのエンコード/デコード
    fmrb_debug_transport.h                 トランスポート vtable 定義
    fmrb_debug_transport_tcp.c             TCP 実装 (Linux 専用 TU)
  main/boot/boot.c                         [変更] debugd 起動
  main/CMakeLists.txt                      [変更] ソース登録 (共通 + Linux 分岐)
  tool/debug/                             [新規] ホスト側
    fmrb_dbg_client.py                     Phase 1: CLI テストクライアント兼ライブラリ
    fmrb_dap_adapter.py                    Phase 2: DAP アダプタ
  doc/vm_remote_debug_protocol.md          [新規] プロトコル仕様 (実装と同時に確定)

family-mruby/ (ルートリポジトリ)
  docker-compose.yml (または override)     [変更] core サービスに 5555/tcp 公開
  vscode-fmrb-debug/                       [新規] Phase 2: 最小 VSCode 拡張
```

設計上の判断 (design.md からの差分・確定):

1. VM 列挙・PID 解決は `fmrb_app_*` API のみで行い、picoruby submodule への
   パッチ (task_hal.c アクセサ) は行わない。
2. デバッグコアの C ソースはターゲット非依存に書き、トランスポートだけ
   Linux TU に分離する。ESP32 ビルドでは Phase 1 の間コンパイル対象に含めない
   (COMPONENT_LINUX_SRCS のみに登録)。ESP32 対応時に共通側へ移す。
3. プロセス状態 DEBUGGING の追加は Phase 1 では行わない (パーク中も RUNNING の
   まま)。調査の結果、reaper はウィンドウ駆動 + STOPPING 限定なので誤検知しない。
   suspend/kill との競合はデバッグ中フラグで debugd 側が防ぐ。
   状態表示 (ps) 上の区別が欲しくなった時点で別途追加する。

## 4. Phase 0: 基盤有効化と PoC

目的: フックと行番号情報が Linux ビルドで実際に機能することを、デバッグコアを
書き始める前に単体で証明する。

### 4.1 ビルド設定変更

1. `lib/add/family_mruby_linux.rb` の defines 群に追加:
   ```ruby
   conf.cc.defines << 'MRB_USE_DEBUG_HOOK'
   ```
2. `components/picoruby-esp32/mruby_abi_defines.cmake` の
   `fmrb_add_mruby_abi_defines()` に同 define を追加。
   ESP32 ビルドに影響させないため `IDF_TARGET STREQUAL "linux"` 条件付きにする。
   マクロの実装形態を確認し、条件分岐が書けない構造なら
   「全ターゲット有効化 + ESP32 側 build_config にも追加」に切り替える
   (hook NULL 時のコストは分岐 1 個なので許容範囲。その場合はフラッシュ増を計測)。
3. `main/prebuild_scripts/compile_ruby_to_bytecode.cmake` の 75 行・86 行の
   mrbc 呼び出しに `-g` を追加。
4. `main/app/fmrb_app.c` の `execute_mruby_script()`、`mrc_ccontext_new` (479 行)
   直後に追加:
   ```c
   mrc_ccontext_filename(cc, ctx->filepath);
   ```
5. `rake clean && rake build:linux` で通ることを確認。

### 4.2 PoC (使い捨てテストフック)

`main/drivers/debug/` に最小の PoC を書く (Phase 1 で fmrb_debug_ctx.c に発展させる)。

- boot.c から起動する PoC タスクが 3 秒後に `fmrb_app_get_context_by_id()` で
  カーネル VM (PROC_ID_KERNEL) の `mrb` を取得し、`mrb->code_fetch_hook` に
  テストフックを設定する。
- テストフックは「初回の 100 命令だけ file:line をログ出力」する。
  `pc - irep->iseq` 変換と `mrb_debug_get_position()` の動作、
  組み込みアプリ (bytecode, mrbc -g 経由) とデバイス上コンパイル
  (mrc_ccontext_filename 経由) の両方で行番号が取れることを確認する。
- `irep->lv` と `irep->nlocals` もダンプし、変数名が取れるか確認する
  (取れなければ `keep_lv` の設定方法を調査して対処 — 本 Phase の主要検証項目)。

### 4.3 計測

- hook NULL 時のオーバーヘッド: 適当な Ruby ベンチ (ループ 1000 万回程度) を
  MRB_USE_DEBUG_HOOK 有効/無効ビルドで比較。
- hook 装着時 (armed=false 相当の早期 return) のオーバーヘッド。
- `-g` によるバイトコードサイズ増: `main/prebuild_scripts/*/mrb/*.c` の
  生成物サイズを前後比較 (ESP32 のフラッシュ判断材料として記録)。

### 4.4 完了条件

- Linux ビルドで、組み込みアプリ・オンデバイスコンパイルの両経路の VM から
  file:line が取得できるログが出る。
- 変数名 (`irep->lv`) の取得可否が判明している。
- 計測値が doc (本書の追記 or 別メモ) に記録されている。
- PoC コードは Phase 1 のコードに置き換えて削除する (Legacy を残さない)。

## 5. Phase 1: デバッグコア + TCP トランスポート

### 5.1 プロトコル仕様

フレーミング (TCP): `[u32 length (BE)] [msgpack body]`。length は body のみのサイズ。
最大フレーム長 4KB (デバイス側受信バッファ上限。超過はエラー応答)。

メッセージ: msgpack 配列。

```
request : [0, seq(u16), cmd(str), payload(map|nil)]
response: [1, seq(u16), err(int, FMRB_OK=0), payload(map|nil)]
event   : [2, 0,       name(str), payload(map)]
```

seq はホストが採番し、response が同じ seq を返す。event は非同期 (seq=0)。

コマンド (Phase 1 実装分):

| cmd | payload (req) | payload (resp) |
|---|---|---|
| `version` | - | `{proto:1, fw:str}` |
| `ps` | - | `{apps:[{pid,name,state,vm,mem_used,mem_total,stack_hw}]}` |
| `attach` | `{pid}` | `{ok}` — hook 装着 + dctx 割当 |
| `detach` | `{pid}` | `{ok}` — BP 全解除、パーク中なら continue して hook 解除 |
| `bp_set` | `{pid,file,line}` | `{bp_id}` |
| `bp_clear` | `{pid,bp_id}` (bp_id=-1 で全解除) | `{ok}` |
| `pause` | `{pid}` | `{ok}` (停止は stopped イベントで通知) |
| `continue` | `{pid}` | `{ok}` |
| `step_in` / `step_over` / `step_out` | `{pid}` | `{ok}` |
| `stack_trace` | `{pid, max:int}` | `{frames:[{idx,func,file,line}]}` |
| `frame_vars` | `{pid, frame:int}` | `{vars:[{name,type,value:str,truncated:bool}]}` |
| `log_read` | `{pos:u32, max_lines}` | `{lines:str, pos:u32, overrun:bool}` |
| `kill` / `stop` / `suspend` / `resume` | `{pid}` | `{ok}` — 既存 fmrb_app API を叩く |
| `spawn` | `{path:str}` | `{pid}` |

イベント:

| event | payload |
|---|---|
| `stopped` | `{pid, reason:"breakpoint"|"step"|"pause", file, line, bp_id?}` |
| `resumed` | `{pid}` |
| `exited` | `{pid}` (アプリ終了検知。Phase 1 では ps ポーリングで代替可、余力があれば) |
| `output` | `{lines:str}` (log_stream 有効時。Phase 1 では log_read ポーリングでも可) |

制約 (Phase 1): 同時デバッグセッションは 1 クライアント、attach できる VM は
最大 `FMRB_DEBUG_MAX_ATTACH` (=4) 個。

仕様は実装と並行して `doc/vm_remote_debug_protocol.md` に清書し、以後そちらを正とする。

### 5.2 per-VM デバッグコンテキスト (fmrb_debug_ctx)

```c
typedef struct {
  bool          in_use;
  int           pid;
  mrb_state    *mrb;            // ctx->mrb (attach 時に解決)
  uint32_t      gen;            // fmrb_app_task_context_t.gen (PID 再利用検知)
  // --- hook が読むフィールド (VM タスクが読み、debugd が書く) ---
  volatile bool armed;          // false なら hook は即 return (高速パス)
  volatile bool pause_req;
  volatile int  step_mode;      // NONE / IN / OVER / OUT
  const mrb_callinfo *step_ci;  // step over/out の基準 ci
  fmrb_dbg_bp_t bps[FMRB_DEBUG_MAX_BP];   // {id, line, file[64], enabled}
  // --- 停止中の状態 ---
  volatile bool stopped;
  const char   *stop_file;      // 直近停止位置 (多重ヒット防止にも使用)
  int32_t       stop_line;
  const mrb_irep *stop_irep;
  // --- パーク通信 ---
  fmrb_queue_t  cmd_q;          // debugd -> hook (パーク中コマンド)
  fmrb_queue_t  resp_q;         // hook -> debugd (コマンド応答)
} fmrb_debug_ctx_t;
```

- 実体はファイルスコープ static 配列 `s_dctx[FMRB_DEBUG_MAX_ATTACH]` (malloc 不要)。
- `mrb_state` から dctx への逆引き: attach 数が高々 4 なので線形探索で十分
  (`mrb` ポインタ一致)。hook 冒頭で 1 回だけ引く。
- cmd_q / resp_q の要素は固定長の小さなコマンド構造体
  (`{op, arg1, arg2}` / `{err, msgpack片へのポインタ+長さ}`)。msgpack の
  組み立てはパークループ内 (VM タスク側) で行い、バッファは dctx 内の
  static 領域 (per-dctx 2KB 程度) を使う。

#### hook 本体 (fmrb_debug_ctx.c)

```
fmrb_debug_code_fetch_hook(mrb, irep, pc, regs):
  dctx = lookup(mrb);  if (!dctx || !dctx->armed) return;      // 高速パス
  if (!irep->debug_info) return;                                // 行情報なし irep はスキップ
  pc_off = pc - irep->iseq;
  line = mrb_debug_get_line(mrb, irep, pc_off);  if (line < 0) return;
  file = mrb_debug_get_filename(mrb, irep, pc_off);

  stop_reason = NONE;
  if (dctx->pause_req)                        stop_reason = PAUSE;
  else if (step 条件成立)                      stop_reason = STEP;    // 5.2.1
  else if (行頭 && (file,line) が BP に一致
           && (irep,line) != 前回停止位置)      stop_reason = BREAKPOINT;
  if (stop_reason == NONE) return;

  park(dctx, stop_reason, irep, pc_off, regs);
```

- 行頭判定は mrdb の `check_start_pc_for_line` を移植:
  `pc_off > 0 && mrb_debug_get_line(mrb, irep, pc_off-1) == line` なら行頭でない。
- BP の file 照合は **basename 比較** (デバイス側パスとコンパイル時パスの揺れを吸収。
  厳密化はホスト側 pathMappings の責務)。

#### step 判定 (5.2.1)

mrdb 方式を踏襲:

- `STEP_IN`: (file,line) が停止時と異なれば停止。
- `STEP_OVER`: 加えて `(intptr_t)mrb->c->ci > (intptr_t)dctx->step_ci` (より深い) 間は素通り。
- `STEP_OUT`: `(intptr_t)mrb->c->ci < (intptr_t)dctx->step_ci` になったら次の行頭で停止。
- 注意: mruby-task により同一 `mrb_state` 内で複数 Ruby task が切り替わる。
  step 基準の context (`mrb->c`) が停止時と異なる場合は step 判定をスキップする
  (別 task の命令で誤停止しない)。dctx に `step_c` (`struct mrb_context*`) も保存する。

#### パーク (5.2.2)

```
park(dctx, reason, irep, pc_off, regs):
  dctx->stopped = true; 停止位置を dctx に記録
  debugd のイベントキューへ stopped 通知を送る (fmrb_queue_send, ノンブロック)
  loop:
    cmd = fmrb_queue_receive(dctx->cmd_q, timeout=500ms)
    if (timeout): dctx->armed が false (detach) なら脱出; continue
    switch (cmd.op):
      STACK_TRACE: mrb->c->cibase..ci を降順走査して msgpack 化 -> resp_q
      FRAME_VARS : 指定フレームの irep->lv / regs を安全整形して msgpack 化 -> resp_q
      CONTINUE   : step_mode=NONE で脱出
      STEP_*     : step_mode/step_ci/step_c を設定して脱出
  dctx->stopped = false; pause_req = false
  debugd へ resumed 通知
```

- パーク中は当該 VM (FreeRTOS タスク) がブロッキング receive で眠る。
  同一 mrb_state 上の全 Ruby task が止まるのは仕様どおり。
- FRAME_VARS の値整形は `mrb_inspect` を呼ばず型別フォーマッタで行う:
  - Fixnum/Float/Symbol/true/false/nil: そのまま文字列化
  - String: 先頭 64 バイト + truncated フラグ (エスケープは最小限)
  - Array/Hash: `Array(len=N)` / `Hash(size=N)` のようなサマリのみ
  - その他オブジェクト: `#<ClassName>` (`mrb_obj_classname`)
  - GC を誘発する API (文字列生成) はパーク中の VM 自タスクなので呼んで良いが、
    arena 溢れ防止に `mrb_gc_arena_save/restore` で囲む
- FRAME_VARS のフレーム解決: stack_trace と同じ走査で frame idx -> ci を再特定し、
  regs は `ci->stack`、irep は `ci->proc->body.irep` を使う (停止フレーム以外も参照可)。

#### attach / detach (debugd タスク側から実行)

- attach:
  1. `fmrb_app_get_context_by_id(pid)` で ctx 取得 (RUNNING/SUSPENDED のみ受理)、
     `vm_type == MRUBY` を確認
  2. dctx スロット確保、cmd_q/resp_q 生成、フィールド初期化、`armed=false`
  3. `mrb->code_fetch_hook = fmrb_debug_code_fetch_hook;` を書く
     (関数ポインタ 1 ワードの store。VM 側は NULL か有効値のどちらかを読むだけ
     なので実用上安全。dctx を完全に初期化してから hook を書く順序を厳守)
  4. `armed = true`
- detach: `armed=false` -> パーク中なら CONTINUE 投入 -> stopped 解除を待つ ->
  `mrb->code_fetch_hook = NULL` -> キュー削除、スロット解放。
- アプリ終了との競合: `gen` を保存し、コマンド処理前に毎回
  `fmrb_app_get_context_by_id(pid)` の再取得と gen 一致を確認する。
  不一致 (終了・再利用) なら dctx を強制解放し `exited` イベントを送る。
  ※ VM 終了パス (`destroy_vm`) はパーク中には走らない (同一タスクのため) が、
  `fmrb_app_kill` による外部削除には注意が必要 → debug attach 中の pid への
  kill/suspend は debugd 経由コマンドでは拒否 (FMRB_ERR_BUSY)。カーネル UI 側
  からの kill は当面「デバッグ対象は閉じない」運用とし、リスクに記載。

### 5.3 debugd タスク (fmrb_debugd.c)

- 起動: `fmrb_debugd_init()` を boot.c の `fmrb_kernel_start()` 成功後 (435 行以後) に
  呼ぶ。`fmrb_task_create_ex(debugd_main, "debugd", 8192, NULL, prio=3)`。
  Linux 専用の間は呼び出しを `#ifdef CONFIG_IDF_TARGET_LINUX` で囲む。
- debugd_main のループ (単一タスク、select ベース):
  ```
  transport->init() (listen)
  loop:
    select(listen_fd, client_fd, timeout=50ms)
    - accept: 既存クライアントがいれば新規を拒否 (1 セッション)
    - client 受信: フレーム組み立て -> proto デコード -> ディスパッチ
    - イベントキュー (dctx/hook から) を poll: stopped/resumed/exited -> event 送信
    - 切断検知: 全 dctx を detach してクリーンアップ (デバッガ死亡時に VM を止めない)
  ```
- コマンドディスパッチ:
  - グローバル系 (version/ps/attach/detach/bp_set/bp_clear/pause/continue/step/
    kill/stop/suspend/resume/spawn/log_read): debugd タスク内で直接処理。
    bp_set/bp_clear/pause/step/continue は dctx のフィールド書き換え
    (パーク中の continue/step は cmd_q 投入)。
  - パーク中検査系 (stack_trace/frame_vars): cmd_q へ転送し、resp_q を
    タイムアウト付き (2s) で待って応答を中継。非停止中に来たら
    FMRB_ERR_INVALID_STATE を返す。
- イベントキュー: hook -> debugd 用の共有キュー 1 本
  (`{ev_type, pid, file, line, reason}` の固定長構造体、深さ 8)。

### 5.4 プロトコル層 (fmrb_debug_proto.c)

- msgpack-esp32 (msgpack-c) を使用。`msgpack_sbuffer` + `msgpack_packer` で
  response/event を組み、`msgpack_unpack_next` で request をパースする。
- パース結果は tagged 構造体 `fmrb_dbg_req_t` (cmd enum + 引数 union) に落とし、
  debugd 本体は msgpack を直接触らない。
- 文字列コマンド名 -> enum のテーブルはここに閉じる。

### 5.5 TCP トランスポート (fmrb_debug_transport_tcp.c)

- vtable (`fmrb_debug_transport.h`):
  ```c
  typedef struct {
    fmrb_err_t (*init)(void);
    int  (*poll)(uint8_t *buf, size_t cap, uint32_t timeout_ms); // 受信フレーム (>0) / 0 / <0
    fmrb_err_t (*send)(const uint8_t *frame, size_t len);
    bool (*connected)(void);
    void (*close_client)(void);
  } fmrb_debug_transport_ops_t;
  ```
  受信側で length プレフィクスの剥離まで行い、poll は「完全な msgpack body」単位で返す。
- 実装: POSIX socket (AF_INET, SOCK_STREAM)。`0.0.0.0:5555` で listen
  (ポートは `FMRB_DEBUG_TCP_PORT` define)。SO_REUSEADDR。ノンブロッキング +
  select。usb_task_linux.c のスタイルを踏襲。
- BLE 追加時はこの vtable の別実装を足すだけにする (debugd はトランスポート名を知らない)。

### 5.6 docker compose のポート公開

family-mruby ルートの compose 設定で core サービスに `ports: ["5555:5555"]` を追加。
override ファイル群 (`docker-compose.wsl.yml` 等) の構成を確認し、既定の起動経路
(dev_run_check.sh が使うもの) で有効になる場所に入れる。
WSL2 -> Windows は localhost フォワードで届くため、Phase 2 の VSCode (Windows) からも
`localhost:5555` で接続できる。

### 5.7 CMake 登録

`main/CMakeLists.txt`:
- `COMPONENT_LINUX_SRCS` に `drivers/debug/fmrb_debugd.c`, `fmrb_debug_ctx.c`,
  `fmrb_debug_proto.c`, `fmrb_debug_transport_tcp.c` を追加。
- `INCLUDE_DIRS` に `drivers/debug` を追加。
- msgpack 依存 (`msgpack-esp32`) が linux ビルドの REQUIRES に含まれているか確認、
  なければ追加。

### 5.8 Python テストクライアント (tool/debug/fmrb_dbg_client.py)

- 標準ライブラリ + `msgpack` パッケージ (or `umsgpack` 同梱) のみ。
- ライブラリ部: `FmrbDebugClient` クラス (connect/request/イベントコールバック)。
  Phase 2 の DAP アダプタが import して使う。
- CLI 部: `python3 fmrb_dbg_client.py <host:port> <cmd> [args...]` +
  対話モード (`ps` / `attach 3` / `b app.rb:10` / `c` / `bt` / `vars 0` 程度)。
- スモークテストスクリプト `tool/debug/test_phase1.sh`:
  1. `tools/dev_run_check.sh --keep` でスタック起動
  2. ps -> shell アプリ (または専用テストアプリ) に attach
  3. bp_set -> fmrb_input.py で操作して BP をヒットさせる -> stopped イベント受信
  4. stack_trace / frame_vars の内容検証 -> step_over -> continue -> detach
  5. `docker compose down`

### 5.9 Phase 1 完了条件

- テストクライアントから全コマンドが機能し、test_phase1.sh が自律実行で通る。
- BP ヒット中も他アプリ・カーネル UI が動き続ける (パークが他 VM に波及しない)。
- detach / クライアント切断で VM が正常に走行再開する。
- attach していない状態でのオーバーヘッドが Phase 0 計測と同等 (劣化なし)。

## 6. Phase 2: DAP アダプタ + VSCode 拡張

### 6.1 DAP アダプタ (tool/debug/fmrb_dap_adapter.py)

- 単一プロセス。stdio で DAP、TCP で fmrb_dbg_client.py を使用。
- 実装する DAP リクエスト:
  `initialize, attach, disconnect, threads, setBreakpoints, configurationDone,
  pause, continue, next, stepIn, stepOut, stackTrace, scopes, variables, source`
- launch.json スキーマ:
  ```json
  { "type": "fmrb", "request": "attach",
    "host": "localhost", "port": 5555,
    "app": "shell",              // 名前 or pid で対象選択
    "pathMappings": [{ "device": "/app/", "local": "${workspaceFolder}/fmruby-core/flash/app/" }] }
  ```
- スレッドモデル: 1 VM = 1 DAP thread。attach 対象 VM のみ表示 (Phase 2 では
  単一 VM attach。マルチ attach は将来)。
- パス変換: pathMappings + basename フォールバック。
- 行オフセット補正 (combined.rb): `compile_ruby_to_bytecode.cmake` の連結処理 (64 行)
  を拡張し、`<name>_combined.map.json` (`[{file, start_line, lines}]`) を生成する。
  アダプタが起動時に読み込み、combined 行番号 <-> 原本 (file,line) を双方向変換する。
  デバイス側は combined の行番号のまま扱う (デバイス側に補正は入れない)。
- stopped/exited/output イベントを DAP イベントへ中継。

### 6.2 VSCode 拡張 (family-mruby/vscode-fmrb-debug/)

- 最小構成: `package.json` (contributes.debuggers, `extensionKind: ["ui"]`) +
  数十行の activate (アダプタ起動設定は DebugAdapterExecutable で
  `python3 fmrb_dap_adapter.py`)。TypeScript ではなく素の JS で開始して良い。
- Windows 側 Python (bleak 不要、Phase 2 は標準 + msgpack のみ) で動かす。
  WSL 内 sim へは localhost:5555。
- パッケージング (vsix) は Phase 2 では不要。開発モード (F5 / --extensionDevelopmentPath)
  で動けば完了。

### 6.3 Phase 2 完了条件

- VSCode 上で: attach -> エディタ余白の BP 設定 -> ヒットで停止線表示 ->
  変数ペインにローカル変数 -> ステップ実行 -> continue -> disconnect が動く。
- combined.rb アプリ (system_desktop 等) で原本ファイル上の BP が正しく効く。

## 7. 実装順序と作業ブレークダウン

依存関係順。各項目は独立にビルド可能な単位。

1. [P0] ビルド設定 4 点 (define x2, -g, ccontext_filename) + ビルド確認
2. [P0] PoC フック + 行番号/変数名検証 + 計測 -> 結果を doc に追記、PoC 削除
3. [P1] fmrb_debug_proto (msgpack エンコード/デコード) — 単体でテスト可能
4. [P1] fmrb_debug_transport_tcp + docker compose ポート公開 —
   エコーレベルで疎通確認 (python で接続)
5. [P1] fmrb_debugd タスク + version/ps/log_read/kill 等の非 hook 系コマンド
6. [P1] fmrb_debug_ctx (attach/detach/hook/BP/park) + stack_trace/frame_vars
7. [P1] step 系 + pause + イベント通知
8. [P1] fmrb_dbg_client.py + test_phase1.sh (自律検証)
9. [P2] combined.map.json 生成 (cmake 拡張)
10. [P2] fmrb_dap_adapter.py
11. [P2] VSCode 拡張 + エンドツーエンド確認
12. ドキュメント: vm_remote_debug_protocol.md 清書、design.md のステータス更新

## 8. リスク・要検証事項 (Phase 0-2 スコープ)

| 項目 | 内容 | 対応 |
|---|---|---|
| `irep->lv` が空 | mrc の `keep_lv` 未設定だと変数名が取れない | Phase 0 の PoC で最優先検証。ダメなら mrc_ccontext の keep_lv 設定 (デバイス側) / mrbc オプション調査 |
| ABI define の条件付き追加 | mruby_abi_defines.cmake がターゲット分岐できる構造か未確認 | Phase 0 で確認。不可なら全ターゲット有効化に方針変更 (フラッシュ/性能を計測して判断) |
| attach 中の外部 kill | fmrb_app_kill はタスクを直接 delete するため、パーク中に kill されると dctx が宙に浮く | debugd 経由の kill は拒否。gen チェックで検知して dctx 強制解放。恒久対処 (DEBUGGING 状態導入) は ESP32 対応時に再検討 |
| hook ポインタの装着レース | 実行中 VM への 1 ワード store | dctx 完全初期化 -> hook 書き込みの順序で実用上安全。問題が出たら「アプリ起動時に常設 hook + armed フラグ」方式へ変更 (構造は変わらない) |
| パーク中の GC / arena | frame_vars で文字列生成する | 自タスクコンテキストなので原理上安全。arena save/restore で溢れ防止 |
| step と mruby-task の干渉 | 同一 mrb_state 内の task 切替で step 誤判定 | step_c (context) 一致チェックを入れる (5.2.1) |
| WDT / reaper | パーク中の長時間ブロック | Linux sim では IDLE 監視無効・reaper はウィンドウ駆動で影響なし (調査済)。ESP32 実機では再確認 |
| msgpack ライブラリの linux ビルド | msgpack-esp32 が linux target でビルドされるか | 手順 3 の時点で確認。不可なら手組みエンコード (fmrb_app.c 前例) へフォールバック |
| 5555 ポート衝突 | WSL 側の他サービス | ポートは define + compose で変更可能にしておく |

## 9. Phase 3 以降への引き継ぎ設計 (参考)

- ESP32 対応: fmrb_debugd.c / fmrb_debug_ctx.c / fmrb_debug_proto.c は
  ターゲット非依存に書いてあるため、COMPONENT_SRCS への移動 +
  BLE トランスポート実装 (ble_task.c の GATT サービス追加、COBS+CRC32 フレーミング) +
  ESP32 側 build_config への define 追加が主作業。
- フレーミング層が TCP (length prefix) と BLE (COBS) で異なるのは
  transport ops の内側に閉じている。
- evaluate は park ループに EVAL コマンドを足し、mrc でコンパイルして
  停止フレームの binding で実行する (mruby-binding が vendored 済み)。
