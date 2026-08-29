# P4a 報告: core 統合 — ヘッドレスでブートまで (node)

> 状態: 完了 (受け入れ 5 点すべて満たす)。P4b/P4c も先行実装済みで、
> **ブラウザでの実操作 (マウス・キーボード) と音の可聴はユーザが実ブラウザで
> 確認済 (2026-08-29)** | 更新: 2026-08-29

指示書は instruction_p4a.md。実装は指示書の公開と並行に進んだため時系列は
前後するが、結果は指示書の受け入れ条件・確認項目に沿って本書で答える。

## 受け入れ条件の確認

| 条件 | 結果 |
|---|---|
| 1. rake wasm:build/run でブート | `rake wasm:mruby` → `rake wasm:core` → node 実行でブート。デスクトップ描画まで到達 |
| 2. kernel + desktop の spawn ログ | `fmrb_app_spawn: name=fmrb_kernel` / `name=system_desktop` とも出る (VM 生成・RUNNING 遷移まで) |
| 3. 60 秒安定 | 90 秒run で周期ログ (fmrb_transport stats 5s、fmrb_task dump) 継続、crash/例外 0 件 |
| 4. 標準構成 (Spinel カーネル + Spinel エディタ) | この構成のまま通った。**全 mruby への撤退は不要だった** |
| 5. 既存ビルド無影響 | rake test 全通過、rake build:linux (x86-64 確認)・build:esp32 (TAB5) とも通り、実機 desktop 基準 **0 px** |

## 急所 1: Spinel 協調適合 (コードで確認)

**イベント駆動でループごとに FreeRTOS の待ちに入る構造で、剥がす必要が
生じない**。待ち箇所:

- カーネル VM: Ruby ループ → FFI `fmrb_spx_recv_message`
  (main/kernel/fmrb_spx_kernel.c:34) → `fmrb_msg_receive`
  (components/fmrb_msg/fmrb_msg.c:297) → `xQueueReceive` (timeout つき)。
- Spinel アプリ (エディタ): 同型で `fmrb_spx_app.c:156` の
  `fmrb_msg_receive`。
- 実証: この待ちがあるからこそ services の heartbeat・desktop の描画・
  エディタの打鍵が同時に回る (協調 port では待ちに入らないタスクが 1 本でも
  あると全員止まる — 下記「busy-poll は即死」で実地に確認した)。

## ソース集合の決め方 (P4b/P4c はこの逆引きで作業する)

Linux ビルド後の build/compile_commands.json から:

```ruby
ruby -rjson -e 'cc = JSON.parse(File.read("build/compile_commands.json"))
  puts cc.map { |e| e["file"] }.uniq.select { |f| f.start_with?("/project/") }'
```

/project 配下 316 ファイルが基準。そこからの差し替え・除外・追加の全リスト:

| 種別 | 対象 |
|---|---|
| 差し替え | fmrb_hal_link_posix.c → platform/esp32/fmrb_hal_link_local.c / usb_task_linux.c → wasm/backend/input_wasm.c |
| 除外 | sim_log_guard.c (+ --wrap フラグ)、x509_crt_bundle、lua の onelua/lua/ltests、socket・net-http・net-websocket gem (mruby 側ごと) |
| 追加 (Modern の体) | display_p4_task/vm/sprite.cpp、display_backend.cpp、display_blend_cpu.c、misaki フォント、host_file_local.c、apu_emu 6 ファイル、audio_p4_task/handler.c |
| 追加 (wasm 固有) | wasm/backend/ 8 ファイル (main/display_backend/audio_backend/input/video スタブ/rd_encoder スタブ/lgfx common) |
| スタブ (ヘッダ+実体) | esp_log/err/system/timer/random/mac/attr/cache/cache_private/heap_caps 拡張 + esp_stub.c (log/clock/restart/random/mac/sysinfo) |
| debugd | fmrb_debugd.c + TCP transport は**コンパイルだけする** (エディタの手元デバッガが acquire_local を使う)。fmrb_debugd_init は wasm では呼ばない |
| MIDI | fmrb_midi_sched/serial とも素通し (serial は FIFO open が実行時に失敗して自然に無効) |

## 定義の組み合わせ (急所 3 と T1 の決着)

- `CONFIG_IDF_TARGET_LINUX` を**定義する** (45 ファイルの posix/esp32 分岐を
  linux 側へ倒す)。`ESP_PLATFORM` も定義 (IDF は linux でも定義しており、
  vendored FreeRTOS.h の idf_additions 取り込みの条件)。
- `FMRB_PLATFORM_WASM` で linux と違う所だけ上書き: boot.c の init 分岐、
  sim_log_guard / debugd 起動 / getitimer 系のスキップ、display_p4_task の
  HW 領域 (パネル起動 / boot screen / I2C service / ヘッドホン / タッチ)、
  host_file_local の base path (相対 "flash")。
- `FMRB_HW_FAMILY_MODERN` を立てる (services が spawn する)。FMRB_HW_MODERN
  は立てない。fmrb_hal_link.h:94 のガードに FMRB_PLATFORM_WASM を追加した。
- display_p4_i2c_* / poll_headphone は NOT_SUPPORTED を返すスタブでシンボル
  維持 (指示書どおり)。既存ターゲットは 0 px 回帰で無影響を確認。

## 急所 2: LGFX sprite-only の決着

- **SDL は持ち込まなかった** (推奨どおり)。M5GFX (実機と同系、
  d91077b9 / 2026-08-25) を `rake wasm:setup` が shallow clone し、
  platform 選択チェーン 2 箇所 (platforms/common.hpp / device.hpp) の先頭に
  `FMRB_LGFX_WASM` 分岐を機械 patch する (vendor はコミット外)。
- common 層は framebuffer platform のヘッダを流用し (linux 依存なし)、実装は
  wasm/backend/lgfx_common_wasm.cpp (clock_gettime / nanosleep / gpio no-op)。
- コンパイルするのは LGFXBase / LGFX_Sprite / lgfx_fonts (+efont JA データ) /
  misc 4 つ / Panel_Device / utility (pngle, qoi, tjpgd, miniz, qrcode) と
  少数。バス・パネル・タッチは一切入れない。
- g_lcd は素の `lgfx::LGFX_Device` (パネル無し)。**sprite の親としてしか
  使わなければ問題は出なかった**。`display_p4_panel_framebuffer()` は NULL。
- **drawPng がこの構成でそのまま動く** (壁紙 bg_426x240.png と boot ロゴで
  確認)。JPEG (HW デコーダ) は非対応スタブ。

## 言語処理系 (T2)

- libmruby: lib/add/family_mruby_wasm.rb (emcc CrossBuild)。linux 構成 −
  socket 系 + `MRB_32BIT` + `MRB_TASK_TICK_SELF_SUPPLY` 焼き込み。
  `conf.ports :posix` のまま**初回ビルドが一発で通った**。
  - 罠 1 件: アーカイブ内で素の allocf.o と estalloc の alloc.o が
    `mrb_basic_alloc_func` を二重定義しており、GNU ld は lazy load の偶然で
    通るが wasm-ld は拒否 → rake wasm:mruby が `emar d libmruby.a allocf.o`。
- Spinel: 生成 C はホスト生成のものをそのまま、`SP_NO_MMAN` +
  `SP_STACK_SCRATCH_MAX=512` + MULTI_CTX/-include を per-file で付与
  (main/CMakeLists.txt の esp32 節の写し)。sp_fiber / sp_sched を除外。
  sp_system.c は emscripten の fork/exec が元々失敗するスタブなので無改変。
  **Spinel 生成 C は無修正で wasm 動作** (カーネル VM + エディタで実証)。
- MicroPython: `MICROPY_NLR_SETJMP=1` のみ (native emitter は元々ゲート済)。
  Lua / BASIC: 無修正で通った。

## ブート配線と実行 (T3)

- main() は app_main を直接呼ばず、タスク 1 本 (stack 256KB) を作って
  `vTaskStartScheduler()` (PoC と同じ形)。**直接呼ぶと即クラッシュ**
  (スケジューラ未初期化のままタスクのスレッドが走り出す)。
- node 用 `core` は **NODERAWFS** (実 flash/ を cwd 相対で読む。sim と同じ
  場所で回せる)、ブラウザ用 `core_web` は `--preload-file flash@/flash`。
  コンパイルは OBJECT ライブラリ 1 回、リンク 2 本。
- Emscripten: -sPROXY_TO_PTHREAD、pool 40、INITIAL_MEMORY 256MB +
  ALLOW_MEMORY_GROWTH、-sASSERTIONS + --profiling-funcs (開発ビルド)。
  emscripten 6 は既定で何も export しないので EXPORTED_FUNCTIONS /
  EXPORTED_RUNTIME_METHODS を明示。node の require() が Module を返す
  (global Module 方式は不発)。
- スタック: 全タスク port の下限 64KB クランプで**そのまま足りた**
  (Spinel エディタの起動・打鍵、prism を含む mruby VM 生成まで不足なし。
  .app.toml の task_stack_kb 読み替えは不要だった)。
- ブート実時間 (node/WSL2): 最初のログまで ~1s、デスクトップ on_create まで
  さらに ~0.5s。8 秒後には 36 フレーム提示済み (render ~33 frames/5s)。

## 実装で見つけた協調ポートの罠 (最重要の申し送り)

**link 初期化前の `fmrb_hal_link_local_receive_cmd` はブロックせず
INVALID_STATE を返し、display タスクがスピンして boot タスクが二度と
走れなくなった** (ログが「entering command receive loop」で止まる)。
エラー時に 10ms delay を入れて解決 (display_p4_task.cpp。実機にも無害)。
「全タスクがどこかで必ず FreeRTOS の待ちに入る」ことが協調 port の生存条件で、
今後タスクを足すときは毎回この観点で見ること。

## 検証道具 (sim_input / sim_screenshot 相当)

- `node wasm/tools/framedump.js [秒] [out.png]` — ブート後のフレームを PNG に。
- `node wasm/tools/drive.js <cmds>` — wait / move / click / key / text /
  shot / rec (音の録音 + 100ms 窓の RMS・周波数)。
- ブート健全性は node の例外 0 + `fmrb_transport stats` の周期継続で見る
  (fmrb_task: の Free 列は P1 の予告どおり無意味な値)。

## P4b/P4c のコア側 (先行実装、指示書は未発行)

指示書 P4a の範囲外だが、ユーザ指示が「P4 実装」だったため続けて実装した。
**instruction_p4b/p4c を書く場合はここが入力になる。**

- 表示 (P4b): display_backend_wasm.cpp が present で RGB565→RGBA8888 変換 +
  フレーム連番。JS は連番を rAF で見て putImageData (計画どおり)。
  カーソル fast path (present_patch) は矩形だけ RGBA 更新。
- 入力 (P4b): wasm/backend/input_wasm.c の 20B 固定長リング (256 発、JS が
  Atomics で wr を進め、10ms ポーラが fmrb_host_send_* へ)。キーの意味づけは
  rd_input.c と同じ (keycode=scancode=HID usage、mod は FMRB_KEYMAP_MOD_*)。
  keymap は tool/web/remote/keymap.js を再利用。
  **headless 検証済**: メニュー開閉、FM-Editor (Spinel) 起動、"puts" 打鍵、
  Ctrl+Tab (`host: Ctrl+Tab detected` が出る)。
- 音 (P4c): audio_backend_wasm.c が 15720Hz int16 リング (16384 標本 ≈1s)
  に書き、AudioWorklet (wasm/web/audio-worklet.js) が線形補間で吸う。
  **数値検証済**: 起動ジングルを drive.js の rec で録り、DFT で
  C3→F3→G3→C4 (265±5Hz、理論値 261.6Hz) を確認。
- ページ (P4b/P5 の入口): wasm/web/index.html + main.js (canvas 2x、
  クリックで AudioContext resume)。`rake wasm:web` でバンドル
  (core_web.wasm 5.3MB + .data 1.4MB)、`rake wasm:serve` が COOP/COEP つき
  で配信 (SharedArrayBuffer の条件)。
- **ユーザ確認済 (2026-08-29)**: 実ブラウザで動作、音・マウス・キーボード
  とも OK。初回に踏んだ穴が 1 つ: MODULARIZE の emscripten は .data を
  ページ URL 相対で fetch するため 404 で黒画面になった (locateFile で
  ../build/ を指して解決、a4eb73ac)。かな入力・Ctrl+Q などの細部の網羅は
  P5 のデモ仕上げで見る。

## やり残し / スコープ外の現状

- BASIC PLAY 文の発音確認 (drive.js で BASIC を起動して打鍵すれば headless
  で録れるはず。未実施)。
- 動画 / EXPORT_FRAME はエラー応答、マイクは 0 台/空、HID デバイス一覧は
  0 台、ネットワークは services の net が起動するが接続不可 (想定内)。
- 性能改善はしていない (数字: render avg 0ms/frame 表示。合成 426x240 は
  wasm でも軽い)。
