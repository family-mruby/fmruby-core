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
- [DSI (DPI) フレームバッファの置き場所と走査のアンダーラン](reference/dpi_frame_buffer_alignment.md)
- [全画面の高解像度モード (使い方と仕組み)](reference/fullscreen_hires.md) — **完了** (2026-09-28) P4 系 (Tab5 / NARYAv4) とブラウザ版で、属性を持つアプリの全画面だけ 640x360 になる。アプリの書き方、エディタのフォント、遠隔の道具での見え方
- [GC の観測と調整 (mruby アプリ VM)](reference/gc_monitoring.md)
- [描画境界 (FmrbGfx ↔ 描画側) の課題と方針](reference/gfx_boundary_issues.md) — **随時更新** (2026-09-04) 課題 4 件、いずれも未着手。仕様変更が落ち着いてから順に
- [次期基板 HDMI 映像出力方式 検討資料](reference/hdmi_video_output_study.md) — **凍結** (2026-08-29) 次期基板 (NARYAv4) の HDMI 方式比較。LT8912B が最有力、基板が動くまで保留
- [ESP-IDF v6.0 移行メモ（IDF6対応で判明した課題と回避策）](reference/idf6_migration_notes.md)
- [内蔵 RAM 削減計画](reference/internal_ram_budget.md)
- [ランチャーの再走査でデスクトップが落ちる (NARYA v4, 2026-09-02)](reference/launcher_rescan_desktop_stop.md)
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
- `app-distribution/` [アプリ配布プラットフォーム 計画](app-distribution/plan.md) — **完了** (2026-09-26) **P1-P3 完了 (Retro 実機も確認済)**。**1 本の Ruby が sim とブラウザで店になり、一覧にスクリーンショットも出る** (report/p3.md)。picoruby の実バグを 2 つ修正。**NARYA v4 で通し確認 + 速度の作り直し済** (report/p4_device.md, p5_perf.md)。**解析 7.6 秒 → 0.12 秒**。**2026-09-03: 店を default app へ移した** — **起動 10.0 秒 → 0.45 秒**、`large_memory` も不要に (report/builtin_move.md)。Retro 実機も 2026-09-26 に確認済み。 〔14 files〕
- `app_model/` [FmrbApp の基底クラスを締める (計画)](app_model/plan.md) — **完了** (2026-09-26) 継承は変えない。契約 1 つと予約名 15 個を 〔3 files〕
- `app_theme/` [窓枠とアプリ配色をテーマに繋ぐ](app_theme/plan.md) — **完了** (2026-09-02) A・B・C + D 実装済。窓枠は 4 か所あり Python と Lua も繋いだ (report/guest_languages.md)。壁紙はテーマ追従 + パス指定 (report/wallpaper.md) 〔3 files〕
- `camera/` [Family mruby カメラ対応 検討メモ](camera/README.md) — **凍結** (2026-08-29) 方式は esp_video 採用で確定、実装未着手 〔1 files〕
- `dev_remote_ctl/` [WiFi 経由の開発用リモート制御(アプリ起動 / kill / 一覧)実装計画](dev_remote_ctl/plan.md) — - 〔3 files〕
- `direct_boot/` [まっすぐ起動する (ロゴ・BGM を省く / 全画面アプリへ直行)](direct_boot/plan.md) — **完了** (2026-09-03) `boot_splash` と、全画面の `startup_app` 〔1 files〕
- `editor_debug/` [FM-EDITOR オンデバイスデバッガ検討・実装方針](editor_debug/design.md) — - 〔3 files〕
- `editor_ja/` [エディタ日本語対応計画: 子供が使える編集環境](editor_ja/plan.md) — - 〔7 files〕
- `editor_serious_mode/` [エディタ本気モード計画: 全画面・高解像度・高速化](editor_serious_mode/plan.md) — **完了** (2026-09-28) 段階 1-4・6 完了。段階 5 (640x360 全体切替) は、全画面のアプリだけを 640x360 にする doc/fullscreen_hires で置き換えて閉じた 〔11 files〕
- `editor_ti/` [エディタ型推論統合 (picoruby-ti) 計画](editor_ti/plan.md) — - 〔21 files〕
- `fmrb_basic/` [FMRuby BASIC 実装プロジェクト 共通指示書](fmrb_basic/00_common.md) — - 〔27 files〕
- `fullscreen_hires/` [全画面の高解像度モード (P4 系とブラウザ版)](fullscreen_hires/plan.md) — **進行中** (2026-09-28) **H0-H4 完了・ユーザの目視で合格 (NARYAv4 とブラウザ版)**。全画面の高解像度の自動切り替え、入力の追従、エディタのフォント 8/12/16、遠隔デスクトップの追従、表示側の守り。残りは Tab5 の実機確認のみ。切り替えの瞬間の一瞬の乱れは保留 (report/h1.md 6 章) 〔11 files〕
- `gfx/` [Canvas Viewport スクロール (SET_CANVAS_VIEWPORT) — P4/PPA 活用](gfx/gfx_canvas_viewport_scroll.md) — - 〔2 files〕
- `imu/` [P1: six-axis sensor (BMI270) on Modern](imu/report/p1.md) — - 〔1 files〕
- `iram_reduction/` [内蔵 RAM の削減 (第 2 弾)](iram_reduction/plan.md) — **進行中** (2026-09-28) **R1 完了**: 静的な内蔵 RAM を S3 -16,384 / Tab5 -39,592 / NARYAv4 -39,584 バイト、P4-Nano の待機時の空き +39,564。表示タスクの表・mruby の組み込みライブラリの表 (const 化)・tmpfs ほかを PSRAM / flash へ。次は R2 (確かめてから移すもの) 〔3 files〕
- `mic_spectrum/` [計画書: Tab5 マイクの周波数分析デモ + FFT エンジン比較](mic_spectrum/plan.md) — - 〔5 files〕
- `micropython/` [MicroPython ゲスト VM 取り込み計画](micropython/README.md) — - 〔24 files〕
- `midi/` [Family mruby MIDI 対応 検討メモ](midi/README.md) — - 〔23 files〕
- `mouse_wheel/` [マウスホイール対応 検討と計画](mouse_wheel/plan.md) — **進行中** (2026-09-01) **W1 完了** (ブラウザ・sim で実測)、**W2 完了・NARYA v4 実機で確認済** (report/w2.md)。残りは W3 (実機 USB マウス・白名簿) / W4 (nsf/smf と修飾キー) 〔4 files〕
- `multivm_app/` [多重 VM アプリ構想: 巨大 Ruby アプリをマイコンで動かす](multivm_app/plan.md) — - 〔3 files〕
- `naryav4/` [NARYA v4 (ESP32-P4 + HDMI 出力) 対応計画](naryav4/plan.md) — **進行中** (2026-09-27) P0-P4 完了。**P6 完了: 青ちらつき (DSI アンダーラン) は DSI フレームバッファの先頭を 4KB 境界に揃えて解消** (report/p6.md、ユーザ目視で確認)。帯域ではなく揃いが原因だった。残り = ユーザ確認とモニタ相性、無印 ESP32 疎通 (保留) 〔11 files〕
- `p4_display_flicker/` [計画書: Tab5 (ESP32-P4) 表示ちらつきの根本修正](p4_display_flicker/plan.md) — - 〔6 files〕
- `p5/` [P5 — Processing/p5.js 互換描画 API](p5/README.md) — - 〔2 files〕
- `picorabbit/` [PicoRabbit (Tab5) の拡張計画](picorabbit/plan.md) — **完了** (2026-09-26) P0-P4・P6-P9 完了 (P8 = 動画 .mjpg、Tab5/wasm/sim 検収済。P9 = 背景 PNG + 行サイズ、Tab5 実機も確認済)。P5 (見せ場) は任意として残す 〔19 files〕
- `raycast_spinel/` [Raycaster の計算を Spinel gem 化する実装計画](raycast_spinel/plan.md) — - 〔2 files〕
- `remote_debug/` [PicoRuby VM リモートデバッグ検討 (Bluetooth / VSCode)](remote_debug/vm_remote_debug_design.md) — - 〔9 files〕
- `remote_desktop/` [リモートデスクトップ機能 設計書 (ESP32-P4 / Modern)](remote_desktop/design.md) — - 〔1 files〕
- `robo_explorer/` [ロボットエクスプローラー: Pub/Sub で操作する二人羽織パズル](robo_explorer/plan.md) — - 〔3 files〕
- `ruby_asterism/` [プロジェクト名の決定: Asterism](ruby_asterism/naming.md) — - 〔7 files〕
- `softap_remote/` [WiFi AP モードと携帯端末からの遠隔画面 (SoftAP + 認証 + iPhone ビューア)](softap_remote/plan.md) — **計画済** (2026-08-31) 機体が自分で WiFi を張り、PC も家の WiFi も無い場所で iPhone のブラウザから遠隔画面を使えるようにする。設定だけで切替、共通鍵で守る 〔1 files〕
- `spinel_aot/` [Spinel AOT 化プロジェクト 共通指示書](spinel_aot/00_common.md) — - 〔38 files〕
- `spinel_upstream_ext/` [Spinel 上流の ext 機構でフォークを置き換えられるか](spinel_upstream_ext/plan.md) — **進行中** (2026-09-27) **P0-P3 完了、develop に入った**。フォーク固定点は `fmrb-ext` (`4faa22b4`、kishima/spinel)。gem と VM は上流の ext 機構で生成し FFI の迂回を撤去、全生成 `--no-inline-hot`。内蔵 RAM は取り込み前より約 6.8KB 少ない。速度の退行 (P2b-2) は許容、P3 で一部回復。今後の上流追従は様子見、単純なバグの PR は続ける 〔22 files〕
- `stamp_p4/` [Stamp-P4 ヘッドレス機 (切符サイズの Modern)](stamp_p4/README.md) — **構想** (2026-08-31) M5Stamp-P4 + Stamp-AddOn C6 を殻に入れたヘッドレス Family mruby。まず殻 (case_design.md)、ファーム分岐は後続 〔2 files〕
- `ui_widgets/` [汎用 UI 部品 (FmrbUI) の計画](ui_widgets/plan.md) — - 〔22 files〕
- `user_extension/` [ユーザによるシステム拡張の余地 (構想の棚卸し)](user_extension/ideas.md) — - 〔17 files〕
- `wasm/` [wasm (ブラウザ) 対応の検討と計画](wasm/plan.md) — **完了** (2026-09-26) P1-P5 完了。**自前の nginx で公開中** (置き先を選んだ経緯と nginx 設定は report/hosting.md)。音の可聴確認とスマホでの実操作も確認済み 〔27 files〕

## アーカイブ (完結したテーマ)

- `archive/app_kill_fix/` [fmrb_app_kill 到達不能問題の診断と修正](archive/app_kill_fix/README.md)
- `archive/focus_switch/` [Ctrl+Tab フォーカス切替 / フルスクリーン退避 - 実装と検証状況 (P1)](archive/focus_switch/report/p1.md)
- `archive/gfx_unification/` [GFX 送出・組み立ての一本化 (App/Gfx 実装分散の解消)](archive/gfx_unification/README.md)
- `archive/idf_seam/` [ESP-IDF 依存の継ぎ目整理 (idf_seam)](archive/idf_seam/plan.md)
- `archive/mic/` [Family mruby マイク入力 検討メモ](archive/mic/README.md)
- `archive/tab5_keyboard/` [実装指示書 K1: Tab5 内蔵キーボードの刻印と入力の不一致修正](archive/tab5_keyboard/instruction_k1.md)
- `archive/video/` [SD カードの動画 (MJPEG) を窓の中で再生する — 実装計画](archive/video/plan.md)
- `archive/work_picoruby_merge/` [PicoRuby 最新版統合 作業フォルダ](archive/work_picoruby_merge/README.md)

<!-- INDEX:END -->
