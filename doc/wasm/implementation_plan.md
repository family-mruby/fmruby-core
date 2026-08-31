# wasm 対応 実装計画書

2026-08-28 起草。方針と調査結果は plan.md を前提とする。ここでは実装をフェーズに
分割し、各フェーズの作業項目・触るファイル・受け入れ条件を定める。
実装担当への指示書: instruction_p1.md / instruction_p2.md / instruction_p3.md /
instruction_p4a.md / instruction_p5.md (P4b/P4c は実装が先行したため
指示書なし。report/p4a.md 末尾が代わり)。

## フェーズ構成と依存関係

```
P1 FreeRTOS wasm port PoC ──────┐
P2 task_hal 自己プリエンプション ─┼─→ P4 core 統合 → P5 配信
P3 display backend 分割 (P4 実機) ┘
```

- P1〜P3 は互いに独立で、並行に進められる。
- P2・P3 は wasm と無関係に本体の改善として単独で完結する
  (P2 は kill 問題 doc/archive/app_kill_fix と同根、P3 は display_p4 の整理)。
  wasm を中断してもこの 2 つは残る。
- 最大のリスク (協調スケジューリングの成立) は P1 で先に潰す。

## ディレクトリ方針

wasm 固有物は fmruby-core 直下の `wasm/` に集める。ESP32/Linux のビルドには
一切影響を与えない (main/ や components/ に足す wasm 分岐は P4 まで最小限)。

```
wasm/
  CMakeLists.txt        独立 CMake (idf.py 非依存、emcmake で構成)
  port/                 FreeRTOS の Emscripten port (portmacro.h, port.c)
  vendor/freertos/      FreeRTOS-Kernel 本体のコピー (取得元: ビルドイメージ
                        /opt/esp/idf/components/freertos/FreeRTOS-Kernel。
                        portable/ は含めない)
  stub/                 esp_log / heap_caps / esp_random / esp_read_mac /
                        esp_restart などの代替
  backend/              display / audio / input の wasm バックエンド
  web/                  配信ページ (html/js、coi-serviceworker)
  poc/                  P1 のテストプログラム
```

rake には `wasm:poc` / `wasm:build` を追加する (rakelib/wasm.rake)。emsdk の
版は ti.rake の前例に合わせて固定する。

---

## P1: FreeRTOS wasm port 単体 PoC — 完了 (2026-08-29)

`rake wasm:poc` の 23 項目が 5 回連続で全 PASS。**撤退線は越えた**。
経過・実測値・指示から変えた点は report/p1.md。設計上、後段が前提にしてよい
確定事項は以下。

- **tick は追いつき方式**。`vPortYield()` の入口とアイドルフック
  (`esp_vApplicationIdleHook`。IDF の tasks.c が必ず呼ぶので
  `configUSE_IDLE_HOOK` は 0) の 2 箇所から `xTaskIncrementTick()` を
  1 tick ずつ呼ぶ。10 秒で実時間とのずれ 0.2〜1.2ms。
- **vendor は kernel 4 本だけでは足りない**。`xTaskCreate` と
  `vTaskSetThreadLocalStoragePointerAndDelCallback` の実体は IDF の
  esp_additions 側にあるため、freertos_tasks_c_additions.h ほか 4 ファイルも
  vendor する。
- **`vTaskDelete(other)` は使える**。pthread_cancel ではなく、
  「必ず `prvSuspendSelf()` で寝ている」性質を使って起こして抜けさせる方式で
  実装済み。ただし CPU を握ったままの相手には効かないので P2 は必要。
- **タスクは pthread のスタックで動く** (カーネルのブロックは使わない)。
  スタック指定はバイト (`portSTACK_TYPE` は uint8_t、IDF と同じ)、
  ただし下限 64KB。**`uxTaskGetStackHighWaterMark()` は意味を持たない**。
- pthread pool は node では使い切っても代償が無い (最遅の生成で 1.3ms)。
  ブラウザは P4b で測り直す。

### 目的

協調スケジューリング (プリエンプション無し) の port で、core が使う API 群が
正しく動くことを core 本体抜きで確認する。ここが通らなければ全体を中止する。

### 設計

- カーネル本体 (tasks.c / queue.c / stream_buffer.c / list.c) は IDF 同梱の
  FreeRTOS-Kernel をそのまま使う (TLS 削除コールバックが IDF 拡張のため、
  素の上流ではなく IDF のコピーを vendor する)。
- port 層のみ新規に書く:
  - 1 タスク = 1 pthread (Emscripten pthread = Web Worker + SharedArrayBuffer)。
  - 非実行タスクは自分の条件変数で待つ。骨格は既存 Linux port の
    utils/wait_for_event.c の流用 (シグナルを使わない部分だけ残す)。
  - クリティカルセクション = グローバル mutex。ISR が無いので
    portSET_INTERRUPT_MASK 系は同じ mutex に落とす。
  - tick は setitimer を使わない。タスクがブロック解除・yield する時点で
    実時間 (emscripten_get_now) との差分から `xTaskIncrementTick()` を
    必要回数呼んで追いつく方式 (tickless の追いつきモデル)。tick=1ms を維持。
  - `vPortCancelThread` (他殺) は非対応。呼ばれたらログを出して no-op
    (P2 の自己プリエンプションで不要になる前提)。
- Emscripten 固有の注意:
  - `-pthread -sPROXY_TO_PTHREAD` で main() をワーカーに逃がし、ブラウザの
    メインスレッドをブロックしない。
  - **pthread pool を事前確保する** (`-sPTHREAD_POOL_SIZE=32` 程度)。プールが
    尽きた状態での pthread_create はメインスレッドに制御が戻るまで完了しない
    ため、タスク生成が同期的に見えなくなる。Linux sim の実測タスク数
    (15〜25 本 + 余裕) で決める。
  - 各タスクのスタックは xTaskCreate の指定値をそのまま pthread の
    スタックサイズにする。

### 作業項目

1. wasm/vendor/freertos/ の取り込みと FreeRTOSConfig.h の作成
   (configNUM_THREAD_LOCAL_STORAGE_POINTERS=3、削除コールバック有効、
   configTICK_RATE_HZ=1000、unicore)。
2. wasm/port/portmacro.h + port.c の実装。
3. wasm/poc/ にテストを書く。node で実行できる形にする (ブラウザ不要で CI 可能)。

### 受け入れ条件 (poc テストで機械判定)

- タスク生成 / vTaskDelay の精度 (±10ms 程度で可) / 優先度どおりの選択
- xTaskNotifyGive / ulTaskNotifyTake のタイムアウトつき往復 (task_hal の心臓)
- キューの送受信とタイムアウト、満杯時のブロックと解除
- counting セマフォ (96 スロット) の枯渇 → 排出 → 再開 (GFX フロー制御の縮小再現)
- mutex の所有者制約、TLS 削除コールバックが vTaskDelete(自分) で呼ばれること
- MessageBuffer の送受信 (link_local が使う)
- CPU を回し続けるタスクがいても、そのタスクが yield した瞬間に上位が走ること
  (= 協調の限界の確認であって、回っている間に走らないのは仕様)

---

## P2: task_hal の自己プリエンプション化 — 完了 (2026-08-29)

`FMRB_TASK_SELF_TICK=1` で mruby_tick タスク無しの構成が成立。タイムスライスは
両モードとも 50ms、`sleep_ms` の精度も同じ。既定ビルドは追加コードが 1 行も
コンパイルされない。経過と実測は report/p2.md。確定事項:

- **タイムスライスは 12ms ではなく 50ms**。この木は `MRB_TICK_UNIT=5` /
  `MRB_TIMESLICE_TICK_COUNT=10` に上書きしている (12ms は task_hal.h の
  フォールバック既定値)。コードは定数から導くので値が変わっても追従する。
- **バイトコード境界の hook だけでは足りない**。tick は 3 箇所から供給する:
  vm.c の境界 (CPU を握ったタスク用)、スケジューラのループ先頭、アイドル待ちの
  あと。アイドル分が無いと、眠ったタスクが二度と起きない。
- **`mrb->task.switching` はタスクが走っている最中にしか書けない**
  (`execute_task` が渡すたびに消す)。要求を立てる (`switch_pending`) のと
  配達する (`vm_deliver_switch`) のを分け、配達は VM のスレッドからのみ行う。
  vm.c の間引き計数は mrb_vm_exec に入った 1 命令目で必ず発火させ、そこが
  top-half の非同期書き込みと等価な瞬間になる。
- **コストは間引き計数ではなく HAL 呼び出しの頻度で決まる** (Linux sim で
  1 回約 10 マイクロ秒、中身は FreeRTOS mutex)。既定値 65536 で VM 速度は
  既定ビルド比 +4%。ESP32 で自給に一本化するなら、先に HAL 呼び出しから
  mutex を外すこと (自給モードでは 1 VM のエントリに触るのはその VM の
  スレッドだけなので外せる)。
- **Spinel プログラムには効かない**。mruby の dispatch loop を通らないので、
  CPU を握った Spinel プログラムを剥がす手段は無い。P4a で確認すること。
- Tab5 実機の回帰も完了 (2026-08-29、主セッション)。既定ビルドで
  ブート / デスクトップ / エディタ打鍵 / task_sleep 精度とも従来どおり、
  crash 0 件。受け入れは 4/4。検証後の遠隔 kill で P2 と無関係の
  非決定ハングを 1 回観測 (app_kill_fix と同族の疑い。report/p2.md)。

### 目的

「別タスクから switching を撃って CPU-bound な Ruby を剥がす」現行設計
(mruby_tick タスク、task_hal.c:83-91) を、VM 自身が実時間で自分を剥がす方式に
変え、協調スケジューラでもビジーループの Ruby アプリが他を飢餓させないようにする。

### 設計

- 編集対象は原本 lib/patch/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/
  freertos/task_hal.c (submodule 側コピーは上書きされるため触らない)。
- bottom-half (`mrb_hal_task_take_pending_ticks`) で、前回呼び出しからの実時間
  (fmrb_hal_time_get_us) を tick に換算して pending_ticks を自給する。
  3ms 相当を超えていたら自分で `mrb->task.switching = TRUE` を立てる。
- top-half (mruby_tick タスク) は当面残す (ESP32 実機では従来動作を維持)。
  自給側が新設フラグで有効化される二段構えにし、wasm では top-half を
  起動しない。将来 ESP32 でも自給に一本化できるかは実機計測後に判断。
- `mrb_hal_task_idle_cpu` の `ulTaskNotifyTake(4ms)` はそのまま協調でも安全。

### 受け入れ条件

- Linux sim (現行のプリエンプティブ port) で回帰: `rake build:linux` →
  tools/dev_run_check.sh でブート、タイマ系アプリの挙動が従来どおり、
  `while true` を回すアプリを起動してもデスクトップ・エディタが操作できる。
- mruby-task のタイムスライスが実測で従来 (4ms tick / 3 tick 切替) と
  同等であること (ログ計装で確認)。
- ESP32 実機 (Tab5) でブートと通常操作の回帰 (top-half 併存モード)。

---

## P3: display backend 分割 (P4 実機リファクタ)

### 目的

display_p4_task.cpp (~5100 行) から「パネル起動 + PPA + DSI 出力」を分離し、
バックエンド差し替え可能にする。同時に PPA 合成の CPU 実装を書き、
**P4 実機上で CPU 経路を回帰確認できる状態**を作る (wasm に行く前に、
一番重い新規実装を実機で検証しておく)。

### 設計

- `display_backend_t` を新設する (GA 側 display_interface_t 相当):
  init / alloc_fb / blend_block (color-key 付き) / present / shutdown。
- 実装を 2 つ並べる:
  - display_backend_ppa.cpp — 現行コードの移設 (PPA Blend + SRM + DSI +
    パネル起動 + バックライト + I2C 周り)。
  - display_backend_cpu.cpp — CPU 合成 + pushRotateZoom による出力。
    **ターゲット非依存の C で書く** (P4 でも wasm でもそのまま使う)。
    viewport のトーラス分割 (render_frame の分割送り) と sprite clip の
    セマンティクスを保存する。
- 音は既に output writer 登録 (apuif_set_output_writer) で半分抽象化済み。
  audio_p4_hw.c 側の init/ready/volume を関数表に寄せるだけに留める。
- 動画 (HW JPEG) と EXPORT_FRAME は PPA バックエンド固有機能とし、
  CPU バックエンドではエラー応答にする。

### 作業項目

1. display_p4_task.cpp の分割 (受信ループ + コマンド解釈を残し、出力系を移設)。
2. display_backend_cpu.cpp の実装 (color-key blit、RGB565)。
3. 起動時にバックエンドを選ぶ仕組み (ビルド時定数で可。実機デバッグ用に
   .env か Kconfig で CPU 強制を選べると回帰が楽)。

### 受け入れ条件

- PPA バックエンドで従来と同一挙動: Tab5 実機で fmrb_rd_snap.rb の画面比較
  (デスクトップ / エディタ / PicoRabbit / スプライト系デモ)、tools/fmrb_pngdiff.rb
  で差分ゼロ。
- **CPU バックエンドでも同じアプリ群が正しく描画される** (性能は問わない。
  正しさのみ)。これが wasm 描画経路の実機事前検証になる。
- `idf.py size-components` で内蔵 RAM/flash の悪化が誤差範囲であること。

---

## P4: core 統合 (wasm ビルド)

### 段階を 3 つに割る。

### P4a: ヘッドレスでブートまで (node)

- wasm/CMakeLists.txt に core のソースを集める。基準は Linux ビルドの
  compile_commands.json のファイル集合から、posix 差し替え分を除いたもの:
  - link: fmrb_hal_link_posix.c の代わりに **fmrb_hal_link_local.c を流用**
    (platform/esp32/ にあるが中身は FreeRTOS MessageBuffer のみで機種非依存。
    ガード `fmrb_hal_link.h:94` に wasm を追加)。
  - 入力: usb_task_linux.c の代わりに backend/input_wasm.c (P4b で実装。
    P4a ではスタブ)。
  - display: display_p4_task の受信ループ + コマンド解釈 + display_backend_cpu
    (P3 の成果物)。present はスタブ (フレームを保持するだけ)。
  - audio: apu_emu を含め、hw write はスタブ。
  - sim_log_guard.c と --wrap フラグは入れない。
- 定義: `FMRB_PLATFORM_WASM` を新設し、`FMRB_HW_FAMILY_MODERN` を立てる
  (fmrb_hw_defines.cmake に wasm 分岐追加)。boot.c に wasm ブランチ
  (display_wasm/audio_wasm/input_wasm の init)。
- picoruby: build_config/picoruby-wasm.rb を土台に family_mruby 用の wasm
  CrossBuild 設定 (lib/add/family_mruby_wasm.rb) を作る。mrbc はネイティブのまま。
  MRB_* レイアウト定義は CMake 側と必ず一致させる (doc 済みの二重ビルド ABI 罠)。
- Spinel: SP_NO_MMAN で sp_fiber.c / sp_sched.c を除外
  (fmrb_spinel_rt/CMakeLists.txt:118 の条件を「ネイティブ ISA か」に変更)。
  sp_system.c の fork/exec は失敗を返すスタブに。
  まず標準構成 (Spinel カーネル) で組み、詰まったら FMRB_APP_ENGINE で
  全 mruby 構成に落として切り分ける。
- MicroPython: MICROPY_NLR_SETJMP=1、native emitter 無効。
  Lua: popen/system/dlopen 無効。
- ファイル: `--preload-file flash` で MEMFS。fmrb_hal_file_posix.c /
  fmrb_tmpfs_posix.c はそのまま使う。
- stub/: esp_log (printf/console へ)、heap_caps 統計 (malloc 概算)、
  esp_random / esp_read_mac / esp_restart / esp_err_to_name。
- debugd / TCP 系 / MIDI serial はソース集合から外す。

**受け入れ条件**: node 実行で `main_loop started` 相当のブートマーカーが出る。
kernel VM とデスクトップアプリの spawn ログ (`M1|spawn:` 相当) が並ぶ。

### P4b: ブラウザで画面と入力

- present: C 側で RGB565 → RGBA8888 に変換した固定番地バッファ + フレーム
  連番を持ち、**JS メインスレッドが requestAnimationFrame で連番を見て
  putImageData する** (SharedArrayBuffer 上の wasm メモリを直接読む。
  ワーカーから DOM に触らないので proxy 不要)。カーソルは capture と同じ
  扱いで JS 側描画。
- 入力: JS がキー/マウスイベントを SAB 上のリングに詰め、input_wasm タスク
  (usb_task_linux.c と同構造のポーリング) が取り出して `fmrb_host_send_*` を
  呼ぶ。KeyboardEvent.code → HID scancode 表と FMRB_KEYMAP_MOD_* 正規化は
  rd_input.c / fmrb_rd_input.rb の SCAN 表を流用。
- 配信ヘッダ: 開発時は emrun / 簡易サーバで COOP/COEP を付ける。

**受け入れ条件**: ブラウザでデスクトップが表示され、マウスでメニュー操作・
ランチャーからアプリ起動・エディタで文字入力ができる。Ctrl+Tab / Ctrl+Q の
グローバルホットキーが効く。

### P4c: 音

- audio_wasm_hw.c: `audio_p4_hw_write()` を SAB 上の int16 リングへの書き込みに
  する。JS 側は AudioWorklet がリングを吸って出力 (15720Hz → 出力レートへの
  変換は AudioWorklet 側で線形補間)。リングは現行 130ms より長く取る
  (500ms 程度から詰める)。
- ブラウザは音声開始にユーザ操作を要求するため、初回クリックで
  AudioContext.resume する導線をページに入れる。

**受け入れ条件**: BASIC の PLAY 文と APU デモで発音する。音程は
fmrb_audio_probe 相当の FFT 検証をリングのダンプに対して行い、既知値
(O3C=261Hz 級) に一致。

---

## P5: 配信 — 完了 (2026-08-29、実公開を除く)

`rake wasm:dist` が置くだけで公開できる `build/dist/` (8 ファイル 6.19MB) を
作る。素の静的サーバ (ヘッダ無し) で cross-origin isolation が成立することを
Chrome で実測済み。経過・実測値・見つけた穴は report/p5.md。確定事項:

- **解像度は 426x240 / 640x360 / 852x480 の 3 つとも採用**。3 解像度で
  デスクトップ・メニュー・ランチャー・エディタ・PicoRabbit を確認して
  レイアウト破綻なし。`default_user_app_width/height` は比例させる。
  core 側は見込みどおり無改修 (conf 駆動)。
- **壁紙だけが 426x240 のまま**残る (広い解像度では周りがデスクトップ色)。
  `DRAW_IMAGE` が Modern/wasm では scale を見ないので、Ruby から
  `draw_image(scale_x:)` を渡しても効かない。直すのは P5 の外。
- **表示側の天井が先に効く**: 窓 1 つ = canvas 1 枚で
  `DISPLAY_P4_MAX_CANVAS=8` のため、FMRB_MAX_APPS を上げただけでは
  11 本目から死ぬ。wasm だけ 34 (+ `DISPLAY_P4_VM_MAX_PROGS=64`) に上げて
  12 本同時起動を確認した。
- `rake wasm:scan` を常設。packed 一覧 (リンカが実際に詰めた結果) と .data の
  バイト列を毎回照合する。同梱の NSF 2 本は権利上問題ないサンプルとして
  そのまま入れる (2026-08-29 ユーザ確認)。
- **audio_p4 が fopen に `/flash` を直書きしていた**ため wasm で NSF も
  Breakout の BGM も鳴っていなかった。`AUDIO_P4_FLASH_BASE` で通した。
  この経路は Linux sim ではコンパイルされないので、wasm で初めて見えた穴。
- family-mruby.github.io への組み込みと実公開はサイト側の作業として別途。
- **wasm 専用の config と staging — 済み (2026-08-29、a242aeb4 + e3f716c1)**:
  flash レイアウト整理とセットで解決した。config/system_conf_wasm.toml を
  新設し、rake wasm:web は **git 追跡分の flash/ + この conf だけ**を
  build/webflash に staging して束ねる。ローカル専用物は flash_local/
  (gitignore、実機 staging でのみ合成) へ。再ビルド後のバイナリ照合で
  ssid/password 0 件・home/music 0 件を確認済み。rake 側に
  wifi/secrets の検知ゲートも常設。preload の「選定」は不要になった
  (追跡分 = 配布可、が構造で保証される)。残るチェックは secrets.h を
  置いた状態のビルド確認のみ。

ユーザ要望 (2026-08-29、P5 に同梱する改善):

- **サイバーパンク配色 (web 既定) — 済み (2026-08-29)**: system_conf_wasm の
  [theme] をネオン系にし、自作のスカイライン壁紙
  (usr/share/backgrounds/bg_cyber_426x240.png、生成スクリプト由来で権利
  問題なし) を cyberpunk 時に /data と usr/share の壁紙へ上書きする。
  ページの設定 (localStorage) で Classic (実機配色 + 西部劇壁紙) に戻せる。
  設定の適用は当初 preRun の MEMFS 編集だったが、実ブラウザで別 FS 問題が発覚し argv + C 側適用に作り替えた (report/p5.md 追補)。
  **教訓: テーマ色に 0x01 を使ってはならない** (デスクトップ前景の
  カラーキー。window_bg=0x01 で窓が透けた)。既知の軽微差: Spinel エディタの
  タイトル帯は別経路の配色で旧色のまま。

- **内部解像度の選択 — 済み (2026-08-29)**。「モニタに合わせる」は入れず、
  3 つのプリセット + `?w=&h=` + 表示倍率 (1x/2x/3x/フィット) にした。
  以下は着手前の調査メモ:
- **内部解像度の選択** (作業空間を広くする。ページ拡大の話ではない):
  調査の結果、解像度は既に実行時設定だった — `display_width/height` は
  system_conf.toml から読まれ (fmrb_kernel.c:134、config/ は linux=320x240 /
  p4=426x240)、display_p4_task も conf とフレームバッファ実体から
  サイズを取っていて 426 の焼き込みは無い (コメントのみ)。よって:
  - ページの起動前 UI で解像度を選び、argv (--fmrb-res=WxH) で渡して
    機械側 C (page_settings_wasm.c) が conf を書き換える (preRun の MEMFS
    編集は PROXY_TO_PTHREAD の別 FS 問題で不成立 — report/p5.md 追補)。
    JS 側 canvas と present の RGBA バッファは
    モジュールから実サイズを受け取る形に直す。ビルドは 1 本のまま
    (解像度ごとのビルドはしない。ユーザ方針 2026-08-29)。
    URL クエリ (?w=&h=) と「モニタに合わせる」(JS が画面サイズから 16:9 の
    近い値を計算、検証済み上限でクランプ) も同じ機構に乗せる。
  - プリセットは 16:9 系で 426x240 (実機一致) / 640x360 (HDMI 計画と同値、
    エディタ 79 桁) / 852x480 (2x) から。
  - 大きい解像度で Ruby 側 (デスクトップ・エディタ・ランチャー) の
    レイアウト前提が露出しないかは検証項目 (320 と 426 の両対応実績が
    あるので、破綻より「余白が間延び」系の見た目問題を想定)。
- **全画面モード — 済み (2026-08-29)**: 設定行のボタンで screen-box を
  requestFullscreen し、全画面中はフィット固定。Keyboard Lock がある環境
  (Chrome) では Esc/Ctrl+W を捕まえ、無い環境向けにページに注記した。
  実ブラウザでの操作確認はユーザ待ち。
- **同時起動アプリ数の上限緩和 — 済み (2026-08-29)**: 下の設計案どおり
  実装した。天井は `components/fmrb_common/include/fmrb_limits.h` へ切り出し
  (fmrb_mem_config.h が FreeRTOS ヘッダ無しで同じ数を使えるように)、
  wasm は 32、実機・sim は 9 のまま。conf の `max_apps` 照合は spawner では
  なく `alloc_ctx_index()` に置いた (枠を配る唯一の場所)。以下は当時の案:
- **同時起動アプリ数の上限緩和**: 実体は FMRB_MAX_APPS=9
  (components/fmrb_common/include/fmrb_task_config.h:11)。ユーザ方針
  (2026-08-29): 無限にはせず十分大きい固定値でよく、**ビルド分岐ではなく
  system_conf で扱いたい**。設計案:
  - FMRB_MAX_APPS は「静的容量の天井」に格下げし (配列サイズ。wasm は 32、
    実機は当面 9 のまま)、実効上限を system_conf の `max_apps`
    (既定 = 天井) として新設、spawner が spawn 時に照合する。
  - 完全な実行時化 (配列の動的確保) は task_hal の
    vms[MRB_TASK_MAX_VMS] など patch 済み gem 側の静的配列まで及ぶので
    採らない。容量+conf の 2 段が、ファイルで制御でき実装も浅い。
  - 連動: pthread pool (現状 40) を wasm 容量に合わせて引き上げ。
    kernel Ruby 側の FMRB_MAX_APPS 前提ループ (input_router.rb) が定数を
    どう受けているか確認 (conf 値を kernel へ渡す経路が要るかもしれない)。
  - メモリ試算: 1 アプリ = VM プール ~1MB + スタック 64KB。32 本で
    +33MB 程度、INITIAL_MEMORY 256MB に収まる。連動する箇所に注意 —
  VM プールのメモリ、pthread pool (現状 40)、および kernel Ruby 側に
  FMRB_MAX_APPS 前提のループがある (input_router.rb:55 のコメント参照。
  定数がRuby 側へどう渡っているか確認してから触る)。

### ページ設定基盤と鍵の持ち込み (配布までに必要。ユーザ指示 2026-08-29)

ネット系機能はブラウザに素のソケットが無いため、使うなら (1) HTTP 要求を
JS の fetch に流すブリッジと、(2) 利用者が自分の API 鍵を持ち込む仕組みが
要る。サイト側は鍵を一切持たない。

- **ページ設定基盤**: 設定パネルを 1 つ設け、保存先は localStorage
  (クッキーは使わない — サイトへの全リクエストに自動同送されログに残る
  経路ができるため)。解像度などのページ設定も同じ器に同居させる。
  鍵は「このブラウザに保存 / セッションのみ」の選択と削除ボタン、
  「このブラウザにのみ保存されます」の注記を付ける。
- **鍵の受け渡し (推奨案)**: 起動時に JS が localStorage の鍵を MEMFS の
  services 設定へ書き込む。実機の「設定ファイルの平文鍵」(tts.rb:97 の
  cfg["api_key"]) と同じモデルなので Ruby 側は無改修。同一オリジンの
  ページ内で完結し、ページ外に出るのは対象 API への送信時のみ。
  (より厳格な「鍵は JS に留めブリッジが付与」案は、Ruby がヘッダを組む
  現行構造と合わないため採らない。必要になったら再検討。)
- **fetch ブリッジ**: HTTP 要求単位の C API (JS fetch へ proxy、応答まで
  タスクを FreeRTOS 待ちでブロック)。消費者は wasm 用の tts_http 差し替え
  (socket 版 tts_http.rb は実機用にそのまま)。
- **対象機能の順**: TTS (実装済みの唯一の鍵消費者。cache → VOICEVOX →
  cloud の順序も既存どおり) → ai コマンド (doc/ai/ideas.md、実装されたら)。
- **調査項目**: OpenAI API を CORS (ブラウザ直) で叩けるかの実測。
  VOICEVOX ローカルサーバ (localhost、CORS 設定可、鍵不要) は
  鍵なしで試せるデモ経路として有望なので先に確認する。

**受け入れ条件**: GitHub Pages 相当の静的配信で、Chrome/Firefox の現行版から
操作できる。初回ロードが実用的な時間 (目安 10 秒以内) に収まる。
鍵持ち込み分: 鍵を入れると TTS が鳴り、未設定なら明示メッセージ。鍵の
送信先が対象 API のみであること (開発者ツールで確認)。

---

## 公開の流れ (検討済 2026-08-30。実行はユーザの号令で)

既存の BLE コンソールと同じ型に載せる。コンソールは
family-mruby.github.io (MkDocs) の deploy.yml が push ごとに
fmruby-core から sync して gh-pages へ出しており、**ビルド済み wasm は
サイト repo にコミットして持つ** (console-wasm/) 前例がある。

- wasm 版は `rake wasm:dist` の自己完結 8 ファイルなので sync 不要。
  **dist/ をサイト repo の docs/studio/ に丸ごとコミット**し、
  どの fmruby-core コミットから作ったかをメッセージに記す。
  deploy.yml は無変更でそのまま公開される。
- サイト側の作業はナビへのリンク 1 本のみ。coi-serviceworker は
  サブパス配下で正しくスコープされる。
- scan が dist 生成に組み込まれているため、「検査を通らない配布物は
  作れない」がリリースゲートとして機能する。
- 定常化したら installer と同じタグ連動 CI (emsdk + M5GFX 取得 +
  wasm:dist) へ昇格できるが、初回〜当面はローカルビルド + 成果物
  コミットで十分 (console-wasm 前例と同型)。
- **公開パスは /studio/ で確定** (ユーザ決定 2026-08-30)。「作る機械」の
  ブラウザ版という将来像 (エディタ + 型支援 + BLE beam) を名乗る名前。
  公開 URL: https://family-mruby.github.io/studio/

## P5b (計画済 2026-08-30): fetch ブリッジ — Net::HTTP を wasm で、Weather を初例に

ユーザ指示: HTTPS を叩けるようにし、Weather サンプルが動くこと。
**既存 gem と同一 IF の汎用 Ruby クラスとして**提供する (専用 API を作らない)。
鍵が不要なので、TTS (旧 T3) より先に単独で出せる。

### 前提の実測 (2026-08-30)

- weather.app.rb は `Net::HTTP.get_response(URI.parse(url))` で
  **Open-Meteo (鍵不要)** を叩く。
- Open-Meteo は CORS 全開放を実測確認: `access-control-allow-origin: *`
  (GET/POST/OPTIONS)。ブラウザ直叩き可。https ページ → https API で
  混在コンテンツの問題も無い。
- picoruby-net-http は **pure Ruby (mrblib のみ) で下は socket 依存**。
  全公開 API (get/get_response/post、リクエストオブジェクト、ヘッダ、
  HTTPResponse) は **`Net::HTTP#request` (http_client.rb:140) 1 本に
  合流する** — ここが輸送の継ぎ目。

### 設計

1. **C ブリッジ** (wasm/backend/webfetch_wasm.c):
   要求スロット (url / method / headers / body) + 完了フラグ。呼び出した
   タスクは 10ms の FreeRTOS 待ちでポーリング (**協調 port の生存条件 —
   スピン禁止**)、タイムアウト既定 30s。応答 (status / headers / body) は
   C 側バッファへ。JS 面 (スロット位置等) を EMSCRIPTEN_KEEPALIVE で公開。
2. **JS ポンプ**: 周期 (50ms) でスロットを見て fetch() を実行し、
   HEAPU8 + _malloc で書き戻す。**page (main.js) と node
   (wasm/tools/webfetch_pump.js) で同一実装を共有** — node 18+ は fetch
   内蔵なので、weather の受け入れが headless で回る。
3. **Ruby 層は既存 gem そのもの**: picoruby-net-http を wasm の gembox に
   加え、**`Net::HTTP#request` (と start/finish の socket 部) だけを
   wasm 上書きで差し替える**。URI・HTTP::Get/Post・ヘッダ・HTTPResponse は
   字面ごと同一 (mruby バインディングは C ブリッジへの薄い FFI 1 つ)。
   アプリは無改修 — weather.app.rb がそのまま動くことが仕様。
4. **FmrbNet.connected?**: weather は起動時にこれでゲートするので、
   wasm ではブリッジ可用 = true を返す道を通す (fmrb-net の wasm 分岐か
   net service 側か、着手時に実装を見て決める)。

### スコープ外

- WebSocket (ブラウザ native WS に写像できるので将来は素直。今回はやらない)、
  chunked/streaming の逐次読み (fetch 完了後に一括で返す。weather / TTS には
  足りる)、リダイレクトやクッキーの追加実装 (gem の現状仕様に従う)。

### 受け入れ条件

1. **wasm (node) で weather.app.rb が無改修で実際の天気を描画する**
   (webfetch_pump 併用、probe 道具でスクリーンショット)。
2. ブラウザ (dist + 素の静的サーバ) でも weather が動く
   (?autostart&holdload の probe で撮って確認)。
3. 実機 / linux に影響ゼロ (gem 本体無改修。ビルドマトリクス + sim ブート)。
4. rake wasm:scan が通ったまま (新規ファイルの混入検査を含む)。
5. ネットワーク不通・CORS 拒否・タイムアウトで**アプリが例外死せず**
   weather の従来のエラー表示に落ちる。

### 後続 (P5c): 鍵の持ち込みと TTS

instruction_p5.md T3 の内容をこの基盤の上に載せる (設定パネルの鍵欄 +
tts_http の wasm 差し替え + VOICEVOX/OpenAI の CORS 実測)。P5b が通れば
輸送は共通で、残るは鍵の UI と services 設定への注入だけ。

## ストレージ永続化 (計画済 2026-08-31): 設計は storage_persistence.md

ブラウザ版でユーザが作ったものを残す。方式は /home への IDBFS 割り当てだが、
**着手の前提は「配布物を /home から出す」整理**で、そこが本体になる。
段取り・判断の根拠・未確定事項は doc/wasm/storage_persistence.md が正。

## P6 候補 (構想): Web Bluetooth で実機の Family mruby と通信

ユーザ発案 (2026-08-29)。ブラウザ版から実機 (Tab5 / Retro) へ BLE で
届くと楽しい、の実現性メモ。P5 (配信) には含めない。

- ブラウザ側は Web Bluetooth (GATT central)。Chrome/Edge のみ・HTTPS 必須
  (GitHub Pages は満たす)・ユーザ操作でデバイス選択、という制約はデモ用途
  なら許容範囲。
- **実機側の追加実装はほぼ不要の見込み**: debugd の BLE GATT サービス
  (main/drivers/ble/ble_task.c + ble_debug_link) と ble_fs
  (ファイル put/get) が既にある。作るのは JS 側のクライアント
  (ble_framing.c の対向 + プロトコルは tool/debug/fmrb_dbg_client.py が正)。
- 一番おいしい形: **ブラウザのエディタで書いたコードを BLE で実機に送って
  起動する「beam」**。特に Retro (S3) は remote desktop が無く BLE が唯一の
  遠隔口なので、「ブラウザから Retro にプログラムを送る」初の経路になる。
- 帯域は BLE なので .rb 数十 KB を数秒 — 用途に足りる。認証は debugd の
  開発ゲートと同じ割り切り (ペアリングと近接が実質の制御)。

## スコープ外 (再掲・実装上の扱い)

| 項目 | 扱い |
|---|---|
| debugd / VM リモートデバッグ | ソース集合から除外 (main/CMakeLists.txt の粒度で外せる) |
| MIDI | fmrb_midi_sched は組み込むが serial 出力はスタブ。精度劣化は許容 |
| 動画 / EXPORT_FRAME | CPU バックエンドはエラー応答 |
| ネットワーク / WiFi サービス | Modern の service host は expected_stop で停止扱い |
| マイク | audio_p4_mic_read は空データ |
| 強制 kill (vTaskDelete 他殺) | no-op + ログ。P2 で行儀よい経路に必ず落ちる前提 |

## リスクと撤退線

- **P1 が通らない場合** (Emscripten pthread 上でカーネルが安定しない):
  全体を中止する。P2 / P3 の成果は本体改善として残る。
- **性能が出ない場合**: mruby VM は wasm 実績があり大きく崩れない見込みだが、
  デスクトップ描画が 30fps に届かないときは present の間引き (連番スキップ) と
  CPU blend の dirty region 化で追う。それでも不足なら「デモは動くが遅い」を
  受け入れてスコープを縮める (エディタ + BASIC 中心)。
- **Spinel (SP_NO_MMAN) で未知の穴が出た場合**: 全 mruby 構成に切り替えて
  先に進み、Spinel は追いかけで直す (新しい構成は無言の穴を持つ前提で、
  wasm 構成でしか通らない経路の動作確認を P4a の受け入れに含めている)。

## 進め方の運用

- 各フェーズの気づき・確定結果は doc/wasm/report/pN.md に残す
  (計画書側には確定結果だけ反映)。
- P2 と P3 は ESP32/Linux の既存検証手段 (sim ヘッドレス検証、Tab5 遠隔検証)
  で受け入れる。P1 と P4a は node で CI 可能にする。
