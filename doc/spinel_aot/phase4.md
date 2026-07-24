# Phase 4 指示書: system_desktop の Spinel 化 (+ オプション: shell) (Linux)

前提: Phase 2 (カーネル Spinel 化、性能実証済み) と Phase 3 + 3.5
(マルチインスタンス化 + アロケーション完全フック化、
reports/phase3_report.md) の完了。fork は fmrb-dev `7063b350` 以降。
Phase 2 のレポートにある性能データを読み、
desktop へ投資する価値の再確認をしてから着手する
(描画転送コストが支配的で Ruby 高速化の効果が薄いと判明している場合は、
ユーザに方針確認を仰ぐこと)。

**着工順の注意**: T4-0 (fork 側のマルチプログラムリンク対応) が
終わるまで T4-3 (desktop の生成 C を firmware にリンクする工程) は
着手不能。T4-0 と T4-1/T4-2 (fmruby-core 側のシム/ベース層) は並行可。

## 目的

- system_desktop (PID 2) を Spinel 化し、カーネルと合わせて
  複数 Spinel インスタンスの同居を Linux 上で実証する。
- **shell はオプション** (ユーザ決定: IRB / Sandbox 連携のハイブリッド
  構成が高コストなら対象から外し、mruby のまま残す)。本 Phase の必須
  範囲は desktop まで。shell (T4-4) は着手前に Phase 0 の境界評価と
  Phase 2/T4-3 の結果を添えてユーザに実施可否を確認し、承認された
  場合のみ行う。shell を外す場合、shell は mruby アプリとして従来どおり
  動く (共存確認は T4-5 に含む)。

## 事前作業

1. fork の Phase 3/3.5 成果を fmruby-core へ取り込む (3 点セット):
   - fork fmrb-dev が push 済みであることを確認し、
     `components/fmrb_spinel_rt/SPINEL_PIN` の commit を更新
     (T4-0 完了後はその commit)
   - `rake spinel:setup` で vendor/spinel を pin に追従
   - `import_from_fork.rb` を再実行 (sp_ctx.h/.c、sp_mem_override.h が
     snapshot に入る)。IMPORT_INFO と pin の commit 一致を確認
2. MC ビルド配線: `components/fmrb_spinel_rt` と生成 C
   (compile_ruby_to_spinel.cmake) の**両方**に `-DSP_MULTI_CTX` と
   `-include sp_mem_override.h` を付ける。**片側だけだと silent ABI
   break / メモリ隔離の静かな破れになる** (SP_GC_STACK_MAX と同種の
   両側一致必須項目。CMake 内で 1 変数から両方へ流す)。fork の
   `check-mc-syms` 相当の nm チェックを firmware ビルドにも移植し、
   Spinel 由来オブジェクト (spinel_rt + 生成 C) に libc malloc 系の
   未定義参照が無いことを検証する。
3. Phase 2 のカーネルを SP_MULTI_CTX 構成で再ビルドし、
   タスク起動側 (`FMRB_LOAD_MODE_NATIVE` 経路) に
   `sp_instance_create` + `sp_ctx_set_current` を実装
   (Phase 3 で決めた契約どおり。config の閾値はまず default)。
4. **Spinel タスクのメモリを estalloc に配線する (ユーザ決定 2026-07-23)**:
   - vm_type は FMRB_VM_TYPE_NATIVE のまま新設しない。spawn 時にタスク所定
     の mempool (`fmrb_get_mempool_ptr(ctx->mempool_id)`) へ `est_init` し、
     得た `ESTALLOC*` を **`ctx->est` に格納** (mruby タスクと同じ持ち方)
     した上で、sp_instance_config の mem_ud / alloc / realloc / dealloc
     フックに est_calloc / est_realloc / est_free を渡す。
   - `fmrb_app_ps` の NATIVE ケースを「`ctx->est` があれば
     `mrb_get_estalloc_stats` で統計を返す」に変更 (mruby ケースと同一
     経路)。これで Monitor のカーネル行 (Phase 2 で消えた Heap バー) が
     復活し、mruby / Spinel でヒープ表示が同じ意味を持つ。
   - gc_threshold / str_threshold は「しきい値 < プールサイズ」の契約
     (Phase 3) に従い、タスクの mempool サイズから設定する。
   - estalloc は今後の全エンジンの標準アロケータとする (優秀なため)。
   - タスク終了時は sp_instance_destroy 後に `est_cleanup` し、
     ctx->est を NULL に戻す (mruby の cleanup と同じ位置)。
5. 回帰: Phase 2 の検証シナリオ (dev_run_check + 入力注入) を再実行し、
   カーネル単独が multi-ctx 構成でも同一挙動であることを確認。
   このタイミングでヒープ使用量の mruby / Spinel 比較 (Phase 2 で延期した
   項目) を Monitor / ps の統計で取得し、レポートに載せる。

## タスク

### T4-0: fork — マルチプログラム同一バイナリリンク対応 (2-3 日)

**背景 (実測済みの blocker)**: 生成 TU は entry 以外に**約 27 個の
グローバルシンボル**を定義し (`sp_exc_*` 例外機構 13 個、`sp_raise_cls`、
`sp_sprintf`、`sp_proc_call`、`sp_box_proc`、`sp_signal_*`、
`sp_fiber_reraise`、`sp_bigint_raise_zerodiv`、BSS の `sp_trap_*` /
`_sp_proc_poly_args/ret`)、**ランタイム .a がこれらを参照する**
(sp_raise_cls は 13 オブジェクト、sp_sprintf は 9 から)。このため
kernel + desktop の生成 C を 1 つの firmware ELF にリンクすると同名
多重定義で失敗する。phase3_report の「プログラムごとに別バイナリ」は
fork のテストハーネス (別プロセス実行) では制約にならなかったが、
fmruby では kernel/desktop が同一 ELF のため解決必須。

**方式 (ユーザ決定 2026-07-23: fork 側修正が本筋)**: Phase 3 で
introspection vtable 12 本に使ったのと同じ per-ctx 間接化を適用する。

1. 棚卸し: 生成 TU が emit する非 static シンボルを機械列挙し
   (`nm --defined-only` で T/B/D/R)、「runtime から参照される /
   TU 内部のみ」に分類する (上記 27 個が起点。コンパイラの版で
   増減しうるので機械的に確定する)。
2. runtime → TU 方向の参照を sp_ctx の関数ポインタ経由に変更し、
   `sp_tu_ctx_init()` で登録する (vtable と同じ流儀)。TU 側の定義は
   library/MC モードでは static 化する (codegen 修正)。default
   ビルドは従来どおり直接シンボル解決で byte 同一を維持する。
3. fork にリンクテストを追加: 2 つの生成プログラムを 1 バイナリに
   リンクし、各インスタンスで両 entry を実行して正答することを
   test/multi_ctx に追加 (`make test-multi-ctx` に配線)。
4. nm ゲート拡張: mc の生成 TU に「entry 以外のグローバル定義が
   無い」チェックを追加 (シンボル漏れの再発防止)。
5. **フォールバック** (本筋が難航した場合のみ): objcopy
   `--prefix-symbols` でランタイムごとプログラム単位に複製する方式。
   fork 無改修で済むが、ESP32 ではランタイムコードがプログラム数分
   フラッシュに乗るため恒久解にはしない。採った場合はレポートに
   サイズ実測を残し、本筋への移行条件を書く。

完了後: fmrb-dev へ push し、事前作業 1 の 3 点セット (pin 更新 →
setup → import) をやり直してから T4-3 へ進む。

### T4-1: FmrbApp / FmrbGfx の FFI シム (2-3 日)

1. 調査: `lib/add/picoruby-fmrb-app/ports/esp32/app.c` (FmrbApp ~24
   メソッド) と `gfx.c` (FmrbGfx ~47 メソッド) の全メソッドと、
   その下位 C 関数を一覧化する (レポートに表として残す)。
2. `main/app/fmrb_spx_app.c` / `fmrb_spx_gfx.c` + ヘッダを新設し、
   Phase 1 の fmrb_spx 設計原則 (素の C ABI、構造化データは
   固定レイアウトバイト列、fmrb_err 標準) で全メソッドをラップする。
   - 描画系 (clear / fill_rect / draw_text / draw_line / sprite /
     image / present / viewport 等) は int 引数中心なのでほぼ機械的。
   - `_create_canvas` 系: canvas ハンドル (int id) を返す設計にする。
   - 文字列引数 (draw_text 等) は `:str` + 長さで渡す。
   - `_transfer_file` / config 読み書き / `heap_info` / `ps` /
     `usb_devices` / `wallclock` 等の構造化戻り値はバイト列
     スナップショット + Ruby 側パースで統一。
   - `hid_raw_subscribe` のようなイベント購読系は「購読フラグを立てて
     メッセージとして受ける」現行機構をそのまま使えるはず。
     コールバック前提の API が見つかったら poll 形へ変換する
     (C→Ruby 呼び出しは不可)。
3. mruby バインディング側も可能な範囲で fmrb_spx_* を呼ぶ形に
   一本化 (Phase 1 の kernel と同じ方針。lib/add を触ったら rake clean)。

### T4-2: Spinel 用 FmrbApp / FmrbGfx ベースクラス (1-2 日)

1. `main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb`:
   `class FmrbApp` / `class FmrbGfx` を FFI 実装で再現。
   `system_desktop.app.rb` が使うメソッドを網羅 (実コードを grep して
   確定。T4-4 を実施する場合は shell 使用分も追加)。
2. `set_timer(&blk)` / タイマ系: C 側にブロックを渡せないため、
   Ruby 側実装にする:
   ```ruby
   def set_timer(ms, &blk)
     @_timers << [FmrbSpx.board_millis + ms, blk]
   end
   def _run_timers
     now = FmrbSpx.board_millis
     due, @_timers = @_timers.partition { |t| t[0] <= now }
     due.each { |t| t[1].call }
   end
   ```
   `_run_timers` はアプリの poll ループ (on_update 相当の駆動部) から
   呼ぶ。mruby 側の既存 set_timer の意味論 (repeat の有無、精度) を
   実物で確認して合わせる。
3. アプリの spin ループも kernel と同様に poll 化する。mruby 側
   FmrbApp の `_spin` 相当の挙動 (メッセージ処理 → on_update →
   present の順序、フレームレート制御) を読み、同一順序で再現する。

### T4-3: system_desktop の Spinel ビルド (2-3 日)

1. combined 生成: Phase 2 の cmake 機構を一般化し、
   `system_desktop_combined_spinel.rb` を生成
   (ffi + const + msgpack + app ベース層 + system_desktop/ mixin 13 本
   + system_desktop.app.rb + 起動コード)。
   `--entry system_desktop_entry` でコンパイル。
2. Ruby ソース調整: Phase 0 の UNSUPPORTED 対応リストに従い、
   mruby 互換を保つ書き換えを行う。既知の注意点:
   - `about_dialog.rb` の `FmrbApp.respond_to?(:usb_devices)` は
     リテラル名なので Spinel で解決可能だが、ベース層に該当メソッドを
     必ず定義しておく (未定義だと推論が false に固定される。それが
     意図と合うか確認)。
   - i18n.rb の日本語文字列: UTF-8 のまま扱えるか Phase 0 で確認済みの
     はず。問題があれば表引きをバイト列比較に変える。
   - `sort {|a,b| ...}` や `$menu_bar_height` は対応済み機能。
3. spawn 統合: `main/app/fmrb_app_spawner.c` の builtin_app_table に
   NATIVE エントリを追加できるよう拡張し、`system/desktop` を
   エンジンフラグで bytecode / native 切替。アプリごとに切替できる
   よう `FMRB_KERNEL_ENGINE` とは別に
   `FMRB_APP_ENGINE_DESKTOP` / `..._SHELL` (または table 内フラグ) を
   用意する (段階投入・切り分けのため)。
4. 2 インスタンス同居確認: カーネル (Spinel) + desktop (Spinel) で
   起動し、dev_run_check + 基本操作。さらに
   カーネル (mruby) + desktop (Spinel) の混成も起動確認する
   (エンジン独立性の証明)。

### T4-4 (オプション): shell の Spinel 化 (ハイブリッド) (2-3 日)

**着手条件**: ユーザの明示承認。承認がなければこのタスクは実施せず、
shell は mruby のまま残す (その場合 T4-1/T4-2 で shell 用に作る必要が
あった分のシムも省略してよい。desktop が使う範囲だけ実装する)。

1. Sandbox 委譲 API: Phase 0 の境界表に従い、C シムを実装する。
   ```c
   /* Execute Ruby source in an mruby sandbox. Returns exec id. */
   int fmrb_spx_sandbox_start(const char *src, int len);
   /* Poll sandbox: fills output chunk, returns state
      (running / done(exit value as str) / error). */
   int fmrb_spx_sandbox_poll(int id, uint8_t *buf, int cap, int *state);
   int fmrb_spx_sandbox_send_input(int id, const uint8_t *b, int len);
   int fmrb_spx_sandbox_kill(int id);
   ```
   実装は既存の Sandbox / FMRB_LOAD_MODE_FILE 系の機構
   (`main/app/fmrb_app.c` の on-device コンパイル経路) を再利用する。
   mruby VM をどのタスクで動かすか (専用 worker タスクを 1 本用意する
   ことを推奨。shell の Spinel タスク内で mruby VM を回すことは
   スタック/ヒープ設計が別問題になるため避ける) を設計してから
   実装する。設計はレポートに記載。
2. shell Ruby の再構成:
   - Spinel 化: `shell.app.rb` 本体 + `shell_commands.rb` +
     `shell_scroll.rb` + `shell_io.rb` ($stdout 実装含む)。
   - `shell_irb.rb`: 入力 1 行を fmrb_spx_sandbox_* へ委譲し、
     出力 poll を表示ループに組み込む形へ書き換え。
   - .toml なしスクリプト実行 (in-process Sandbox 実行) も同じ
     委譲 API 経由に変更。
   - この書き換えは mruby ビルドでも動くようにする (mruby 版でも
     同じ C API を呼ぶバインディングを用意して共通化。IRB の実装が
     二重にならないようにする)。
3. combined 生成 + spawn 統合 (T4-3 と同様、`--entry shell_entry`)。

### T4-5: 検証 (2 日)

構成: カーネル + desktop が Spinel (2 インスタンス同居)。
T4-4 を実施した場合は shell も Spinel で 3 インスタンス。

1. 起動と基本操作の回帰 (自律検証ツール):
   - デスクトップ表示、メニュー、Launcher、時計表示更新
   - shell (mruby のまま、または Spinel 化した場合はその構成) の起動、
     `help` 実行、代表コマンド数個 (ls / cat / ps 相当、
     shell_commands.rb を読んで選ぶ)、スクロール
   - IRB: `1+1` → `2`、複数行、例外の表示 (shell が mruby のままなら
     従来経路の回帰確認、Spinel 化した場合は委譲経路の確認)
   - .toml なしスクリプト実行 (適当なテストスクリプトを flash/ に置く)
   - editor / monitor 等 mruby のままの default_app が従来どおり
     起動できること (mruby アプリと Spinel アプリの共存確認)
2. 性能計測:
   - kernel 側は Phase 2 で入れた **hid_lat 計測 (input_router.rb、
     1000 イベントごとの sum/max/閾値カウント)** をそのまま使い、
     MC 化 + estalloc 化による劣化がないことを確認する
     (Phase 2 実測: mruby 比 80-150 倍。これが基準線)。
   - desktop: `draw_foreground` / `on_update` の所要時間ログ
     (Machine.board_millis ベース、mruby 版と共通コード、hid_lat と
     同形式) を mruby 版と比較。目標は Ruby 実行部分の大幅短縮
     (描画転送分は残ることを織り込んで、内訳を分離計測する:
     gfx 呼び出し前後の時間 vs それ以外)。
   - **mc アロケーション overhead の実測**: Phase 3.5 のマイクロベンチ
     最悪 6-7% が実ワークロードでどうかをここで確定する。恒常的に
     5% を超える場合は raw (非 zero-fill) フック追加を fork へ起案
     (判断材料の数値をレポートに)。
   - **typed symbol-hash の着手判断**: desktop の Ruby 実行部で
     poly アクセス (`win[:x]` 等の symbol-hash) が支配的と計測で出た
     場合のみ、fork_pr_candidates.md C 節の typed symbol-hash
     (設計評価済み、3-5 日規模) を起案する。支配的でなければ見送りを
     レポートに明記する。
   - resize プレビューのレート制限 (input_router / desktop 側の
     100ms 制限) を緩められるかを実験し、体感改善の根拠を取る。
   - shell を Spinel 化した場合のみ: 大量出力コマンド (長い ls 相当) の
     表示時間比較。
3. メモリ: 各 Spinel インスタンスの GC ヒープ使用量、mruby 版との比較表。

## 受け入れ基準

1. kernel + desktop の 2 インスタンス同居で T4-5 のシナリオ全パス。
   混成構成 (一部 mruby) でも起動可能。
2. shell (mruby のまま/Spinel 化いずれでも) の IRB とスクリプト実行が
   ユーザ視点で従来同等に動く。
3. desktop の Ruby 実行部分の時間が mruby 版比で明確に短縮
   (数値でレポート)。draw 全体の改善幅は転送コストに依存するため
   目標値は設けないが、内訳が計測されていること。
4. エンジン切替がアプリ単位で可能で、mruby 版に回帰がない。
5. shell の扱い (Spinel 化した / 見送った) とその根拠がレポートに
   記録され、ユーザの承認を得ている。

## 落とし穴・注意

- desktop は ~4,200 行 + 13 mixin と大きい。**一気に全部を通そうと
  しない**こと。推奨順: まず boot アニメーション+メニューバーのみの
  縮退版で起動 → mixin を数個ずつ有効化 → 全量。コンパイルエラーは
  spinel-reduce で最小化して fork に報告 (コンパイラバグの可能性を
  常に疑う。Ruby 側の回避とコンパイラ修正のどちらが正しいか判断し、
  コンパイラ修正は fork へ)。
- Float を含む UI 計算 (アニメーション等) は mruby と Spinel で
  丸め差が出る可能性がある。座標は最終的に Integer 化されるので
  実害は出にくいが、スクリーンショット比較で 1px 差を許容するか
  どうかの基準を先に決める (推奨: 構造一致を目視 + 主要 UI 要素の
  位置をピクセル判定するスクリプト)。
- shell の $stdout / $LOAD_PATH などグローバル変数は Spinel では
  静的型が付く。poly になって推論が悪化する場合は、$stdout を
  単一クラス (ShellOut) に固定する設計へ寄せる。
- Sandbox worker タスクのヒープは mruby 用 (fmrb_malloc 系) で確保。
  Spinel 側インスタンスのヒープと混ぜない。
- 3 プログラム分の生成 C でビルド時間・バイナリサイズが増える。
  サイズは Linux ではただ記録し、対策は Phase 5 で考える。
- **両側一致必須フラグ** (ランタイム snapshot のビルドと生成 C の
  コンパイルで値が食い違うと silent ABI break):
  `SP_GC_STACK_MAX` / `SP_MULTI_CTX` / `-include sp_mem_override.h`。
  CMake で単一の変数から両方へ配ること。加えてコンパイラ (SPINEL_PIN) と
  snapshot (IMPORT_INFO) の commit 一致 (spinel:gen が警告する)。

## 完了レポート

`doc/spinel_aot/reports/phase4_report.md`:
- FmrbApp/FmrbGfx メソッド対応表 (実装済み/未使用で省略の別)
- Sandbox 委譲の設計 (タスク構成図) と挙動確認結果
- 性能比較表 (desktop 内訳計測、shell、メモリ)
- スクリーンショット一式のパス
- fork へ報告/修正したコンパイラ問題の一覧
- Phase 5 (ESP32) への引き継ぎ (サイズ、ヒープ設定の推奨値)
