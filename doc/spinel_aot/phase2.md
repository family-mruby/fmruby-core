# Phase 2 指示書: カーネル VM の Spinel 化 (Linux)

前提: Phase 1 完了 (--no-main、fmrb_spx 骨格、エンジン切替、hello 疎通)。
この Phase では Spinel インスタンスは 1 個 (カーネルのみ) なので
マルチインスタンス化 (Phase 3) は不要。

## 目的

カーネル VM (`FmrbKernelImpl`、`main/prebuild_scripts/kernel/`) を
Spinel コンパイルされたネイティブタスクとして動かし、mruby 版と
同一の外部挙動を自律検証ツールで確認し、性能を比較計測する。

## 設計方針 (必読)

1. **デュアルビルド原則**: `fmrb_kernel.rb` と `fmrb_kernel/*.rb` の
   ロジック本体は 1 ソースを維持し、mruby / Spinel の両方でビルド可能に
   保つ。処理系差は「ベースクラス層」に閉じ込める:
   - mruby 版: `FmrbKernel` ベースクラスは C 定義
     (lib/add/picoruby-fmrb-kernel) のまま。
   - Spinel 版: `FmrbKernel` ベースクラスを **Ruby で再実装**した
     `main/prebuild_scripts/spinel/fmrb_kernel_base_spinel.rb` を用意し、
     各メソッド (`_send_raw_message` 等) を FmrbSpx FFI 呼び出しで
     実装する。上に乗る `FmrbKernelImpl` は無改変で共通。
2. **制御反転 (poll 化)**: 現行は C の `_spin` がキューを drain して
   Ruby `msg_handler` を mrb_funcall する。これを
   「Ruby 側ループが `_poll_message` で 1 件取り出し、Ruby 内で
   `msg_handler` を呼ぶ」構造へ変える。**mruby 版も同じ構造に変更**して
   ソースを揃える (mruby 側 C バインディングに `_poll_message` を追加)。
3. メッセージ表現: `_poll_message` の戻りは
   `{ "type" => Integer, "src_pid" => Integer, "data" => String } | nil`
   とする (キーは文字列。現行 `_spin` が組み立てる Hash と揃える。
   現行がシンボルキーならそれに合わせ、mruby/Spinel で同一にする)。
   Spinel 版の実装は FmrbSpx.recv_message のバイト列から Ruby 側で
   Hash を組み立てる。

## タスク

### T2-1: mruby 側の poll API 追加と main_loop 再構成 (1 日)

1. `lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c` に
   `_poll_message(timeout_ms)` を追加 (既存 `_spin` の drain 部分を
   1 件取り出しに分解。既存 `_spin` は当面残すが、main_loop からの
   利用は poll に置き換え、置き換え完了後に `_spin` を削除する。
   Legacy を残さない原則)。
2. `fmrb_kernel.rb` の `main_loop` を poll ループへ書き換え:
   ```ruby
   loop do
     msg = _poll_message(@tick)
     if msg
       msg_handler(msg)
     else
       tick_process   # timeout: periodic work
     end
     # drain burst: poll with timeout 0 until empty, bounded per frame
   end
   ```
   現行 `_spin` の「1 tick で複数メッセージを drain する」挙動を
   維持すること (バーストドレイン: timeout 0 の poll を上限付きで回す。
   上限は現行実装の挙動を読んで合わせる)。
3. `rake clean && rake build:linux` (mruby エンジン) →
   dev_run_check.sh + 入力注入 (ドラッグ、メニュー操作、アプリ起動) で
   回帰なしを確認。**ここまでを先に安定させる** (Spinel と無関係に
   意味のあるリファクタなので、ユーザにコミット依頼してよい区切り)。

### T2-2: Spinel 用ベースクラス層 (1-2 日)

`main/prebuild_scripts/spinel/fmrb_kernel_base_spinel.rb`:

1. `class FmrbKernel` を定義し、`FmrbKernelImpl` が呼ぶ全メソッドを
   FFI 実装する。対象メソッドは grep で洗い出す
   (`_init`, `_set_ready`, `_poll_message`, `_send_raw_message`,
   `_try_send_raw_message`, `_get_window_list`, `_set_hid_target`,
   `_set_focused_window`, `_bring_to_front`, `_update_window_position`,
   `_update_window_size`, `_suspend_app`, `_resume_app`, `_reap_app`,
   `_get_app_info`, `_spawn_app_req`, `_get_last_error`,
   `_get_sync_files`, `_sync_file`, `_sync_time_to_host`,
   `check_protocol_version`, `check_ga_version`, `_set_error_led`,
   `boot_complete!` — 実物を確認して過不足なく)。
2. `_get_window_list` は fmrb_spx_windows_snapshot のバイト列を
   getbyte でパースし、現行 C 実装が返すのと**同一構造の Ruby 値**
   (Hash の配列等。実物を確認) を返す。
3. `Machine.board_millis` 相当、`Log` モジュール、`FmrbConst` も
   ここで供給する:
   - `Log`: fmrb_spx_log_write への薄いラッパ。
   - `FmrbConst`: C の定数テーブル (lib/add/picoruby-fmrb-const の
     const.c) から Ruby 定数定義を**生成するスクリプト**
     `tool/spinel/gen_const_rb.rb` を作り、ビルド時に
     `fmrb_const_generated.rb` を生成する (手書き二重管理を避ける)。
4. I2C/RTC: `sync_rtc` は Phase 1 で C 化した `fmrb_spx_sync_rtc` を
   呼ぶ形に `fmrb_kernel.rb` 側を変更 (mruby 版も同じ関数を呼ぶ
   バインディングにして共通化する)。

### T2-3: MessagePack 置換 (半日)

1. Phase 0 の `msgpack_pure.rb` を `main/prebuild_scripts/spinel/` へ
   移し (または common な場所)、モジュール名を `MessagePack` として
   `pack` / `unpack` 互換 API にする。
2. Spinel ビルドではこれを連結。mruby ビルドは従来の C msgpack のまま。
3. 相互運用テスト: mruby 側 (C msgpack) で pack したバイト列を
   Spinel 側 (pure) で unpack、およびその逆が一致すること。
   Phase 0 のテストベクタに「実際にカーネルが流すメッセージのダンプ」
   を数件追加して確認する (audio_handler / app 制御メッセージ)。

### T2-4: combined ソース生成とビルド統合 (1 日)

1. Spinel 用 combined 生成: `tool/debug/gen_combined_rb.py` を参考に
   (または拡張して)、以下の順で連結した
   `fmrb_kernel_combined_spinel.rb` を CMake で生成する:
   1. `spinel/fmrb_ffi.rb` (FFI 宣言)
   2. `fmrb_const_generated.rb`
   3. `spinel/msgpack_pure.rb`
   4. `spinel/fmrb_kernel_base_spinel.rb`
   5. `kernel/fmrb_kernel/*.rb` (mixin 群、picorbc ビルドと同じ sort 順)
   6. `kernel/fmrb_kernel.rb`
   7. 末尾に起動コード: `FmrbKernelImpl.new.boot` 等
      (現行の mruby 側エントリがどう Impl を起動しているか
      kernel.c / fmrb_kernel.rb を読んで同じ流れにする)
2. `compile_ruby_to_spinel.cmake` で
   `spinel --no-main --entry fmrb_kernel_entry -c` を実行し生成 C を
   COMPONENT_SRCS へ。コンパイル警告はエラー扱い (Spinel は
   warning-free 生成を謳うので、警告が出たらコンパイラ側の問題として
   fork へ報告・修正)。
3. Ruby 側の書き換えが必要になった箇所 (Phase 0 の UNSUPPORTED 対応
   含む) は、**mruby でも動く書き方**へ寄せる。どうしても分岐が要る
   場合のみ `spinel/` 配下の差し替えファイルにする。分岐は最終手段。

### T2-5: タスク起動統合 (1 日)

1. `main/app/fmrb_app.c` にロードモード
   `FMRB_LOAD_MODE_NATIVE` を追加: `fmrb_app_attr_t` に
   `int (*native_entry)(void)` を持たせ、spawn 時に VM を作らず
   タスク関数から native_entry を呼ぶ。ヒープ・スタックサイズは
   `fmrb_task_config.h` のカーネル設定を流用しつつ、Spinel 用に
   必要なら別定数を足す (mruby ヒーププールは確保しない)。
2. `main/kernel/fmrb_kernel.c` のカーネル spawn を
   `FMRB_KERNEL_ENGINE` で分岐: mruby = 従来 bytecode、spinel =
   NATIVE + `fmrb_kernel_entry`。
3. Spinel ランタイム初期化: entry 呼び出し前にランタイムの
   初期化が必要か fork の生成コードを確認 (Phase 1 の hello で把握済みの
   はず)。GC ヒープの事前確保は不要 (calloc ベース) だが、
   将来の Phase 5 のためにタスク側に init フックを置いておく。
4. 終了処理: カーネルは通常 return しないが、return した場合は
   エラーログ + 既存のカーネル異常処理 (再起動系) に繋ぐ。

### T2-6: 検証 (1-2 日)

すべて `FMRB_KERNEL_ENGINE=spinel` ビルドで、リポジトリルートの
自律検証ツールを使う。比較基準は同一シナリオの mruby ビルド。

1. 起動: dev_run_check.sh でブートマーカー到達 + 画面 PNG が mruby 版と
   同等 (デスクトップ表示、メニューバー、boot アニメーション終了)。
2. シナリオ回帰 (fmrb_input.py):
   - メニューバークリック → Launcher 起動 → アイコンダブルクリックで
     アプリ (shell) 起動 (CLAUDE.md の座標例を参考に、実際の画面を
     スクリーンショットで確認しながら座標を決める)
   - ウィンドウのドラッグ移動 (down → move x10 → up) 後の位置が
     期待どおり
   - リサイズ操作、フォーカス切替 (2 アプリ起動して交互クリック)
   - アプリ終了 → 再起動 (spawn/reap 経路)
   - キーボード入力が shell に届く (text "help" key enter)
   各操作後に fmrb_screenshot.py で確認。
3. 性能計測: カーネル Ruby に計測コードを入れる
   (`Machine.board_millis` ベースで 1 イベント処理時間の max/avg を
   1000 イベントごとに Log 出力。mruby/Spinel 共通ソースなので両方で
   同じログが出る)。マウス洪水 (move を 100Hz 相当で 30 秒注入) 中の
   max latency を比較。目標: Spinel 版で「hid_event slow (>25ms)」警告
   (input_router.rb:299 付近) がゼロになること。
4. メモリ: Linux 上でカーネルタスク相当のヒープ使用量
   (Spinel: GC heap bytes をログ出力する debug 関数を追加 /
   mruby: 既存 heap_info) を記録。
5. 長時間 soak: マウス洪水 + アプリ起動終了ループを 30 分回して
   クラッシュ・リーク (heap bytes の単調増加) がないこと。

## 受け入れ基準

1. T2-1 の poll 化後、mruby 版に回帰がない。
2. spinel 版で T2-6 のシナリオ回帰が全項目 mruby 版と同等。
3. マウス洪水時の max latency が mruby 版より改善し、25ms 警告ゼロ。
4. 30 分 soak でクラッシュ・リークなし。
5. mruby / spinel の両エンジンが同一ソースからビルドできる
   (差し替えファイルは spinel/ 配下のベース層のみ)。
6. 性能比較表 (avg/max latency、ヒープ使用量) がレポートにある。
   **この数値が Phase 4 (desktop/shell) 着手判断の基礎データになる。**

## 落とし穴・注意

- msg_handler の Hash キー (文字列 vs シンボル) の不一致は静かに壊れる。
  現行実装 (kernel.c の _spin が組む Hash) を必ず読んで合わせる。
- Spinel は型推論コンパイラなので、poll の戻り値のような
  「Hash or nil」は nullable/poly 型になる。生成 C のコンパイルエラーや
  推論警告が出たら、戻り値を「常に Hash を返し、空は type=0 で表す」
  形に変えるなど、単型化する書き換えを検討 (mruby 互換のまま可能)。
- 例外: FFI 呼び出しは例外を投げない (エラーコード)。現行コードの
  rescue 前提の箇所 (top-level rescue 等) はそのまま活きるが、
  C 例外 (mruby が raise していた箇所、例: 引数エラー) の意味論が
  変わる。ベース層でエラーコード → raise 変換を行い挙動を揃える。
- input_router.rb:170 付近の「ArgumentError 蓄積で mruby コンテキストが
  壊れる」対策 clamp はそのまま残す (挙動互換のため)。
- 生成 C ファイルは大きくなる。ビルド時間が問題になったら
  レポートに記録 (最適化は後回し)。

## 完了レポート

`doc/spinel_aot/reports/phase2_report.md`:
- 変更ファイル一覧 (mruby 側共通変更 / spinel 専用層の区別)
- シナリオ回帰の結果表 (スクリーンショットのパス)
- 性能比較表 (mruby vs spinel: avg/max latency、ヒープ、soak 結果)
- Ruby ソースに必要だった書き換えの一覧 (デュアルビルド逸脱が
  ないことの確認)
- Phase 4 判断への示唆 (desktop の Ruby 実行比重の見立て)
