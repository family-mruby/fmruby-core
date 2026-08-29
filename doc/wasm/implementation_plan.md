# wasm 対応 実装計画書

2026-08-28 起草。方針と調査結果は plan.md を前提とする。ここでは実装をフェーズに
分割し、各フェーズの作業項目・触るファイル・受け入れ条件を定める。
実装担当への指示書: instruction_p1.md / instruction_p2.md / instruction_p3.md
(2026-08-29 作成。P4 以降は P1-P3 の結果を見てから書く)。

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

## P5: 配信

- wasm/web/ にページを作る: Canvas + 開始ボタン (AudioContext resume 兼用) +
  簡単な説明。キーボードフォーカスの捕捉と IME 抑止 (かな入力は端末側機能を使う)。
- coi-serviceworker を同梱して GitHub Pages で COOP/COEP を成立させる。
- preload する flash/ の内容を選定する (デモアプリ一式 + フォント。
  サイズと初回ロード時間で調整)。
- family-mruby.github.io への組み込みはサイト側の作業として別途。

**受け入れ条件**: GitHub Pages 相当の静的配信で、Chrome/Firefox の現行版から
操作できる。初回ロードが実用的な時間 (目安 10 秒以内) に収まる。

---

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
