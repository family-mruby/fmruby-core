# 解説 4: 関連コミットの日本語解説

> 状態: 完了 (P4 時点) | 更新: 2026-08-29 | 外部説明用の解説群 その 4

wasm 対応のコード変更コミット (fmruby-core、develop) を時系列で解説する。
文書だけのコミット (report・計画の反映) は省き、末尾に一覧だけ置く。

## P1: FreeRTOS 移植の単体証明

### e00f8685 — wasm: cooperative FreeRTOS port for Emscripten, with a proof of concept (P1)

移植の核心を 1 コミットで持ち込んだ。中身は 4 群:

- **wasm/port/** — Emscripten 協調 port の新規実装 (解説 2 の主題)。
  1 タスク = 1 pthread (Web Worker)、単一実行、実時間追いつき tick、
  pthread_cancel を使わないタスク他殺。
- **wasm/vendor/** — ESP-IDF v5.5.4 同梱の FreeRTOS カーネルのコピー
  (無改変)。素の上流でなく IDF 版なのは、xTaskCreate や TLS 削除
  コールバックの実体が IDF 拡張 (esp_additions) 側にあるため。
- **wasm/poc/poc_main.c** (804 行) — core が使う API 群の検査 23 項目。
  notification の往復、96 スロットの counting semaphore (GFX フロー制御の
  縮小再現)、TLS 削除、MessageBuffer、他殺、協調の限界の実測など。
  node で機械判定でき、5 回連続 PASS を受け入れ条件にした。
- **rakelib/wasm.rake + wasm/CMakeLists.txt** — idf.py から完全に独立した
  ビルド。emsdk / node の版は report の数字が再現するよう固定。

このコミットの成立が「wasm 対応全体を続けるか」の撤退線だった。

## P2: 自給タイムスライス

### 8e391c41 — mruby-task: let a VM supply its own timeslice, behind FMRB_TASK_SELF_TICK

協調 port で唯一成立しない「CPU を握った Ruby を外から剥がす」を、
VM の自己申告に置き換えた。読みどころは 3 つ:

- **lib/patch/.../src/vm.c (全文 patch 化、加筆 4 箇所)** — バイトコード
  境界 (RETURN_IF_TASK_STOPPED) に間引き計数付きの hook を差した。
  計数の初期値が 1 なのが急所: タスク再開直後の 1 命令目で必ず発火させ、
  「走っていない間に溜まった切替要求」を配達する瞬間を作る
  (スケジューラは execute_task のたびに switching を消すので、
  そこからは配達できない)。未定義時は 1 バイトも変わらない。
- **task_hal.c** — 時計換算の本体。要求を立てる (switch_pending) のと
  mrb->task.switching へ書く (配達) のを分離した。tick の供給は
  vm.c の hook / スケジューラのループ先頭 / アイドル待ちの後の 3 箇所
  (どれか 1 つ欠けると「眠ったタスクが起きない」「起床が 1 巡遅れる」の
  穴になる。経緯は report/p2.md)。
- **フラグ配線** — 環境変数 FMRB_TASK_SELF_TICK 1 個が rake 側 (vm.c を
  焼く) と CMake 側 (task_hal.c を焼く) の両方に届く。既定は完全に
  従来どおりで、ESP32/Linux の挙動は変えていない。

### 30962163 — test: apps that exercise the self-supplied timeslice

検査アプリ 4 本 (slice_probe / task_sleep / spin_hog / vm_bench) を
flash/app/test/ に常設した。task_sleep は P2 で見つけた穴 2 つを両方踏む
回帰検査で、実機 (Tab5) でもそのまま走らせて受け入れに使った。

## P3: 表示バックエンドの分割 (実機リファクタ)

### 605c7d23 — display_p4: split the output path behind a display backend interface

display_p4_task.cpp から「PPA 合成 + 3x 回転出力 + カーソル高速パス」を
display_backend_t (init / first_frame / blend_block / present /
present_patch / shutdown) の背後へ移設。パネル起動・I2C・ヘッドホンは
「PPA ではなく Tab5 のハードウェア」なので task 側に残す、という線引きを
した。移設に徹し (挙動変更ゼロ)、実機 5 画面の 0 px 一致で受け入れ。

### 448cd705 — display_p4: a CPU display backend, pixel-exact with the PPA

wasm で必要になる CPU 合成を、**先に実機の上で**書いて検証したコミット。
核は display_blend_cpu.c (ESP-IDF を include しない素の C。カラーキー付き
矩形ブリット)。実機で PPA と全画面 0 px 一致 — 争点だったカラーキーは
「帯の生成規則 [c<<k, (c<<k)|mask] なら 8bit 展開方式の差が現れない」
ことの確認で決着した。FMRB_DISPLAY_BACKEND=cpu の選択機構つき。

### 8aeeb90d — audio_p4: gather the hardware output touchpoints into a backend table

音の出口 4 点 (init / ready / write / set_volume) を audio_backend_t に
集約。実装が 1 つの間は選択機構を作らず、「wasm が来たら表を差し替える」
だけの最小の抽象化に留めた。

### 077fa29e — rake: flash only the app partition, keeping the device's /home

P3 の検証を回すための道具: app 区画だけ焼く rake flash:app。storage
(端末の /home) を消さないので、コード変更の反復書き込みが安全になった。

## P4: core 統合 — ブラウザで動くまで

### 5b74a4c3 — wasm: the core sources learn the wasm platform

共有ソース側の受け入れ準備。FMRB_PLATFORM_WASM ガードの追加 (boot.c の
init 分岐、display_p4_task の実機ハードウェア領域の除外、ファイル基準
パス)、link ガードへの wasm 追加、libmruby のクロスビルド設定
(family_mruby_wasm.rb。MRB_32BIT と MRB_TASK_TICK_SELF_SUPPLY の焼き込みが
要点)。既存ターゲットのバイナリを変えないことを実機 0 px 回帰で担保。

### 3f91408e — wasm: P4 core integration -- boot, display, input and audio under node

統合の本体 (28 ファイル、+2059 行)。wasm/CMakeLists.txt が Linux ビルドの
ソース集合 + Modern の表示・音系を束ね、wasm/backend/ の 8 ファイル
(process 入口 main_wasm、RGBA+連番の表示、Atomics リングの入力、15720Hz
リングの音、LGFX 共通層、video/rd スタブ)、ESP-IDF 顔のスタブヘッダ群、
ブラウザページ (index.html + main.js + AudioWorklet)、headless 検証道具
(drive.js / framedump.js) を追加。ここで node ブート → デスクトップ描画 →
打鍵 → 発音まで通った。

### a4eb73ac — wasm: locate the packed data file next to the script, not the page

3 行の修正。Emscripten は .data をページ URL 基準で探すため、ページと
ビルド成果物を別ディレクトリに置く構成では locateFile で場所を教える
必要がある。ブラウザ実確認で見つかった唯一の修正だった。

## 文書コミット (参照用一覧)

| ハッシュ | 内容 |
|---|---|
| b4f2fb0 ほか | 指示書群 (instruction_p1〜p4a) |
| 05859e9 / ea93bb34 / 4e34d820 / 4e05de4b | 各フェーズの report と計画反映 |
| 570cfab0 | ブラウザ実機確認 (表示・マウス・キー・音) の記録 |
| cfaf44d8〜 | P5 (配信) 計画への改善要望の取り込み |
