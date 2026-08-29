# wasm (ブラウザ) 対応の検討と計画

> 状態: 進行中 | 更新: 2026-08-29 | P1 (port PoC)・P2 (自己プリエンプション)・P3 (display backend 分割) 完了。残るは P4 以降

2026-08-28 調査。core 単体をブラウザで動かすための実現性検討と段取り。
実装の分割・作業項目・受け入れ条件は implementation_plan.md。

## 目的と基本方針

- **core をブラウザ (WebAssembly) で動かす**。デスクトップ・エディタ・Ruby アプリが
  ブラウザ内で動く Web デモが目標。
- **graphics-audio (GA) は移植しない**。Tab5 (Modern / ESP32-P4) の in-process 描画
  経路を土台にする。Modern は link 実装 1 ファイルの差し替えだけで描画がプロセス内
  完結する構造になっており、単一 wasm モジュールと相性がよい。
- **FreeRTOS は最低限の移植**にとどめる。カーネル本体は流用し、port 層だけを
  Emscripten 向けに書く。プリエンプションは捨てて協調にする。
- 現行の Linux sim (3 プロセス + UNIX ソケット + POSIX SHM) は土台にしない。
  FreeRTOS Linux port が SIGALRM とシグナルでのプリエンプションに全面依存しており、
  wasm にシグナル配送が無い以上そのままでは 1 行も動かない。プロセス分割の理由も
  SIGALRM と SDL の衝突 (docker-compose.yml:5) なので、シグナルを使わない port に
  すれば分割の動機ごと消える。

## 全体像

```
Ruby/Lua/BASIC アプリ → fmrb_gfx → host_task → link_local (プロセス内 MessageBuffer)
  → display_wasm_task (display_p4_task の移植。PPA を CPU 合成に置換)
  → 426x240 RGB565 完成フレーム → Canvas / SDL テクスチャ
音   : audio コマンド → apu_emu (core 内合成、P4 と同じ) → audio_p4_hw_write() 差し替え → WebAudio
入力 : ブラウザイベント → rd_input.c と同形の変換 → fmrb_host_send_*
FS   : flash/ を --preload-file で MEMFS へ
```

wasm ターゲットは「Modern の亜種」として定義する:
`FMRB_HW_FAMILY_MODERN` の習慣 (フォント・サービス・426x240) を引き継ぎ、
描画・音・入力のバックエンドだけを差し替える。

## 調査結果の要点

### 分岐点は link 1 ファイル (Tab5 経路の構造)

- Retro/Modern/Linux の分岐は HAL link の実装選択だけ
  (components/fmrb_hal/CMakeLists.txt:35-49)。P4 は `fmrb_hal_link_local.c`
  (FreeRTOS MessageBuffer によるプロセス内 link)。link より上 (host_task /
  fmrb_gfx / transport) に機種分岐は無い。
- gfx コマンドの消費側は main/drivers/display_p4/ (~5100 行) に閉じている。
  プリミティブは LovyanGFX の `LGFX_Sprite`、合成は PPA Blend (HW)、出力は
  PPA SRM 3x 拡大 + 90 度回転で DSI FB へ。canvas は常に画面サイズの RGB565。
- **remote desktop が wasm の必要とする境界を既に実運用している**:
  - 完成フレームの取り出し口 = capture API (display_p4_task.h:74-114、
    426x240 RGB565、カーソル別チャネル)
  - 仮想座標系での入力注入 = rd_input.c (ブラウザ座標 → `fmrb_host_send_*`)
  present と入力はこの 2 つの写経で済む。
- 音は `audio_p4_hw_write(const int16_t*, len, ch)` 1 関数
  (main/drivers/audio_p4/audio_p4_internal.h:16) の差し替えで apu_emu ごと流用可。
  Retro/Linux sim では apu_emu は相手側の仕事だが、Modern 構成では core 内。

### FreeRTOS の実使用面は狭い

Linux ビルド全 313 ファイルの棚卸し結果:

- プロジェクトコードは `fmrb_rtos.h` (components/fmrb_common) のマクロを玄関に
  しており、生 API 直叩きは task_hal.c など数箇所。
- **使用ゼロ**: ストリーム/メッセージバッファ (※ link_local を使うと 1 箇所増える)、
  イベントグループ、ソフトタイマ、`*FromISR` (fmrb_msg.c:25 に検証コメントあり)、
  recursive mutex。
- クリティカルセクションを使うのは fmrb_midi_sched.c の 1 ファイルのみ。
- 必要な機能: タスク生成/削除/遅延/suspend/resume、TLS 3 スロット +
  削除コールバック (`fmrb_current()` が全域で常時呼ぶ)、task notification
  (mruby-task の心臓、task_hal.c:94,217)、mutex/binary/counting セマフォ、
  キュー、1ms 単調 tick。
- IDF サービスで実際に要るのは **freertos + heap + log の 3 つだけ**。
  esp_timer/esp_event/nvs は Linux ビルドで既に事実上ゼロ依存。
  `esp_restart` / `esp_read_mac` / `esp_random` はダミーで足りる。

### tick=1ms を仮定している箇所

`portTICK_PERIOD_MS==1` 前提の ms 換算が fmrb_basic.cpp:127、fmrb_mp.c:32、
fmrb_rtos.h:46 にある。wasm でも tick は 1ms のまま維持する (実時間から供給)。

## 設計

### FreeRTOS 移植: カーネル維持 + port 新設 (協調)

`tasks.c` / `queue.c` / `stream_buffer.c` は移植性の高い C なのでそのまま使い、
`portable/linux/port.c` の代わりを Emscripten pthread (Worker +
SharedArrayBuffer) で書く。

- 各タスク = pthread。非実行タスクは自分の条件変数で待つ。この骨格は既存 port の
  `utils/wait_for_event.c` がシグナル無しで実現しており流用できる。
- クリティカルセクション = グローバル mutex (現行の pthread_sigmask の置き換え)。
- tick は setitimer を使わず、待ちからの復帰時に実時間 (`emscripten_get_now`)
  から `xTickCount` を進める。
- **プリエンプションは捨てる** (pthread_kill が無いので原理的に不可)。
- 自前の API shim 23 個で置き換える案も検討したが、タイムアウト付きキューや
  所有者付き mutex の再実装はバグの温床なので採らない。

副産物として SIGALRM 前提の防御が丸ごと消える: sim_log_guard.c と
`--wrap` リンカフラグ 4 本 (main/CMakeLists.txt:291-296)、各所の EINTR リトライ、
「ログ 1 行ごとに 1ms 寝る」緩和策 (fmrb_task.c:171-174 ほか)。

### 協調化で壊れる点と対策

1. **ビジーループの Ruby アプリを剥がせない (最重要)**。
   現行は `mruby_tick` タスクが別スレッドから `mrb->task.switching = TRUE` を
   撃って剥がす (task_hal.c:83-91)。協調ではこのタスクが走れない。
   → task_hal を「VM 自身のスレッドがバイトコード境界で実時間を見て自分に
   switching を立てる」自己プリエンプション方式に書き換える。
   doc/reference/task_priority.md に単核でゲストが下の段を飢餓させた実証記録があり、
   ここを解かない限り成立しない。この変更は Linux sim でも検証できる。
2. **ブロッキング I/O**。fmrb_hal_link_posix.c:38 の recv() が該当するが、
   wasm では link_local に置き換わるので問題ごと消える。debugd の TCP
   (select) は初期スコープ外。
3. **強制 kill**。`fmrb_app_kill` の最終手段 `vTaskDelete` (他殺) は、
   Emscripten では pthread_cancel 相当が信用できない。1 の自己プリエンプションが
   効けば should_exit 側の行儀よい経路に必ず落ちるので、初期は強制削除を諦める。
4. **待ち解除の連鎖が生命線**。GFX フロー制御 (host_task.c:1859 の counting
   セマフォ 96 スロット) は app が take で無限ブロックし host が排出して give する
   構造。カーネル本体を使う限り take/give の READY 遷移はカーネルが正しくやるが、
   host_task が定期的に走るループ構造 (16ms 周期 + 10ms キュー待ち) の維持が前提。

### 描画: display_p4_task の分割と wasm バックエンド

display_p4_task.cpp は「link 受信 + コマンド解釈 + 合成」と「パネル起動 + PPA +
DSI 出力」が同居しているので、まずバックエンド抽象で割る
(GA 側の display_interface.h 相当の `{init, blend, present, poll}` を新設し、
P4 実装と wasm 実装を並べる)。この分割は P4 実機へのリファクタとして先行でき、
実機で回帰確認できる。

| 作業 | 内容 | 重さ |
|---|---|---|
| PPA Blend の CPU 実装 | color-key 付き矩形ブリット。現状 `g_ppa_blend==NULL` だと無描画 (display_p4_task.cpp:676) なので書き起こし必須。viewport 分割と sprite clip のセマンティクス保存 | 大 |
| LovyanGFX の sprite-only 切り出し | 現状 M5GFX 経由で P4 パネルドライバごと include (lgfx_tab5.hpp:29)。LGFX 本体に CMake_WASM の例があり wasm 実績はある | 大 |
| present | capture と同じバッファを Canvas へ。3x 拡大と 90 度回転は不要になるので実機より単純 | 小 |
| 音 | `audio_p4_hw_write` を WebAudio へ。init/ready/volume/mic はスタブ | 小 |
| 入力 | rd_input.c を写経。ブラウザの `KeyboardEvent.code` → HID scancode 変換と `FMRB_KEYMAP_MOD_*` 正規化。イベントは JS 側リングに積み、タスク文脈から `fmrb_host_send_*` を呼ぶ (host のキュー待ちがあるため) | 小 |
| HW JPEG (動画 / EXPORT_FRAME) | 初期スコープ外。エラー応答で可 | — |

### 言語処理系の措置

- **Spinel**: `SP_NO_MMAN` を立てて sp_fiber.c / sp_sched.c を除外する
  (ESP32 用に実装・実証済みの経路)。除外条件が現在
  `if(NOT CONFIG_IDF_TARGET_LINUX)` (components/fmrb_spinel_rt/CMakeLists.txt:118)
  になっているのを「ネイティブ ISA かどうか」に直す。sp_system.c の
  fork/exec はスタブ化。
- **mruby/picoruby**: 例外は素の setjmp/longjmp なので
  `-sSUPPORT_LONGJMP` で通る。picoruby 本体に wasm 用 build_config が既にある
  (build_config/picoruby-wasm.rb)。mrbc 等のホストツールはネイティブのまま。
- **MicroPython**: `MICROPY_NLR_SETJMP=1` を明示し、x64 native emitter を無効化。
- **Lua**: popen/system/dlopen を無効化 (マクロで可能)。

### ビルドと配信

- `idf.py` に emcc を差すより、**wasm 用の独立 CMake を組んで必要ソースだけ
  集める**。IDF から要るのは freertos カーネル + heap/log の代替だけ。
  emcc ビルドの前例は `rake ti:wasm` (rakelib/ti.rake:66-109) にある。
- ファイルシステムは `--preload-file flash` で MEMFS へ。/tmp の揮発性再現
  (fmrb_tmpfs_posix.c) はそのまま流用できる見込み。永続化 (IDBFS) は後回し。
- pthread を使うため SharedArrayBuffer が必要 → 配信に COOP/COEP ヘッダが要る。
  GitHub Pages はヘッダを出せないので coi-serviceworker で回避する。
- スタックに注意: wasm の既定スタックは小さい。`-sSTACK_SIZE` と各タスクの
  pthread スタックサイズを .app.toml の task_stack_kb 同様に明示する。
- `clock_gettime` の分解能はブラウザで ~1ms。性能計測の数字は意味を持たない。

## 初期スコープ外

- debugd (TCP 5555) と VM リモートデバッグ
- MIDI (1ms 精度 + 最高優先度で成立している。ブラウザのタイマ精度では劣化前提)
- 動画再生 (HW JPEG) と EXPORT_FRAME
- ネットワーク系 (WiFi 前提のサービス、ai コマンド等)
- マイク入力
- 強制 kill (vTaskDelete 他殺)
- 音の官能品質 (15720Hz → WebAudio のリサンプル品質は後で詰める)

## 段取り

1. **FreeRTOS wasm port 単体 PoC** — カーネル + 新 port + タスク数本のテストで、
   協調スケジューリング・notification・キュー・タイムアウトが回ることを確認する。
   全体の成否がここで決まる。
2. **task_hal の自己プリエンプション化** — wasm と独立に Linux sim で検証できる。
   実機の kill 問題 (doc/archive/app_kill_fix) とも根が同じで、単体でも価値がある。
3. **display backend 分割** — display_p4_task の抽象化を P4 実機に対する
   リファクタとして先行し、実機で回帰確認する。
4. **統合** — core 全体を wasm ビルドし、CPU 合成 + Canvas/WebAudio/入力を繋ぐ。
5. **配信** — preload、COOP/COEP、ページ UI。デモとしてサイトに置く。

2 と 3 は wasm を進めなくても本体の改善として残るため、先行投資として無駄に
ならない。

## 未確定事項

- デモとして何を見せるか (デスクトップ + エディタ + どのアプリ群か)。
  preload するアプリの選定はここで決まる。
- エンジン構成: Spinel カーネル標準のまま行くか、wasm では全 mruby 構成に
  するか。Spinel は SP_NO_MMAN で通る見込みだが、検証構成が 1 つ増える。
- LovyanGFX の切り出し方: LGFX 素の sprite-only 構成を自前で組むか、
  SDL2-Emscripten 上で LGFX の PC 向け構成を使うか。
- キーボード配列: ブラウザの物理キー情報は限定的。jp 配列とかな入力の扱い。
