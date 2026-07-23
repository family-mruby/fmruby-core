# Spinel AOT 化プロジェクト 共通指示書

この doc/spinel_aot/ 以下は、Spinel (Ruby AOT コンパイラ) を使って
fmruby-core の PreBuild Ruby (カーネル / system_desktop / shell) を
C 化するプロジェクトの、実装担当 AI 向け指示書である。
全体の背景・実現性検討は `doc/spinel_aot_feasibility_plan.md` を必ず先に読むこと。

## プロジェクト概要 (最小限の背景)

- Family mruby は ESP32-S3 (+ Linux シミュレーション) 上で複数の
  mruby(PicoRuby) VM を FreeRTOS タスクとして動かす OS。
  OS 機能 (Window マネージャ、イベント処理、デスクトップ、シェル) は
  Ruby で書かれ、ビルド時に picorbc でバイトコード化して焼いている。
- mruby VM のインタプリタ実行と GC 停止が性能ボトルネック。
- Spinel (`tmp/spinel` = フォーク kishima/spinel の作業チェックアウト) は
  Ruby を型推論付きで単一 C ファイルへ AOT コンパイルする。これで
  カーネル → desktop / shell の順に PreBuild Ruby を C 化する。
- 方針決定事項:
  - Spinel は**フォークして改修**する (ライブラリモード、マルチ
    インスタンス化、ESP32 対応)。汎用変更は upstream に PR できる
    コミット粒度を保つ。
  - Spinel 化の対象: **カーネル VM と system_desktop**。
    **shell は対象外に確定 (2026-07-23 ユーザ決定)** — IRB / .toml なし
    スクリプト実行が Sandbox (実行時評価 = eval 相当) に依存し AOT で
    原理的に提供できないため、mruby のまま残す。
    editor 等その他の default_app と ユーザアプリも対象外 (mruby のまま)。
  - **OS 側 PreBuild Ruby (Spinel 対象コード) はメタプログラミング禁止を
    正式なコーディング規約とする**: eval / instance_eval / class_eval、
    動的名の send、define_method、method_missing、ObjectSpace、
    動的リフレクション (instance_variable_get 等)、Fiber、Thread を
    使わない。リテラル名の respond_to? は可。性能を最優先する。
    既存コードはこの規約を満たしていることを調査済み。新規追加コードも
    従うこと。
  - VM 間の通信はこれまで通り「バイナリメッセージ (FreeRTOS キュー)」。
    Spinel 化した VM と mruby VM は独立ランタイムとして共存する。
  - mruby 版は捨てない。**同一 Ruby ソースを picorbc と Spinel の両方で
    ビルドできる状態 (デュアルビルド) を常に維持**する。

## リポジトリと主要パス

- fmruby-core: `/home/kishima/fmrb/family-mruby/fmruby-core` (このリポジトリ)
  - PreBuild Ruby: `main/prebuild_scripts/kernel/` (カーネル ~1,160 行、
    system_desktop.app.rb + system_desktop/ mixin ~4,200 行)、
    `main/prebuild_scripts/default_app/` (shell / editor 等)
  - バイトコード化: `main/prebuild_scripts/compile_ruby_to_bytecode.cmake`
    (combined .rb 生成は `tool/debug/gen_combined_rb.py`)
  - VM 起動・ロード: `main/app/fmrb_app.c`, `main/app/fmrb_app_spawner.c`,
    `main/kernel/fmrb_kernel.c`
  - mruby 向け C バインディング: `lib/add/picoruby-fmrb-*` 以下
    (ビルド時に picoruby サブモジュールへコピーされる。
    例: `lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c` の `_spin`)
  - タスク/PID 構成: `components/fmrb_common/include/fmrb_task_config.h`
- Spinel フォーク: `/home/kishima/fmrb/family-mruby/tmp/spinel`
  (origin = kishima/spinel、フォーク元 = matz/spinel)。**改修可能**。
  改修は作業ブランチ `fmrb-dev` (master から作成、無ければ最初に作る) に
  コミットする。master は upstream 追従用に保ち、直接コミットしない。
  コミットは英語メッセージで、汎用変更と fmruby 固有変更を分ける
  (詳細規約は phase1.md T1-1)。

## 実装 AI が厳守するルール

fmruby-core/CLAUDE.md と family-mruby/CLAUDE.md のルールが最優先。特に:

1. **git 操作 (commit/push/submodule 追加) を勝手に行わない**。
   コミットが必要な区切りではユーザに依頼する。
2. `.gitsubmodule` 配下 (components/picoruby-esp32/picoruby 等) を直接編集
   しない。変更は `lib/add` / `lib/patch` / `lib/replace` + Rakefile 経由。
3. `sdkconfig` / `sdkconfig.defaults` は編集禁止。変更が要る場合は提案のみ。
4. `main/` 以下の C の戻り値は `fmrb_err.h` 標準、malloc は使わず
   `fmrb_mem.h` (mruby 実行タスク系: fmrb_malloc / OS 系: fmrb_sys_malloc)。
   GPIO は `fmrb_pin_assign.h` 参照。
5. ソースコードのコメントは英語。ドキュメント・ユーザ向け報告は日本語。
   絵文字・チェックマーク等の非 ASCII 記号は使わない。
6. 問題回避のためにソースをビルド対象から外すことは禁止。
   Legacy コードは残さず消す。ファイルを勝手に削除しない。
7. `lib/` 以下を編集したら `rake clean` してからビルド。
   linux/ESP32 のターゲット切替時は `rake clean_all`。
8. **フォーク (Spinel) 側の改修では**: upstream の `make test` (Phase 0 実測基準: 1,991 pass / 1 既存 cosmetic fail) と
   `make bench` を毎回のゲートにする。upstream PR 可能な汎用コミットと
   fmruby 固有コミットを分ける。fork のコード規約 (C、既存スタイル) に従う。

## ビルド・検証コマンド

- fmruby-core ビルド: `rake build:linux` / `rake build:esp32` / `rake -T`
- Linux シミュレーション自律検証 (リポジトリルート family-mruby で実行):
  - 起動+スクリーンショット: `tools/dev_run_check.sh [--keep] [出力.png]`
    (core のブートマーカー `main_loop started` を待って PNG 化)
  - 画面キャプチャのみ: `python3 tools/fmrb_screenshot.py [--wait 秒] 出力.png`
  - 入力注入: `python3 tools/fmrb_input.py click X Y / key NAME / text "S" /
    sleep MS ...` (座標は 320x240 フレームバッファ座標)
  - 検証後は `docker compose down`
  - 事前に fmruby-core と fmruby-graphics-audio 両方の `rake build:linux` が
    必要。音声・実機はユーザ確認事項。
- Spinel: `make deps && make && make test && make bench` (フォーク内)

## フェーズ構成とゲート

| Phase | 内容 | 指示書 | 前提 |
|---|---|---|---|
| 0 | PoC・言語カバレッジ検証 (Go/NoGo) | phase0.md | なし |
| 1 | フォーク整備 + ライブラリモード + 組み込み基盤 | phase1.md | Phase 0 Go |
| 2 | カーネル VM の Spinel 化 (Linux) | phase2.md | Phase 1 |
| 3 | ランタイムマルチインスタンス化 (フォーク) | phase3.md | Phase 1 (2 と並行可) |
| 4 | system_desktop の Spinel 化 (+ オプション shell) (Linux) | phase4.md | Phase 2, 3 |
| 5 | ESP32-S3 ポート | phase5.md | Phase 2 (kernel のみなら)、4 (全体) |

- 各 Phase の完了条件は各指示書の「受け入れ基準」をすべて満たすこと。
- 完了時に `doc/spinel_aot/reports/phaseN_report.md` (日本語) を書き、
  ユーザのレビューを受けてから次の Phase に進む。
- 指示書内のファイルパス・行番号は 2026-07-18 時点の調査に基づく。
  行番号はドリフトしている可能性があるため、**必ず実物を確認してから**
  編集すること。事実が指示書と食い違う場合は、指示書に従うのではなく
  実物を正とし、レポートで差異を報告する。

## 用語

- PreBuild コード: ビルド時にバイトコード化して firmware に埋め込む Ruby。
- カーネル VM: `FmrbKernelImpl` を実行する VM (PID 0)。Window 管理と
  イベントルーティングを担う。desktop (PID 2) や shell は別 VM。
- fmrb_spx: 本プロジェクトで新設する「Spinel FFI 用の素の C API シム層」。
- デュアルビルド: 同一 Ruby ソースを mruby (picorbc) と Spinel の両方で
  ビルドできる状態。切替フラグは `FMRB_KERNEL_ENGINE` (Phase 1 で導入)。
- 制御反転 (poll 化): 従来は C の `_spin` が Ruby の `msg_handler` を
  mrb_funcall で呼ぶ (C→Ruby)。Spinel は C→Ruby コールバック不可のため、
  Ruby 側ループが FFI でメッセージを取り出して Ruby 内でディスパッチする
  構造に変える。
