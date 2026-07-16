# PicoRuby VM リモートデバッグ検討 (Bluetooth / VSCode)

作成日: 2026-07-15 (追加調査反映: 同日)
ステータス: 検討ドラフト

## 1. 目的

Family mruby 上で RTOS タスク単位に起動している PicoRuby (microruby/mruby) VM を、
Windows 上の VSCode から Bluetooth (BLE) 経由でデバッグできるようにする。

やりたいこと:

- VM の制御 (起動/停止/サスペンド/レジューム/kill)
- 状態監視 (VM 一覧、実行状態、メモリ使用量、ログストリーム)
- ブレークポイント設定、実行中断 (break)、ステップ実行
- 停止中のスタックトレース・変数の参照 (可能なら evaluate)

VSCode 側は Debug Adapter Protocol (DAP) に乗せるのが自然。デバイス側に DAP
(JSON, 大きめのメッセージ) を直接実装するのは重いので、**デバイス側は軽量
独自プロトコル + ホスト側で DAP に変換する** 構成を推奨する。

## 2. 現状アーキテクチャの整理 (調査結果)

### 2.1 VM 実行基盤

- VM は mruby (`mrb_state`)。PicoRuby の prism ベース microruby + `mruby-task`
  協調スケジューラ。mruby/c ではない。
- アプリ 1 個 = FreeRTOS タスク 1 個 = `mrb_state` 1 個。
  - エントリ: `app_task_main()` — `fmruby-core/main/app/fmrb_app.c:821`
  - コンテキスト: `fmrb_app_task_context_t` — `components/fmrb_common/include/fmrb_app.h:53`
    (`mrb_state*`, FreeRTOS handle, `app_id`(PID), `state`, `should_exit`, Estalloc heap)
  - プール: `g_ctx_pool[FMRB_MAX_APPS]` (PSRAM)
- VM 生成: `create_vm_mruby()` (fmrb_app.c:345) → `mrb_open_with_custom_alloc()`
  → `mrb_hal_task_register_vm(ctx->mrb)` (fmrb_app.c:363)。
  **`mrb_hal_task_register_vm` が全 VM 登録の単一チョークポイント。**
- 実行: `mrc_load_string_cxt` (ソース) / `mrb_read_irep` (バイトコード) →
  `mrc_create_task` → `mrb_task_run(ctx->mrb)` (fmrb_app.c:568) がスケジューラループ。
- **VM レジストリ**: `g_tick_manager.vms[MRB_TASK_MAX_VMS]`
  (`{mrb_state*, pending_ticks, tick_countdown, TaskHandle_t}`) —
  `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c`。
  全 VM とその FreeRTOS タスクをここで列挙できる (mutex 保護)。
- 既存ライフサイクル API (fmrb_app.h:174-203):
  `fmrb_app_spawn / kill / stop / suspend / resume / reap / ps / get_context_by_id`。
  `fmrb_app_ps()` は状態 + スタック高水位 + メモリ統計を返す (監視系はほぼ既製)。
- タスク→VM 逆引き: `fmrb_current()` (FreeRTOS TLS)。
- 割り込み系: `picoruby-machine/ports/esp32/machine.c` に `sigint_status` あり
  (Ctrl-C 相当のシグナル機構が既にある)。

### 2.2 mruby VM のデバッグ機能サポート状況 (詳細調査済み)

vendored mruby は **4.0.0** (2026-04-20 リリース、`4.0.0-2061-gf56d44ecc` の
master 追従。microruby 統合 = prism コンパイラ + mruby-task が upstream 入り後)。
場所: `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/`。

**ソースレベルで存在するもの (デバッガの材料はほぼフル装備):**

| 機能 | 状態 |
|---|---|
| `code_fetch_hook` (全命令フック) | あり (`include/mruby.h:359`, `vm.c:1567`)。`MRB_USE_DEBUG_HOOK` ガード付きで**現行ビルドでは未定義=コンパイルアウト** |
| 行番号 debug_info | 完全にあり。`src/debug.c`: `mrb_debug_get_filename/get_line/get_position`。packed_map 形式で省メモリ格納 (`include/mruby/debug.h`) |
| ローカル変数名 (`irep->lv`) | **mrc (prism コンパイラ) が生成する** (`mruby-compiler/src/codegen.c:590`)。変数名付きフレーム変数表示が可能 |
| バックトレース | `src/backtrace.c` (例外時に既に使用中) |
| CLI デバッガ mrdb (`mruby-bin-debugger`) | ツリーに丸ごとあり。breakpoint 管理 `apibreak.c`、step、print、list 実装済み |
| `mruby-binding` / `mruby-proc-binding` | vendored 済み。停止コンテキストでの `evaluate` の土台 |
| 実行時コンパイラ (mrc) | デバイス上に既にある → eval 式のコンパイルも可能 |

**使えない・欠けているもの:**

- **`OP_DEBUG` / `debug_op_hook` は死んでいる**: VM 側 (vm.c:3848) にはあるが、
  mrc (prism) の codegen は `OP_DEBUG` を一切生成しない (grep でゼロ件)。
  旧 mruby コンパイラ専用。**フックは `code_fetch_hook` 一本で設計する。**
- **mrdb はホスト CLI 専用**: 自分で `mrb_open` してスクリプトを実行する対話型
  ツール。動作中 VM へのアタッチ・リモートプロトコル・マルチ VM 対応は無い。
  picoruby / Family mruby のビルドにも組み込まれていない。
  ただし `apibreak.c` (file+line→irep 照合の BP 判定ロジック) は流用元として有用。
- **マルチ VM・スレッド安全性はデバッガ側の責務**: フックは `mrb_state` 単位で
  1 本。mruby-task のタスク切替との整合はこちらで設計する (→ パーク方式)。
- PicoRuby として追加されたデバッグ機能は特に無く、フットプリント優先で
  全部オフの状態。`MRB_USE_DEBUG_HOOK` は現行ビルド設定
  (`build_config/family_mruby_esp32.rb`, `family_mruby_linux.rb`,
  `lib/add/family_mruby_esp32p4.rb`) では未定義 → 有効化が必要。

### 2.3 デバッグ情報 (行番号) の現状 — ★要対応

- **ビルド時プリコンパイル (組み込みアプリ)**: `main/prebuild_scripts/compile_ruby_to_bytecode.cmake`
  は `bin/mrbc -B<sym>` で **`-g` なし** → debug_info (行番号) が落ちている。
  mrbc は `-g` (`MRC_DUMP_DEBUG_INFO`) をサポート済み (`mruby-bin-mrbc/tools/mrbc/mrbc.c:218`)。
- **デバイス上コンパイル (flash/app の .rb)**: `fmrb_app.c:479` の `mrc_ccontext_new`
  後に **filename を設定していない** → codegen はファイル名がある場合のみ
  debug_info を確保する (`mruby-compiler/src/codegen.c:610`) ため、
  これも行番号なしの可能性が高い。`mrc_ccontext_filename(cc, path)` 相当の設定が必要。
- 組み込みアプリは `<name>_combined.rb` (subdir/*.rb + main.rb の連結) を
  コンパイルしているため、**元ファイル⇔行番号のマッピング補正**が必要
  (連結時のオフセット表を生成するか、DAP アダプタ側で変換する)。

### 2.4 通信経路

- **BLE は実装済み**: `main/drivers/ble/ble_task.c` (NimBLE)。
  GATT ファイル転送サービス (RX write / TX notify、COBS + CRC32 + JSON フレーミング、
  デバイス名 "FamilyMruby")。**このパターンをそのまま debug サービスに流用できる。**
  - S3: ネイティブ NimBLE。P4: C6 コンパニオンへ esp-hosted vHCI
    (`config/sdkconfig.defaults.p4`: `CONFIG_BT_NIMBLE_ENABLED=y`,
    `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`)。
- Wi-Fi + HTTP/WebSocket サーバも既存 (`main/drivers/remote_desktop/rd_http.c`)。
  BLE 帯域が苦しい場合の代替/併用トランスポートになる。
- 家内共通フォーマットは msgpack (`components/msgpack-esp32`, `fmrb_msg`)。
- ログ: `fmrb_log_buffer_read_lines()` (`components/fmrb_log/fmrb_log_buffer.h`)
  でリングバッファをプログラム読み出し可能 → ログストリームに直結できる。

### 2.5 Linux シミュレーション

- 両リポジトリとも `rake build:linux` + docker compose で動作。
  ヘッドレス検証ハーネス (`tools/dev_run_check.sh`, `fmrb_screenshot.py`,
  `fmrb_input.py`) があり、**デバッガのプロトタイプは Linux sim 上で
  TCP トランスポートを使って自律検証まで回せる。**

## 3. 全体アーキテクチャ (提案)

```
[VSCode (Windows)]
   │ DAP (stdio)
   ▼
[fmrb-debug 拡張 + DAP アダプタ]  … Windows 側で動作 (extensionKind: ui)
   │ 独自デバッグプロトコル (msgpack-RPC 風)
   │   ├─ BLE GATT (Windows: WinRT / Python bleak)   ← 本命
   │   └─ TCP (Linux sim / Wi-Fi 時)                  ← 開発用・代替
   ▼
[デバイス: fmrb_debugd (新規 FreeRTOS タスク)]
   ├─ VM 列挙・制御: g_tick_manager / fmrb_app_* API
   ├─ ログ: fmrb_log_buffer_read_lines
   └─ per-VM デバッグコンテキスト
        └─ code_fetch_hook 内で break 判定 → VM タスク自身が hook 内で停止し
           コマンドサービスループに入る (パーク方式)
```

設計上の重要判断:

1. **DAP はホスト側で終端する。** デバイス側は msgpack ベースの小さな
   コマンドプロトコルのみ。BLE の MTU/帯域と ESP32 の RAM に適合させる。
2. **break したら VM タスク自身が hook 内で止まり、そのタスク自身が
   inspect コマンドを処理する (パーク方式)。**
   別タスクから `mrb_state` を触るとスレッド安全性がない
   (GC・スタック伸長と競合) ため、停止中の状態参照は必ず当該 VM タスクの
   コンテキストで行う。debugd はコマンドを per-VM キュー (`fmrb_msg` の
   新 type か専用キュー) に転送するだけにする。
3. **フックは attach 時のみインストールする。** `MRB_USE_DEBUG_HOOK` は
   全 opcode に分岐を 1 個追加する程度だが、hook 関数ポインタが NULL なら
   呼ばれないので、非 attach 時のオーバーヘッドはほぼゼロにできる。
4. **トランスポートは抽象化する** (`debug_transport_ops` 的な vtable)。
   Linux sim = TCP、実機 = BLE GATT (+ 将来 WebSocket)。プロトコル層以上は共通。

## 4. デバッグプロトコル設計 (デバイス⇔ホスト)

フレーミング:

- BLE: 既存 ble_task.c と同じ COBS + CRC32。GATT 特性は新規
  「debug service」(RX: write, TX: notify) を追加。MTU 越えは
  既存のフラグメント方式に倣う。
- TCP: 4byte 長プレフィクス + msgpack。

メッセージ: msgpack 配列 `[type, seq, cmd, payload]`
(type: request/response/event)。

| 分類 | コマンド | 内容 |
|---|---|---|
| セッション | `attach` / `detach` | 対象 PID への hook 装着/解除 |
| 監視 | `ps` | `fmrb_app_ps()` 相当 (PID, 名前, 状態, mem, stack) |
| 監視 | `log_read` / `log_stream` | fmrb_log_buffer の読み出し/push 通知 |
| 制御 | `spawn` / `kill` / `stop` / `suspend` / `resume` | 既存 API を叩く |
| 実行制御 | `bp_set` / `bp_clear` (file, line) | ブレークポイント表 |
| 実行制御 | `pause` / `continue` / `step_in` / `step_over` / `step_out` | |
| 検査 | `stack_trace` | `mrb->c->ci` 走査 + `mrb_debug_get_filename/line` |
| 検査 | `frame_vars` (frame_idx) | `irep->lv` のシンボル + レジスタ値を安全 inspect |
| 検査 | `evaluate` (expr, frame) | (後期フェーズ) 停止コンテキストで mrc コンパイル実行。vendored 済みの `mruby-binding` / `mruby-proc-binding` が土台に使える |
| イベント | `stopped` (reason, pid, file, line) / `exited` / `output` | デバイス→ホスト通知 |

## 5. デバイス側の実装内容

### 5.1 ビルド設定 (fmruby-core)

- `lib/add/family_mruby_*.rb` に `MRB_USE_DEBUG_HOOK` を追加
  (mrbgem 側の編集は lib/add 配下で行う)。デバッグビルド変種にするか
  常時有効にするかは性能計測後に判断 (hook NULL 時は分岐 1 個)。
- `compile_ruby_to_bytecode.cmake` の mrbc 呼び出しに `-g` を追加
  (フラッシュ増加量を計測。debug ビルドのみ `-g` でも可)。
- `fmrb_app.c` の on-device コンパイル経路に
  `mrc_ccontext_filename(cc, アプリのパス)` を追加して debug_info を生成させる。

### 5.2 新規コンポーネント `fmrb_debugd` (仮)

- `main/drivers/debug/` あたりに debugd タスク + プロトコル処理を新設。
- VM 列挙: `g_tick_manager` にアクセサを追加 (task_hal.c は picoruby 側なので
  変更は lib/add パッチとして管理) + `fmrb_app_ps()` / `g_ctx_pool` で
  PID⇔mrb_state を突き合わせ。
- attach 時: 対象 `mrb_state` に `code_fetch_hook` を設定し、
  per-VM デバッグコンテキスト (breakpoint 表、step モード、pause 要求フラグ、
  コマンドキュー、応答キュー) を割り当てる。

### 5.3 break / step の仕組み (per-VM デバッグコンテキスト)

```
code_fetch_hook(mrb, irep, pc, regs):
  if (!dctx->armed) return;                 // 高速パス
  line = mrb_debug_get_line(mrb, irep, pc)
  if (pause要求 || bp一致(file,line) || step条件成立):
      dctx->stopped = true
      notify: stopped イベント → debugd → ホスト
      loop:                                  // ★パーク: VM タスク自身が停止
          cmd = queue_receive(dctx->cmd_q)   // stack_trace / frame_vars / evaluate
          此処で mrb_state を安全に読める (自タスクコンテキスト)
          continue/step 系コマンドで loop を抜ける
```

- step over/out は `mrb->c->ci` の深さを記録して比較する定石方式。
- BP 判定 (file+line → irep/pc 照合)・step 判定は mrdb の
  `mruby-bin-debugger/tools/mrdb/apibreak.c` / `mrdb.c` の実装が
  そのまま参考になる (mrdb 自体はホスト CLI 専用で流用不可、ロジックのみ移植)。
- 同一行での多重ヒット防止に「前回停止 (irep, line)」を保持。
- 注意点:
  - パーク中も FreeRTOS 的にはブロッキング receive なので CPU は食わない。
    WDT は IDLE 監視無効化済みなので問題になりにくいが、
    kernel 側の app 監視 (reaper) が誤検知しないか確認する。
  - `mruby-task` のタイムスライス/tick はパーク中進まないが、
    VM 内スケジューラごと止まるのはデバッガとして正しい挙動。
  - mutex (prism コンパイルロック等) を握ったまま break しないよう、
    hook 到達時点では VM はロック外である前提を確認する。
- 変数 inspect は `mrb_inspect` 直呼びだと Ruby メソッド実行で副作用・再入の
  恐れがあるため、型別の安全フォーマッタ (String/Int/Float/Symbol/Array 先頭
  N 要素…) を基本にし、full inspect はオプションにする。

### 5.4 BLE debug サービス

- `ble_task.c` に GATT サービスを 1 本追加 (file service と同型:
  RX write / TX notify、COBS+CRC32)。JSON ではなく msgpack を推奨
  (家内標準・サイズ効率)。
- スループット目安: NimBLE notify で実効数 KB/s〜10KB/s 程度
  (P4 は vHCI 経由なのでさらに要実測)。デバッグ用途 (小メッセージ +
  ログテキスト) には十分だが、ログ全量ストリームはレベルフィルタ必須。

## 6. ホスト側 (Windows / VSCode) の実装内容

### 6.1 構成の選択

VSCode は Windows 側、開発ツリーは WSL2 という現環境がポイント。
**WSL2 からは Bluetooth デバイスに直接アクセスできない**ため、
BLE を触るプロセスは必ず Windows 側で動かす必要がある。

VSCode の起動形態別の整理:

- **Windows ネイティブ起動 (PowerShell から `code`、WSL リモートなし)**:
  拡張もアダプタも Windows 上で動くので **BLE はそのまま使える**。
  `extensionKind` の細工も不要で一番シンプル。ただしワークスペースは
  `\\wsl$\...` の UNC パス越しになり、ビルドや sim 実行タスクは WSL 内で
  走らせる必要がある → 実機 BLE デバッグの初期検証にはこれが手っ取り早い。
- **Remote-WSL 接続 (普段の開発形態)**: 拡張が既定で WSL 側で動くため
  そのままでは BLE 不可。拡張を `extensionKind: ["ui"]` にして
  UI ホスト (Windows 側) で動かせば BLE が使える。常用はこちらを推奨。

推奨構成:

- **VSCode 拡張 `fmrb-debug`**: `contributes.debuggers` でデバッグタイプ
  `fmrb` を登録。`extensionKind: ["ui"]` にして Remote-WSL 接続時も
  Windows 側 (UI ホスト) で動くようにする。
- **DAP アダプタ**: 拡張から起動する単一プロセスに DAP 終端と BLE クライアント
  を同居させる。実装言語の候補:
  - **Python + bleak (BLE) + pydap 自作** — bleak は WinRT バックエンドで
    Windows BLE が安定して使える。配布は pyinstaller で exe 化。
  - TypeScript (拡張内) + `@abandonware/noble` — Windows での BLE は不安定に
    なりがちなので非推奨。
  - → **Python + bleak を推奨。**
- TCP トランスポート (Linux sim) は同じアダプタに `--transport tcp:host:port`
  として実装。WSL 内の sim へは Windows から `localhost` フォワードで届く。

### 6.2 DAP マッピング

| DAP | デバイスプロトコル |
|---|---|
| `attach` (launch.json: `{type:"fmrb", request:"attach", pid or appName}`) | `ps` → `attach` |
| `threads` | VM/アプリ一覧 (1 VM = 1 thread として見せる) |
| `setBreakpoints` | パス変換 (下記) → `bp_set` |
| `pause` / `continue` / `next` / `stepIn` / `stepOut` | 対応コマンド |
| `stackTrace` / `scopes` / `variables` | `stack_trace` / `frame_vars` |
| `evaluate` (REPL) | `evaluate` (後期フェーズ) |
| `output` イベント | `log_stream` + VM の stdout |

パス変換: デバイス上のパス (`/app/usr/foo.app.rb` 等) ⇔ ワークスペースの
`fmruby-core/flash/app/...` を launch.json の `pathMappings` で対応付ける。
combined.rb (連結コンパイル) の行オフセット補正はアダプタ内で行う
(ビルド時にオフセット表 JSON を生成するのが確実)。

## 7. 実現手順 (フェーズ計画)

### Phase 0: 基盤有効化と PoC (デバイス側だけで完結)

1. `MRB_USE_DEBUG_HOOK` 有効化 + mrbc `-g` + `mrc_ccontext_filename` 対応。
2. Linux sim で、テスト用 C コードから任意 VM に code_fetch_hook を張り、
   「特定行で停止→行番号取得→再開」ができることを確認。
3. 性能影響測定 (hook NULL 時 / armed 時)。

### Phase 1: デバッグコア + TCP トランスポート (Linux sim)

1. `fmrb_debugd` タスク、per-VM デバッグコンテキスト、パーク方式の
   break/step/stack_trace/frame_vars を実装。
2. トランスポート抽象 + TCP 実装 (Linux sim)。
3. Python 製 CLI テストクライアントで全コマンドを検証
   (既存のヘッドレス検証ハーネスで自律テスト可能)。

### Phase 2: DAP アダプタ + VSCode 拡張 (まず TCP で)

1. Python DAP アダプタ (attach/threads/breakpoints/step/stackTrace/variables)。
2. 最小 VSCode 拡張 (`contributes.debuggers`, launch.json スキーマ,
   pathMappings, 行オフセット補正)。
3. VSCode ⇔ WSL 内 Linux sim を TCP で接続してエンドツーエンド確認。
   **この時点で「VSCode からのデバッグ」自体は完成する。**

### Phase 3: BLE トランスポート

1. デバイス: BLE debug GATT サービス追加 (ble_task.c 拡張、COBS+CRC32+msgpack)。
2. ホスト: アダプタに bleak バックエンド追加 (スキャン→"FamilyMruby" 接続)。
3. S3 実機で疎通 → P4 (C6 vHCI 経由) で実測・安定化。
4. スループット/切断再接続 (reconnect + セッション再同期) の作り込み。

### Phase 4: 仕上げ

- `evaluate` (停止コンテキストでの式評価)、変数の詳細展開、
  ログレベル制御 UI、VM 制御コマンド (spawn/kill) のコマンドパレット統合。
- 拡張のパッケージング (vsix)、アダプタ exe 化、ドキュメント。

## 8. リスク・要検討事項

| 項目 | 内容 | 対応方針 |
|---|---|---|
| BLE 帯域/遅延 | P4 は esp-hosted vHCI 経由で実効帯域が未知 | Phase 3 で実測。ログ全量は流さない。WebSocket 代替を温存 |
| フックのオーバーヘッド | code_fetch は全命令で呼ばれ得る | NULL チェックの高速パス + armed フラグ。計測して debug ビルド限定も検討 |
| クロススレッド安全性 | 別タスクから mrb_state を触ると GC と競合 | パーク方式で当該タスク内処理に限定 (本設計の核) |
| debug_info のメモリ/フラッシュ増 | `-g` で irep が肥大 | 増加量を計測。ユーザアプリのみ -g などの切り分け |
| combined.rb の行ずれ | 連結コンパイルで行番号が原本とずれる | ビルド時にオフセット表を出力しアダプタで補正 |
| パーク中のシステム挙動 | reaper/監視系の誤検知、GFX 更新停止 | DEBUGGING 状態を `fmrb_proc_state_t` に追加し監視系が無視するようにする |
| picoruby 側改変の管理 | task_hal.c 等 submodule 内の変更は上書きで消える | 変更は lib/add パッチ or upstream PR 化 (doc/picoruby_upstream_pr_candidates.md の候補にもなり得る) |
| WSL2 と BLE | WSL から Bluetooth 不可 | アダプタを Windows 側で実行 (extensionKind: ui)。TCP は WSL 直結可 |

## 9. まとめ (推奨アプローチ)

- mruby 4.0 ツリーにデバッガの材料 (フック・行番号・変数名・binding・mrdb の
  BP ロジック) は全部揃っているが、完成品のリモートデバッガは無い。
  実装の実態は「有効化フラグを立てて、mrdb を参考にマルチ VM・リモート対応の
  デバッグコアを自作する」こと。
- デバイス側: `MRB_USE_DEBUG_HOOK` + code_fetch_hook の**パーク方式**デバッグコアを
  新設 `fmrb_debugd` で実装。VM 列挙・制御・ログは既存機構をそのまま流用。
- プロトコル: msgpack の小さな独自プロトコル。BLE GATT (既存 ble_task.c の
  COBS+CRC32 パターン) と TCP の 2 トランスポート。
- ホスト側: Python (bleak) 製 DAP アダプタ + 薄い VSCode 拡張
  (`extensionKind: ui` で Windows 側実行、WSL2 の BLE 制約を回避)。
- 進め方: **Linux sim + TCP で Phase 0-2 を先に完成させる** (自律検証ハーネスで
  回せる) → BLE は最後にトランスポート差し替えとして追加。
