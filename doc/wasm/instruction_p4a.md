# 実装指示書 P4a: core 統合 — ヘッドレスでブートまで (node)

対象: 実装担当セッション。前提: plan.md / implementation_plan.md の P4a 節と、
**report/p1.md・p2.md・p3.md の 3 本** (P4a はこの 3 つの成果物を初めて
束ねるフェーズで、各 report の「申し送り」節が本指示書の前提になっている)。
タスクごとにコミット (push はしない)。report は doc/wasm/report/p4a.md へ。

## ゴール

core 全体 (カーネル + デスクトップ + 言語処理系) を wasm ビルドし、
**node でヘッドレスにブートして、kernel VM とデスクトップの spawn まで
到達する**。画面出力・入力・音出しはすべてスタブ (P4b/P4c)。
`rake wasm:build` で再現可能にし、既存の ESP32/Linux ビルドには影響ゼロ。

## 前段から引き継ぐ確定事項 (要点のみ。詳細は各 report)

- P1: wasm/port の協調 FreeRTOS が土台。タスクスタックは下限 64KB・バイト
  指定、`uxTaskGetStackHighWaterMark` は無意味 (`fmrb_task:` の Free 列は
  出さないか無効値と明記する)。sdkconfig.h の CONFIG_* は
  「無効なものは定義しない」。stub は wasm/stub/ に集約済み (heap_wasm ほか)。
- P2: **wasm は `FMRB_TASK_SELF_TICK=1` 固定** (top-half を起動しない)。
  vm.c (rake 側) と task_hal.c (CMake 側) の両方に定義が届くこと。
  lib/ を触ったら `rake clean` (libmruby のカスタムコマンドはソース非依存で、
  clean しないと vm.c の変更が黙って落ちる)。
- P3: 合成核 `main/drivers/display_p4/display_blend_cpu.c` は ESP-IDF 非依存で
  **そのままコンパイルする**。カラーキーは「帯の生成規則」ごと持ち込む。
  音は `audio_backend_t` (main/drivers/audio_p4/audio_backend.h) の 4 点を
  差し替えるだけで audio_p4_task.c は動く。

## 設計上の急所 (最初に確認・決着させること)

### 1. Spinel プログラムと協調スケジューラの適合 (P2 の申し送り)

wasm の標準構成は Spinel カーネル + Spinel エディタ + mruby デスクトップ
(.env と同じ)。P2 の自給プリエンプションは mruby の dispatch loop にしか
効かないので、**Spinel プログラムが CPU を握り続けると誰も剥がせない**。

- 最初にやること: Spinel 側の実行ループ (spx スケジューラ、_spin 相当) が
  **FreeRTOS の待ち (queue / notify / delay) でブロックする構造**に
  なっていることをコードで確認する (協調 port では「FreeRTOS の待ちに入る」
  ことがそのまま yield になる。イベント駆動でループごとに待つ設計なら
  剥がす必要自体が生じない)。確認結果 (どの関数がどこで待つか) を report へ。
- 待ちに入らない CPU-bound 区間が実在した場合: 標準構成を諦めて
  全 mruby 構成 (FMRB_APP_ENGINE) に落として前進し、切り分けを report に
  書く (implementation_plan の撤退線どおり。Spinel は追いかけ)。

### 2. LGFX の sprite-only 切り出し (plan.md 未確定事項の決着)

canvas への描画は LGFX_Sprite のソフトウェア描画で、wasm でも要る。現状は
M5GFX 経由で Tab5 パネルドライバごと include している (lgfx_tab5.hpp:29)。

- **推奨: SDL2-Emscripten は持ち込まず、LGFX 素の sprite-only 構成を組む**
  (出力に SDL を使わない方針と揃える。LGFX 本体に CMake_WASM の前例あり)。
  LGFX_Sprite + フォント描画に必要な最小ソース集合を特定し、
  wasm/CMakeLists.txt で直接コンパイルする。
- display_p4_sprite.cpp 等が include するヘッダを wasm 用に差し替える
  必要が出たら、lgfx_tab5.hpp の対になる薄いヘッダ (lgfx_wasm.hpp 等) を
  作る。パネル・バス・タッチのクラスは一切入れない。
- ここが P4a で一番の未知数。詰まった内容と決着を report へ。

### 3. Tab5 HW 依存の除外

P3 はパネル起動 / I2C service / ヘッドホン検出を display_p4_task.cpp に
**意図して残した** (report/p3.md の T1 節)。wasm ではこれらを
`FMRB_PLATFORM_WASM` ガードで外す:

- パネル起動・バックライト、`display_p4_lcd()` / `display_p4_panel_framebuffer()`
  を呼ぶ経路、I2C service (display_p4_i2c_*)、ヘッドホン検出、
  ブート画面の実パネル描画、ppa_verification_test。
- 外した display_p4_i2c_* の extern を参照する他モジュールがあれば、
  エラーを返すスタブで受ける (シンボルは保つ)。

## 作業の分割

### T1: ソース集合の確定とコンパイル通過

- wasm/CMakeLists.txt に core のソースを集める。基準は **Linux ビルドの
  compile_commands.json のファイル集合**から、posix 差し替え分を除いたもの
  (集合の抽出はスクリプト化して report に方法を残す)。
- 差し替え:
  - link: fmrb_hal_link_posix.c → **fmrb_hal_link_local.c** (中身は
    MessageBuffer のみで機種非依存)。拡張 API のガードは
    fmrb_hal_link.h:93 の `#if defined(FMRB_HW_ATOM_DISPLAY) || defined(FMRB_HW_MODERN)`。
    wasm で立てる HW マクロ (次項) がこれを満たすかを先に確認し、
    満たさなければ wasm を条件に追加する。
  - 入力: usb_task_linux.c → スタブ (P4b で backend/input_wasm.c)。
  - display: display_p4_task.cpp (受信ループ + コマンド解釈) +
    display_p4_sprite/vm.cpp + display_blend_cpu.c。display_p4_video.cpp は
    外し、動画コマンドはエラー応答。
  - audio: apu_emu と audio_p4_task.c を含め、audio_backend_t を
    スタブ表 (write は捨てる) で差し替え。
  - **入れない**: sim_log_guard.c、--wrap リンカフラグ、debugd/TCP 系、
    MIDI serial (fmrb_midi_sched は入れて出力だけスタブ)、マイク。
- 定義: `FMRB_PLATFORM_WASM` を新設し、Modern の習慣を引き継ぐ
  (components/fmrb_common/fmrb_hw_defines.cmake の分岐と、既存の
  FMRB_HW_MODERN / FMRB_HW_FAMILY_MODERN の使われ方を調べて、wasm で
  立てるべき組み合わせを決める。**既存ターゲットの定義は変えない**)。
- stub の拡張は wasm/stub/ に追加 (P1 の一覧が土台。esp_log は
  printf/console へ、esp_random / esp_read_mac / esp_restart /
  esp_err_to_name など)。何を足したか report へ。
- この段階の合格: 全ソースがコンパイルされる (リンクはまだ)。

### T2: 言語処理系のクロスビルド

- mruby/picoruby: build_config/picoruby-wasm.rb を土台に
  **lib/add/family_mruby_wasm.rb** を作る。mrbc はネイティブのまま。
  - **MRB_* レイアウト定義は wasm CMake 側と完全一致させる** (二重ビルド
    ABI 罠。mruby_abi_defines.cmake の linux 節を写す)。
  - **FMRB_TASK_SELF_TICK=1 を焼き込む** (rake 側 = family_mruby_wasm.rb、
    CMake 側 = wasm/CMakeLists.txt の task_hal.c コンパイル)。
  - MRB_TICK_UNIT / MRB_TIMESLICE_TICK_COUNT も linux と同値 (5/10) に。
- Spinel: 生成はホストの rake spinel:gen のまま。wasm CMake 側で
  `SP_NO_MMAN` + `SP_STACK_SCRATCH_MAX=512` を定義し、sp_fiber.c /
  sp_sched.c を集合から外す (components/fmrb_spinel_rt/CMakeLists.txt:118 の
  esp32 節と同じ扱い。あちらの条件式は wasm が独立 CMake なら触らなくてよい)。
  sp_system.c の fork/exec は失敗を返すスタブに。
- MicroPython: MICROPY_NLR_SETJMP=1、native emitter 無効。
  Lua: popen/system/dlopen 無効。BASIC はそのまま。
- 合格: リンクが通り、undefined reference ゼロ。

### T3: ブート配線と node 実行

- boot.c (main/boot/boot.c) に wasm 分岐: display (受信ループ + wasm
  backend)、audio (スタブ表)、input (スタブ) の init。
- display の wasm backend: display_backend_t を実装する薄い 1 ファイル。
  blend_block = display_blend_cpu、present / present_patch = フレームを
  保持するだけ (P4b で Canvas へ)、init = fb の malloc、first_frame /
  shutdown = no-op。
- FS: `--preload-file flash@/flash` で MEMFS へ。fmrb_hal_file_posix.c /
  fmrb_tmpfs_posix.c はそのまま。
- Emscripten 設定: `-pthread -sPROXY_TO_PTHREAD -sENVIRONMENT=node`、
  `-sPTHREAD_POOL_SIZE` は Linux sim の実測タスク数 (15〜25) + 余裕。
  タスクスタックは xTaskCreate 指定値のまま (下限 64KB は port が保証)。
  **.app.toml の task_stack_kb がそのまま pthread スタックになることを確認し、
  不足が出たら wasm 側の下限か読み替えで対処** (P1 の申し送り。特に
  prism/エディタ系は要注意、ti:wasm は 1MB の前例)。
- `rake wasm:build` / `rake wasm:run` (node 実行) を rakelib/wasm.rake に追加。

### T4: 受け入れと安定確認

下記の受け入れ条件を満たすことを確認し、急所 1 (Spinel 協調) の確認結果と
合わせて report にまとめる。

## 受け入れ条件

1. `rake wasm:build` → `rake wasm:run` で node がブートし、
   `main_loop started` 相当のブートマーカーが出る。
2. `M1|spawn:fmrb_kernel` と `M1|spawn:system_desktop` 相当の spawn ログが
   並ぶ (M1| の数字は heap_wasm の予算値で可)。
3. そのまま 60 秒回して、周期ログが出続け (fmrb_task: の Free 列は無効で
   よい)、crash マーカー (Guru / abort / assert / node の例外) 0 件。
4. 標準構成 (Spinel カーネル + Spinel エディタ) で 1〜3 を満たす。
   どうしても詰まる場合は全 mruby 構成で 1〜3 を満たした上で、Spinel 側の
   詰まりの切り分けを report に書く (撤退線)。
5. rake build:linux / build:esp32 (TAB5) が従来どおり通る (wasm 分岐の
   追加が既存ターゲットに漏れていないこと)。ソース側に足した
   `FMRB_PLATFORM_WASM` ガードは、既存ビルドではコンパイル結果を変えない。

## report に書くこと

- ソース集合の決め方 (compile_commands.json からの抽出手順) と、
  除外・差し替え・スタブの全リスト (P4b/P4c がこの逆引きで作業する)。
- Spinel 協調適合の確認結果 (どの関数がどこで FreeRTOS 待ちに入るか)。
- LGFX 切り出しの決着 (採った構成、外したもの、詰まった点)。
- スタック関連の実測 (どのタスクにいくら与えたか、64KB 下限で足りたか)。
- ブートにかかる実時間と、node での挙動の癖 (P4b の計画材料)。
- 詰まった点・指示から変えた点。

## やらないこと (P4a の範囲外)

- ブラウザ実行・Canvas/入力 (P4b)、WebAudio (P4c)、配信 (P5)。
- 性能改善 (present の間引き、dirty region 化)。数字の記録だけする。
- Spinel の協調対応の実装 (穴が見つかった場合は全 mruby へ切替えて前進)。
- IDBFS 等の永続化。/tmp の揮発性はそのまま。
