# Phase 0 作業進捗メモ (再開用)

このファイルは Phase 0 (PoC / Go-NoGo) の作業を中断・再開しやすくするための
作業ログ。最終成果は `phase0_report.md` にまとめる。

## 環境・前提 (確認済み事実)

- Spinel フォーク: `tmp/spinel`、ブランチ `fmrb-dev` (作業用)。
  - `make deps && make` 成功。`bin/spinel` 生成済み。
  - 動作確認: `./bin/spinel -E -e 'puts 42; puts "hello".upcase'` -> `42` / `HELLO` OK。
  - 実行方法: `-E` でコンパイル+即実行。`-c` で C 生成のみ。デフォルトはバイナリ生成。
- PoC 成果物ディレクトリ: `fmruby-core/tool/spinel_poc/` (+ `coverage/`)。
- レポート: `fmruby-core/doc/spinel_aot/reports/`。

## 確認済みの実コード事実

- 対象ファイル (無改変で取り込む):
  - `main/prebuild_scripts/kernel/fmrb_kernel/input_router.rb` (`InputRouterMixin`, 345行)
  - `main/prebuild_scripts/kernel/fmrb_kernel/window_manager.rb` (`WindowManagerMixin`, 90行、`$menu_bar_height=13`)
- 定数値 (実コードより):
  - `FmrbConst::MSG_TYPE_APP_CONTROL = 0`
  - `FmrbConst::MSG_TYPE_HID_EVENT  = 3`
  - (fmrb_msg.h の enum: APP_CONTROL=0, APP_GFX=1, APP_AUDIO=2, HID_EVENT=3, FILE_TRANSFER=4)
  - `$menu_bar_height = 13` (window_manager.rb トップレベルで設定)
  - `MIN_WINDOW_WIDTH=64`, `MIN_WINDOW_HEIGHT=64`, `RESIZE_PREVIEW_MIN_INTERVAL_MS=100` (InputRouterMixin 内定数)
- HID イベントバイナリ形式 (input_router.rb decode 部):
  - 6 byte: subtype(1) + button(1) + x_lo(1) + x_hi(1) + y_lo(1) + y_hi(1)、x/y は little-endian
  - subtype: 3=move, 4=button down, 5=button up
- InputRouterMixin が参照する外部 API (ハーネスでスタブ):
  - C: `_get_window_list`, `_set_hid_target`, `_bring_to_front`, `_send_raw_message`,
    `_try_send_raw_message`, `_update_window_position`, `_update_window_size`, `_get_app_info`
  - 他 mixin: `find_window_by_pid`/`find_window_at`/`mark_window_list_dirty`/`update_window_list`
    (window_manager.rb で取り込み), `app_suspended?`/`build_hid_close_overlay`
  - グローバル: `Machine.board_millis`, `Log.info/warn/error`, `MessagePack.pack`
  - ivar: @window_list, @window_list_dirty, @desktop_pid, @fullscreen_pid, @hid_target_pid,
    @capture_mode, @capture_pid, @mouse_down_pid, @resize_* / @drag_* / @resize_preview_*,
    @suspended_pids, @desktop_overlay_active, @desktop_overlay_rect

## タスク進捗

- [x] T0-1: Spinel ビルド + 動作確認完了。`make test` 基準 = **1,991 pass / 1 fail**
      (fork でテスト増、基準指示の 1,744 から乖離)。唯一の fail は
      `nilclass_bool_ops_conversions` (Complex 虚部表示 cosmetic、本件無関係・既存)。
- [x] T0-2: input_router + window_manager 検証ハーネス **CRuby==Spinel byte 一致 (2089行)** 達成。
      - ハーネス: `tool/spinel_poc/harness_input_router.rb` (実コード無改変で取り込み)
      - trace モード: 700 イベント、byte 単位一致。bench モードは scale=480 (33.6万イベント)。
      - **達成に Spinel フォークへ 3 修正が必要だった** (詳細 phase0_findings.md):
        - FIX-1 `String#setbyte` poly 引数受理 (codegen_call_recv.c、1行)
        - FIX-2 include された mixin メソッドの `rescue => e` poly 化 (analyze.c、関数化+再実行)
        - FIX-3 `sp_poly_length` の strlen 停止 (sp_runtime.h、1行)
      - 3 修正後の make test: **1,991 pass / 1 fail (回帰ゼロ)**。
      - 最小再現: `tool/spinel_poc/repro/`。
      - **残**: bench モード (33.6万件) の実行完走確認 (T0-3 と統合)。
- [x] T0-3: 性能計測完了。フル harness (336k件): Spinel 0.52s ≈ CRuby 0.51s (allocation律速)。
      compute micro (2M反復): Spinel 0.06s vs CRuby 0.21s = **3.5x**。mruby はホスト実行系
      未ビルドのため Phase 2 実機に委譲。詳細 phase0_findings.md。Go 基準 (CRuby比1x以上) 充足。
- [x] T0-5: pure-Ruby MessagePack サブセット完了。`msgpack_pure.rb` + `test_msgpack.rb`
      = **CRuby / Spinel とも 64 pass, 0 fail**。固定ベクタ + roundtrip + float64。
      Array#pack 不使用 (バイト配列 + setbyte)。msgpack gem 無しのため相互検証は固定ベクタで代替。
      実装中に **FIX-4** (`String#* が NUL 切り捨て`, commit a8c3c201) 発見・修正。回帰ゼロ。
- [x] T0-4: coverage 7本完了。**全7本 CRuby==Spinel byte 一致**。
      4本無改変一致、3本は軽微な回避で一致 (`coverage/UNSUPPORTED.md` に U-1/U-2/U-3 記録)。
      回避不能な未対応構文ゼロ = Go 基準充足。ランナー `run_all.sh` で 9/9 パス。
      未対応 (回避策あり): U-1 poly.ljust 未dispatch、U-2 同名 rescue 複数 arm 特殊化不可(意図的)、
      U-3 nested-array poly が ctor/mixin算術で miscompile。U-1/U-3 は Phase 1 hardening 候補。
- [~] T0-6: 32bit (-m32) ビルド検証 **ブロック中** (gcc-multilib 未導入、i386 コンテナ網不可)。
      harness.c 生成済み (/tmp/harness32/)。`sudo apt-get install -y gcc-multilib` 導入後に
      runtime+harness を -m32 ビルド→出力比較。mrb_int=intptr_t のため 32bit 化リスクは
      小整数中心で低いが要実測。
- [x] T0-7: shell IRB/Sandbox 境界評価完了。Sandbox#compile/execute = 実行時 eval で
      Spinel AOT 不可。**shell は mruby のまま残す推奨** (境界表は phase0_report.md)。
- [x] Go/NoGo 判定 -> phase0_report.md 作成済み。**判定: Go (条件付き)**。
      T0-6 のみ環境要因で未完、Go を覆す要素なし。

## Spinel フォーク改修状態 (fmrb-dev, コミット済み)

ユーザ許可のもと PR しやすい単位で個別コミット (2026-07-23):
- `318f4a7b` codegen: String#setbyte accepts a poly byte value (FIX-1)
- `56394f2d` runtime: String#size/length on a poly is byte-exact, not strlen (FIX-3)
- `7b820768` analyze: type `rescue => e` in an included-module method as Exception (FIX-2)

いずれも汎用バグ修正で upstream PR 可能。push は未実施 (ユーザ判断)。

## 次にやること

T0-5 (pure-Ruby MessagePack サブセット) と T0-4 (desktop/shell coverage 8本) が残り。
T0-6 (32bit -m32)、T0-7 (shell 境界評価) も。その後 Go/NoGo を phase0_report.md に。
mruby ホスト実行系: `components/picoruby-esp32/picoruby/bin/` には picorbc/mrbc のみ
(実行系なし)。`rake host:build` で作れるか要確認 (T0-5 のデュアルビルド確認で必要になれば)。
