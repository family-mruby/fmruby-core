# 実装指示書 P3: display backend 分割 (P4 実機リファクタ)

対象: 実装担当セッション。前提: plan.md と implementation_plan.md の P3 節を
読むこと。wasm とは独立の Tab5 (ESP32-P4 / Modern) リファクタで、実機の回帰
確認で完結する。タスクごとにコミット (push はしない)。report は
doc/wasm/report/p3.md へ。

## ゴール

display_p4_task.cpp (3543 行) から「パネル起動 + PPA + DSI 出力」をバックエンド
として分離し、PPA 実装と CPU 合成実装を並べ、**両方が Tab5 実機で正しく動く**
状態を作る。CPU 実装は wasm 描画経路の一番重い新規部分の実機事前検証になる。

## 現状の在庫 (grep 確認済みの anchor)

PPA / DSI 出力への依存は display_p4_task.cpp に閉じている
(display_p4_sprite.cpp / display_p4_video.cpp / display_p4_vm.cpp に
ppa / g_dsi_fb の直接参照は無い):

- 初期化: PPA client 登録 + DSI FB 取得 (display_p4_task.cpp:2479-2505、
  g_lcd.getFrameBuffer)。
- 合成: blend_canvas_block (:666) → ppa_do_blend (:755)。
  **g_ppa_blend==NULL だと無描画で return (:676)** — CPU 実装が埋める穴。
- 出力: render_frame (:778) 内の SRM 3x 拡大 + 90 度回転 (:919-964) と
  esp_cache_msync (:783-788)。
- カーソル: cursor_patch (:502) が g_dsi_fb (出力面座標) へ直接書く。
  capture 用の bake/restore (:609-658) も同族。
- ブート画面 (:3174-3246)、起動時の ppa_verification_test (:2580-3067)。
- 周辺 HW: I2C service (:3069)、ヘッドホン検出 (:3114)、バックライト、
  パネル起動 — Tab5 HW 固有なので PPA バックエンド側へ。
  display_p4_i2c_* は外部から呼ばれる extern なのでシンボルを維持する。
- 動画 (display_p4_video、HW JPEG) と export_frame_jpeg (:1269) は PPA/HW 固有。

## 設計 (決定済み)

- `display_backend_t` を新設: init / alloc_fb / blend_block (color-key 付き) /
  present / shutdown (GA 側 display_interface.h 相当)。カーソルの出力面書込みや
  cache 同期のために I/F が増えるのは構わないが最小限にし、確定形を report に
  記す。
- display_backend_ppa.cpp: 現行コードの**移設に徹する** (挙動を 1 bit も
  変えない。リファクタと機能変更を混ぜない)。
- display_backend_cpu.cpp: CPU 合成 + pushRotateZoom で DSI へ出力。
  **合成の中身 (color-key 付き矩形ブリット、RGB565) はターゲット非依存の C に
  分離する** (例: display_blend_cpu.c。IDF ヘッダを include しない。P4 でも
  wasm でもそのまま使う)。viewport のトーラス分割 (render_frame の分割送り) と
  sprite clip のセマンティクスを保存する。ppa_verification_test は CPU 側では
  実行しない。
- バックエンド選択はビルド時定数。実機デバッグ用に .env → rake → CMake define
  で CPU 強制を選べるようにする (**sdkconfig は編集禁止**)。
- capture API (display_p4_task.h:74-114) は無傷で維持する — 本フェーズの検証
  手段そのものが capture (remote desktop) に乗っている。
- 音は深追いしない: apuif_set_output_writer (audio_p4_task.c:470) で半分
  抽象化済みなので、audio_p4_hw.c の init/ready/volume を関数表に寄せるだけ。
- 動画 / EXPORT_FRAME は PPA バックエンド固有機能とし、CPU バックエンドでは
  fmrb_err.h のエラー応答にする。

## タスク

### T0: 基準画像の採取 (リファクタに手を付ける前に必ず)

現行 develop の Tab5 実機で、比較対象アプリの画面を tab5_screenshot
(または tools/fmrb_rd_snap.rb) で取り、tmp/ 以下 (コミット外) に保存する。
対象: デスクトップ / エディタ / PicoRabbit / スプライト系デモの 4 種。
各画面は静止した状態 (アニメーションの止まる画面) を選ぶ。

### T1: 分割と PPA バックエンドの回帰

display_backend.h を新設し、出力系を display_backend_ppa.cpp へ移設する。
受信ループ + コマンド解釈 (process_message / process_gfx_command) は
display_p4_task.cpp に残す。この時点で PPA ビルドを実機に焼き、T0 の基準と
tools/fmrb_pngdiff.rb で差分ゼロを確認する。

### T2: CPU バックエンド

display_blend_cpu.c (ターゲット非依存の合成核) + display_backend_cpu.cpp
(P4 での出力: pushRotateZoom)。CPU 強制ビルドで同じアプリ群を実機確認する。

### T3: 選択機構と両構成ビルド

.env / define での切替。TAB5 の PPA / CPU 両ビルドに加えて、S3 (.env 切替、
rake clean_all) と linux のビルドが通ることも確認する (display_p4 は Modern
専用だが、ビルド配線を触るため)。

## 検証

- 実機操作は MCP ツール (flash / serial_log / tab5_app / tab5_screenshot /
  tab5_input)。**焼く前にコード経路を最後まで読む** (推測修正の連投をしない)。
- PPA バックエンド: T0 基準と pngdiff 差分ゼロ (4 アプリ)。
- CPU バックエンド: 同じアプリ群が正しく描画される (性能は問わない。正しさのみ)。
  capture は SRM を通る前の合成済み 426x240 を取るので、**blend 結果は
  capture 同士の pngdiff で CPU / PPA を直接比較できる** (原理的に一致する
  はず)。一致しない画素が出たら原因を特定して report へ — color-key 解釈の
  差はそのまま wasm に持ち込まれるため、ここで潰す。
- `idf.py size-components` で内蔵 RAM / flash の悪化が誤差範囲であること
  (bin 差分ではなく size-components を使う)。
- 性能: GFX STATS の render_ms を両バックエンドで記録する (CPU が遅いのは
  想定内。数字だけ残す。P4 の撤退線判断の材料になる)。

## 受け入れ条件 (implementation_plan P3)

1. PPA バックエンドで従来と同一挙動 (T0 基準と pngdiff 差分ゼロ、4 アプリ)。
2. CPU バックエンドで同じアプリ群が正しく描画される。
3. size-components で悪化が誤差範囲。
4. TAB5 (PPA/CPU) / S3 / linux の全ビルドが通る。

## report に書くこと

- display_backend_t の確定シグネチャと、移設した関数の対応表 (簡潔に)。
- cursor / ブート画面 / capture / I2C・ヘッドホンをどちら側に置いたかの確定。
- CPU / PPA の capture 比較結果 (差が出た画素と原因。wasm への申し送り)。
- render_ms の実測 (両バックエンド)。
- 音の関数表化でやったこと・やらなかったこと。
- size-components の実測差。

## やらないこと (P3 の範囲外)

- wasm バックエンドの実装 (P4a/P4b)。Canvas / WebAudio には触れない。
- 動画 / EXPORT_FRAME の CPU 実装 (エラー応答まで)。
- 性能最適化 (dirty region 化などは P4 の撤退線で検討する話)。
- 受信ループ・コマンド解釈の作り替え (そのまま残す)。
