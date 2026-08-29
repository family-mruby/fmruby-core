# doc の歩き方

fmruby-core の設計・計画文書の索引と、文書の置き方の規約。
索引部は `rake docs:index` が doc/ の実態から自動生成する
(手で編集してよいのはこの規約の節だけ)。

## 規約

- **doc 直下に置くのはこの README.md だけ**。他のファイルは必ず下のどれかに入れる。
- 文書は 2 種類に分ける:
  - **参照資料** (doc/reference/): 現状を記述する文書。常に最新へ更新する
    (例: reference/task_priority.md、reference/internal_ram_budget.md)。
  - **企画文書** (doc/<テーマ>/): 時系列で進み、いつか終わる文書。
    入口は plan.md か README.md。フェーズごとの作業指示は
    instruction_pN.md、経過と気づきは report/pN.md に書き、
    **計画 (plan) には確定した結果だけを反映する**。
- plan.md の章立ての目安: 目的 / 方針 / スコープ / 受け入れ条件 /
  未確定事項 (実例: wasm/plan.md)。
- 入口ファイルの先頭 (タイトル直後) に状態行を置く:
  `> 状態: 構想|計画済|進行中|完了|凍結 | 更新: YYYY-MM-DD | 一行要約`
  索引はこの行を拾う。無いものは "-" と表示される (追加は任意だが推奨)。
- テーマが完結したら doc/archive/ へ `git mv` する。参照している側の
  パス (doc 内・ソースコメント・memory) も同時に直す。
- 文体は常体。参照した外部資料の名前・ページ番号は書かない。
- 文書を足した/動かしたら `rake docs:index` で索引を更新する。

<!-- INDEX:BEGIN (rake docs:index で生成。手で編集しない) -->

## 参照資料 (doc/reference/)

- [TODO](reference/TODO.md)
- [Tab5 (ESP32-P4) BLE有効化 — Web コンソールの Modern 対応](reference/ble_c6_web_console.md)
- [ブート時間の実測とコストモデル](reference/boot_performance.md)
- [fmruby-core のビルド構造とコンパイル定義のスコープ](reference/core_build_structure.md)
- [GC の観測と調整 (mruby アプリ VM)](reference/gc_monitoring.md)
- [次期基板 HDMI 映像出力方式 検討資料](reference/hdmi_video_output_study.md) — **凍結** (2026-08-29) 次期基板 (NARYAv4) の HDMI 方式比較。LT8912B が最有力、基板が動くまで保留
- [ESP-IDF v6.0 移行メモ（IDF6対応で判明した課題と回避策）](reference/idf6_migration_notes.md)
- [内蔵 RAM 削減計画](reference/internal_ram_budget.md)
- [高速な PicoRuby アプリを書くための知見](reference/picoruby_performance_notes.md)
- [picoruby 上流PR候補メモ (ネットワークAPI検証で発見したバグ)](reference/picoruby_upstream_pr_candidates.md)
- [ESP32-P4 PPA と LovyanGFX の RGB565 描画パイプライン知見](reference/ppa_lgfx_notes.md)
- [Ruby ネットワークAPI 設計書 (Net::HTTP / WebSocket / TLS)](reference/ruby_network_api_design.md)
- [Linux sim がまれに固まる: ログロックの優先度逆転](reference/sim_log_deadlock.md)
- [stdio Design Limitation: Global $stdout/$stdin in Sandbox Execution](reference/stdio_design_limitation.md)
- [ESP32-P4対応指針](reference/support_esp32p4.md)
- [Tab5 内部 I2C バスの制約と設計ルール](reference/tab5_i2c_bus_notes.md)
- [Tab5: SPI 液晶へのミラー映像出力 (計画)](reference/tab5_spi_mirror_plan.md) — **凍結** (2026-08-29) 机上設計済み・実装未着手・被参照なし
- [タスク優先度の全体設計](reference/task_priority.md)

## テーマ別

- `ai/` [OpenAI API 活用の構想メモ](ai/ideas.md) — - 〔1 files〕
- `camera/` [Family mruby カメラ対応 検討メモ](camera/README.md) — **凍結** (2026-08-29) 方式は esp_video 採用で確定、実装未着手 〔1 files〕
- `dev_remote_ctl/` [WiFi 経由の開発用リモート制御(アプリ起動 / kill / 一覧)実装計画](dev_remote_ctl/plan.md) — - 〔1 files〕
- `editor_debug/` [FM-EDITOR オンデバイスデバッガ検討・実装方針](editor_debug/design.md) — - 〔3 files〕
- `editor_ja/` [エディタ日本語対応計画: 子供が使える編集環境](editor_ja/plan.md) — - 〔7 files〕
- `editor_serious_mode/` [エディタ本気モード計画: 全画面・高解像度・高速化](editor_serious_mode/plan.md) — - 〔11 files〕
- `editor_ti/` [エディタ型推論統合 (picoruby-ti) 計画](editor_ti/plan.md) — - 〔17 files〕
- `fmrb_basic/` [FMRuby BASIC 実装プロジェクト 共通指示書](fmrb_basic/00_common.md) — - 〔27 files〕
- `gfx/` [Canvas Viewport スクロール (SET_CANVAS_VIEWPORT) — P4/PPA 活用](gfx/gfx_canvas_viewport_scroll.md) — - 〔2 files〕
- `idf_seam/` [ESP-IDF 依存の継ぎ目整理 (idf_seam)](idf_seam/plan.md) — **完了 (report/s1_s4.md)** (2026-08-29) 共有コードの esp_* 直接依存を fmrb_* の platform 継ぎ目へ吸収し、wasm/stub をカーネル用最小に縮める 〔3 files〕
- `imu/` [P1: six-axis sensor (BMI270) on Modern](imu/report/p1.md) — - 〔1 files〕
- `mic_spectrum/` [計画書: Tab5 マイクの周波数分析デモ + FFT エンジン比較](mic_spectrum/plan.md) — - 〔5 files〕
- `micropython/` [MicroPython ゲスト VM 取り込み計画](micropython/README.md) — - 〔24 files〕
- `midi/` [Family mruby MIDI 対応 検討メモ](midi/README.md) — - 〔23 files〕
- `multivm_app/` [多重 VM アプリ構想: 巨大 Ruby アプリをマイコンで動かす](multivm_app/plan.md) — - 〔3 files〕
- `p4_display_flicker/` [計画書: Tab5 (ESP32-P4) 表示ちらつきの根本修正](p4_display_flicker/plan.md) — - 〔5 files〕
- `p5/` [P5 — Processing/p5.js 互換描画 API](p5/README.md) — - 〔2 files〕
- `picorabbit/` [PicoRabbit (Tab5) の拡張計画](picorabbit/plan.md) — - 〔15 files〕
- `raycast_spinel/` [Raycaster の計算を Spinel gem 化する実装計画](raycast_spinel/plan.md) — - 〔2 files〕
- `remote_debug/` [PicoRuby VM リモートデバッグ検討 (Bluetooth / VSCode)](remote_debug/vm_remote_debug_design.md) — - 〔9 files〕
- `remote_desktop/` [リモートデスクトップ機能 設計書 (ESP32-P4 / Modern)](remote_desktop/design.md) — - 〔1 files〕
- `robo_explorer/` [ロボットエクスプローラー: Pub/Sub で操作する二人羽織パズル](robo_explorer/plan.md) — - 〔3 files〕
- `ruby_asterism/` [プロジェクト名の決定: Asterism](ruby_asterism/naming.md) — - 〔5 files〕
- `spinel_aot/` [Spinel AOT 化プロジェクト 共通指示書](spinel_aot/00_common.md) — - 〔38 files〕
- `ui_widgets/` [汎用 UI 部品 (FmrbUI) の計画](ui_widgets/plan.md) — - 〔21 files〕
- `user_extension/` [ユーザによるシステム拡張の余地 (構想の棚卸し)](user_extension/ideas.md) — - 〔17 files〕
- `wasm/` [wasm (ブラウザ) 対応の検討と計画](wasm/plan.md) — **進行中** (2026-08-29) P1-P4 完了 (P4b/P4c はブラウザで実操作・音ともユーザ確認済)。残るは P5 (配信) 〔14 files〕

## アーカイブ (完結したテーマ)

- `archive/app_kill_fix/` [fmrb_app_kill 到達不能問題の診断と修正](archive/app_kill_fix/README.md)
- `archive/focus_switch/` [Ctrl+Tab フォーカス切替 / フルスクリーン退避 - 実装と検証状況 (P1)](archive/focus_switch/report/p1.md)
- `archive/gfx_unification/` [GFX 送出・組み立ての一本化 (App/Gfx 実装分散の解消)](archive/gfx_unification/README.md)
- `archive/mic/` [Family mruby マイク入力 検討メモ](archive/mic/README.md)
- `archive/tab5_keyboard/` [実装指示書 K1: Tab5 内蔵キーボードの刻印と入力の不一致修正](archive/tab5_keyboard/instruction_k1.md)
- `archive/video/` [SD カードの動画 (MJPEG) を窓の中で再生する — 実装計画](archive/video/plan.md)
- `archive/work_picoruby_merge/` [PicoRuby 最新版統合 作業フォルダ](archive/work_picoruby_merge/README.md)

<!-- INDEX:END -->
