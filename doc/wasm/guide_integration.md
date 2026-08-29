# 解説 3: core 統合と入出力 — wasm ビルドの中身

> 状態: 完了 (P4 時点の実装を記述) | 更新: 2026-08-29 | 外部説明用の解説群 その 3

FreeRTOS port (解説 2) の上に core 本体を載せた部分の解説。
「どのソースを、どの定義で、どう繋いだか」と、入出力の実装、
ページと道具の順に見る。

## 定義戦略: 「linux の顔で Modern の体」

wasm ビルドは既存の 2 構成のいいとこ取りで組んである。

- **CONFIG_IDF_TARGET_LINUX と ESP_PLATFORM を定義** — 45 ファイルにある
  posix/esp32 分岐を Linux sim と同じ側に倒す (時刻、ファイル、/tmp など)。
- **体は Modern (Tab5)** — 表示は 3 プロセス構成の Linux sim ではなく、
  Tab5 のプロセス内経路をそのまま使う: link は fmrb_hal_link_local
  (FreeRTOS MessageBuffer)、消費側は display_p4_task (受信ループ +
  コマンド解釈 + 合成)。FMRB_HW_FAMILY_MODERN を立てるのでサービス群も
  Modern と同じに振る舞う。
- **FMRB_PLATFORM_WASM で差分だけ上書き** — boot.c の init 分岐、
  sim_log_guard / debugd 起動のスキップ、display_p4_task 内の Tab5 実機
  ハードウェア領域 (パネル起動・ブート画面・I2C・ヘッドホン・タッチ) の
  除外、ファイルの基準パス (相対 "flash")。既存ターゲットのコンパイル
  結果は変えない (実機 0 px 回帰で確認済み)。

ソース集合は Linux ビルドの compile_commands.json (316 ファイル) を基準に、
posix 差し替え分を除き、Modern の表示・音系と wasm/backend/ を足したもの。
全リストは report/p4a.md の表が正。

## ビルドの部品 (rake wasm:*)

| タスク | 何をするか |
|---|---|
| wasm:setup | LovyanGFX (実機と同系の M5GFX、コミット固定) を取得し、platform 選択 2 箇所に FMRB_LGFX_WASM 分岐を機械 patch |
| wasm:mruby | libmruby を emcc でクロスビルド (lib/add/family_mruby_wasm.rb) |
| wasm:core | core 本体を emcmake でビルド → node 用 `core` |
| wasm:web | ブラウザ用 `core_web` (+ .data) とページを束ねる |
| wasm:serve | COOP/COEP ヘッダ付きの開発サーバ (SharedArrayBuffer の条件) |
| wasm:run / wasm:poc | node 実行 / P1 の検査 23 項目 |

要点:

- **libmruby (family_mruby_wasm.rb)**: linux 構成からソケット系 gem を抜き、
  MRB_32BIT (wasm32 は ILP32) と **MRB_TASK_TICK_SELF_SUPPLY を焼き込む**
  (協調 port では別タスクの tick 係が走れないため、wasm では自給が必須)。
  レイアウトを決める MRB_* 定義は wasm/CMakeLists.txt 側と一致させる
  (実機の mruby_abi_defines.cmake と同じ規律)。
- **LovyanGFX は sprite-only**: SDL は持ち込まず、LGFX_Sprite / フォント /
  画像デコーダ (pngle 等) だけをコンパイル。g_lcd はパネル無しの素の
  LGFX_Device で、「sprite の親」としてだけ使う。壁紙の drawPng も
  この構成でそのまま動いた。
- **Spinel**: ホストで生成した C を無修正でコンパイル (SP_NO_MMAN +
  ファイバ系 2 ファイル除外、ESP32 節の写し)。カーネル VM とエディタが
  wasm で動いていることが「Spinel 生成 C の可搬性」の実証になっている。
- 実行ファイルは 2 本: node 用 `core` は NODERAWFS (実 flash/ を直接
  読み書き = 開発検証用、永続化あり)、ブラウザ用 `core_web` は
  --preload-file (MEMFS = 読み書き自由だがリロードで消える)。

## 入出力: 3 本のリングでページと繋ぐ

wasm/backend/ の 8 ファイルが、実機ではハードウェアだった部分を
SharedArrayBuffer 上のリングバッファに置き換える。JS との共有はすべて
「wasm 側が配列と書き込みカウンタを公開し、相手が番号を見て差分だけ
処理する」形で、ロックは使わない。

### 表示 (display_backend_wasm.cpp)

P3 で作ったバックエンド境界 (display_backend_t) の wasm 実装。合成は
**実機の CPU バックエンドと同じ display_blend_cpu.c** を呼ぶだけ。present は
完成した RGB565 フレームを RGBA8888 の固定番地バッファへ変換し、
フレーム連番を +1 する。カーソル高速パス (present_patch) は 16x16 の
矩形だけ変換する。JS 側 (main.js の paint) は requestAnimationFrame ごとに
連番を見て、変わっていたら putImageData で Canvas へ写す。

### 入力 (input_wasm.c)

usb_task_linux.c の代役。JS が 20 バイト固定長の記録 (キー押下/解放、
マウス移動/ボタン) を 256 要素のリングに書き、書き込みカウンタを
Atomics.add で進める。wasm 側の 10ms ポーラタスクが差分を取り出し、
実機と同じ入口 fmrb_host_send_* へ流す。キーの意味づけは remote desktop
(rd_input) と同じ「scancode = HID usage ID + FMRB_KEYMAP_MOD_*」で、
ブラウザの KeyboardEvent.code → HID 変換表は既存の keymap.js を再利用。

### 音 (audio_backend_wasm.c)

P3 で表化した audio_backend_t の wasm 実装。APU が合成した 15720Hz mono
int16 を 16384 標本 (約 1 秒) のリングに書くだけで、**書き手は決して
ブロックしない** (node で誰も読まなくても害がない)。ブラウザでは
AudioWorklet (audio-worklet.js) がリングを吸い、線形補間で出力レートへ
変換する。AudioContext はブラウザの自動再生制限のため初回クリックで開始。

### スタブ群 (wasm/stub/)

ESP-IDF の顔だけを持つ最小実装: esp_log (printf へ)、heap_caps (単一の
malloc に集約、空き容量は予算値)、esp_random / MAC / restart / cache 系。
video (HW JPEG) と rd_encoder はエラー応答のスタブ。sdkconfig.h は
「無効な CONFIG_* は定義しない」(0 定義は #ifdef で有効扱いになる罠)。

## ページ (wasm/web/) と検証道具 (wasm/tools/)

- index.html + main.js (154 行): 仕事は 3 つの橋渡しだけ —
  フレームを Canvas へ、入力をリングへ、音のリングを Worklet へ。
  firmware のロジックは 1 行も持たない。
- 検証は headless で完結する: framedump.js (ブート後のフレームを PNG に)、
  drive.js (クリック/打鍵/スクリーンショット/録音 + 周波数解析。
  sim_input / sim_screenshot の wasm 版)。起動ジングルの音程確認
  (C3→F3→G3→C4) も drive.js の録音を DFT にかけて行った。

## 協調 port と core の接点で踏んだ罠 (再掲)

link 初期化前の受信がブロックせずエラーを返し、display タスクがスピンして
boot タスクが二度と走れなくなった (協調 port では「待ちに入らないタスクが
1 本いると全員止まる」)。エラー時に 10ms の delay を入れて解決。
**今後タスクを足すときは「このループは必ずどこかで FreeRTOS の待ちに
入るか」を毎回確認すること。** この移植で得た一番大事な運用知見である。
