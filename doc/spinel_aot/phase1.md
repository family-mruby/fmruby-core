# Phase 1 指示書: フォーク整備 + ライブラリモード + 組み込み基盤 (Linux)

先に `00_common.md` と Phase 0 レポートを読むこと。
この Phase の成果物は (a) Spinel フォークの `--no-main` 対応、
(b) fmruby-core 側のランタイムコンポーネントと FFI シム層の骨格、
(c) ビルド統合 (エンジン切替フラグ)、である。
カーネル本体の Spinel 化は Phase 2 で行う。ここでは「Spinel でコンパイル
した最小プログラムが firmware (Linux 版) 内のタスクとして動く」まで。

## タスク

### T1-1: フォークの利用準備 (Phase 0 でほぼ消化済み)

fork の作業チェックアウトは
`/home/kishima/fmrb/family-mruby/tmp/spinel`
(origin = git@github.com:kishima/spinel.git、upstream 元 = matz/spinel)。
作業ブランチ `fmrb-dev`、upstream remote は設定済み。Phase 0 で汎用バグ
修正 5 件 (318f4a7b, 56394f2d, 7b820768, a8c3c201, d9e363ed) が
fmrb-dev にコミット済み (make test 回帰ゼロ)。

残作業:

1. fmrb-dev を origin へ push する (未 push の場合)。
2. コミット規約の再確認 (fork 内):
   - upstream へ PR しうる汎用変更と fmruby 固有変更を別コミットにする。
   - コミットメッセージは英語。
   - 各コミットで `make` (gen2.c == gen3.c の self-check を含む)、
     `make test`、`make bench` を通す。
   - AI 支援コミットには upstream の慣習に従い
     `Co-Authored-By:` トレーラを付ける。
3. family-mruby からの参照方法 (サブモジュール化するか等) は Phase 5 までに
   決めれば良い。現状はルートリポジトリの gitignore 対象 (`tmp/`) の
   チェックアウトを直接使う。
4. fmruby-core 側の作業ブランチは `feature/spinel-aot` (develop から作成)。

### T1-1b: Phase 0 知見のハードニング (fork、1 日)

Phase 0 の UNSUPPORTED.md で「Phase 1 修正候補」とした 2 件を fork で
修正する (どちらも汎用バグとして upstream PR 可能な粒度で):

1. U-1: poly レシーバの `String#ljust`/`rjust` が dispatch されない。
2. U-3: nested-array 由来の poly 値を ctor / mixin 算術に渡すと
   miscompile する (Phase 0 では添字アクセス / concrete 化で回避した)。
   最小再現は `tool/spinel_poc/repro/` にある。

U-2 (同名 `rescue => e` の複数 arm) は Spinel の意図的制約のため
修正しない (OS コード規約側に記載済みの回避で対応)。

### T1-1c: typed symbol-hash の設計検討 (fork、設計のみ 0.5-1 日)

Phase 0 の核心発見: symbol-keyed hash は常に poly 値になり、カーネルの
ホットパスの型特殊化を妨げる (フルハーネスで CRuby 同等に留まった主因)。
恒久対策の候補 (a) Spinel に typed symbol-hash 型 (SYM_INT/SYM_STR 等) を
追加、の実装規模とデータレイアウトをこの Phase で**設計評価**する
(実装は Phase 3 と同時期を想定。analyze/codegen/runtime の 3 層に
またがるため、無理に Phase 1 で実装しない)。
代替の (b) FFI 境界で typed String を渡す設計は T1-5 に織り込み済み、
(c) window list の poll 化は Phase 2 で扱う。評価結果は完了レポートへ。

### T1-2: ライブラリモード `--no-main` / `--entry` の実装 (fork、1-2 日)

対象: `src/main.c` (CLI)、`src/codegen.c` (main 生成部、5561 行付近)。

1. CLI: `--no-main` (bool) と `--entry <name>` (デフォルト
   `spinel_program_main`) を追加。`usage()` (`src/main.c:165` 付近) と
   フラグテーブル (`src/main.c:213-272` 付近) を更新。
   `--no-main` 指定時は cc 起動をスキップして `-c` 相当の動作
   (C 生成のみ) にする。単体実行できない C を勝手にリンクしないため。
2. codegen: `int main(int argc,char**argv)` を生成している箇所
   (`src/codegen.c:5561` 付近) を条件分岐し、`--no-main` 時は
   `int <entry>(void)` を **非 static** で生成する。
   main 本体がやっている以下の処理の扱いを決めて実装する:
   - `SP_GC_SAVE()` : entry でも必要。維持。
   - `sp_re_init()` (regexp 使用時のみ) : 維持。
   - `sp_sched_init()` / `sp_sched_drain()` (Thread 使用時のみ) :
     組み込み対象は Thread 禁止だが、生成条件はそのまま維持でよい
     (使わなければ出ない)。
   - ARGV コピー: `--no-main` 時は生成しない (ARGV は空配列として定義)。
   - `atexit` 経由の END ブロック: `--no-main` 時は atexit 登録をやめ、
     entry の return 直前に直接呼ぶ。
   - BEGIN ブロック: entry 先頭に維持。
   - 戻り値: 正常終了 0。トップレベル未捕捉例外は abort せず
     非 0 を返す形にできるか検討し、まずは既存の例外終了処理
     (メッセージ出力) を維持したうえで到達したら非 0 return。
3. スモークテスト (fork 内に追加):
   - `test/` の既存ハーネスに乗せられない場合は `make notest-nomain`
     のような専用ターゲットで良い。内容: 適当な .rb を
     `--no-main --entry sp_entry -c` でコンパイル → 生成 C に
     `int sp_entry(void)` があり `int main(` がないことを確認 →
     ホスト C スタブ `int main(){return sp_entry();}` とリンクして実行し、
     通常コンパイル時と出力一致。
4. 既存動作の無変更確認: `make test` 全パス、`make bench` 劣化なし。
5. このコミットは upstream PR 候補。コミットメッセージにその旨を書く。

### T1-3: 生成 C への C コード注入手段 (fork、半日、暫定でよい)

C 側から Spinel 内部 (static メソッド) を呼ぶ必要が出た場合に備えた
逃げ道。Phase 2 の設計 (制御反転) では entry 1 本で足りる想定なので、
**最小実装**でよい:

1. `--inject <file.c>` オプション: 生成 C の末尾 (全 static 定義の後) に
   指定ファイルの内容をそのまま連結する。
2. スモークテスト 1 本 (inject した関数から生成側 static 関数を呼べること)。
3. 使わずに済めばそれが最善。Phase 2 以降で不要と確定したら削除検討を
   レポートに書く。

### T1-4: fmruby-core 側ランタイムコンポーネント (1-2 日)

1. 配置: `components/fmrb_spinel_rt/` を新設。
   - `spinel_rt/` : fork の `lib/` から必要ファイルをコピーした
     スナップショット (sp_runtime.h, sp_types.h, sp_gc.[ch],
     sp_alloc.[ch], sp_core.[ch], sp_string.[ch], sp_str.[ch],
     sp_array.[ch], sp_inspect.[ch], sp_format.[ch], sp_cold.c,
     sp_io.[ch], sp_enum.h。regexp / bigint / fiber / sched / net /
     system / crypto / pack / time / random は**入れない**。
     ただし生成 C が time/random を要求したら追加してよい)。
   - `import_from_fork.rb` (または rake タスク `spinel:import`):
     fork のパスを引数に取り (必須)、上記ファイル一覧をコピーするスクリプト。
     コピー元コミット hash を `spinel_rt/IMPORT_INFO` に記録する。
     スナップショットはリポジトリにチェックインする (再現性優先)。
2. CMakeLists.txt: 既存コンポーネント (例: `components/fmrb_msg/`) を
   参考に作成。コンパイルフラグに `-ffunction-sections -fdata-sections`
   と `-DSP_GC_STACK_MAX=8192` を付ける (Linux ではメモリに余裕がある
   ため 8192。ESP32 向け調整は Phase 5)。
   注意: `sp_gc.h` の指示どおり、`SP_GC_STACK_MAX` は
   **ランタイム側と生成 C 側の両方に同じ値**を定義する必要がある。
3. Linux ターゲットでのビルド確認: `rake build:linux` が通ること
   (この時点ではリンクされるだけで未使用でよい)。
   リンカの --gc-sections は ESP-IDF ビルドが既に
   function-sections/gc-sections を使っているか確認し、無ければ
   コンポーネントの INTERFACE リンクオプションで足す。

### T1-5: FFI シム層 fmrb_spx の骨格 (1-2 日)

目的: Spinel の FFI (`ffi_func`) から呼べる「素の C 関数」の層。
mruby バインディング (`lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c`)
と実装を共有する。

1. 既存コードの調査: kernel.c の各 mrb メソッド (`_spin`,
   `_send_raw_message`, `_get_window_list`, `_set_hid_target`,
   `_set_focused_window`, `_bring_to_front`, `_update_window_position`,
   `_update_window_size`, `_suspend_app`, `_resume_app`, `_reap_app`,
   `_get_app_info`, `_spawn_app_req` 等) が呼んでいる下位 C 関数
   (fmrb_kernel.c / fmrb_msg 等) を特定する。
2. `main/kernel/fmrb_spx_kernel.c` + `main/kernel/include/fmrb_spx.h` を
   新設し、mrb に依存しない C ABI を定義する。設計原則:
   - 戻り値は `fmrb_err.h` 標準 (fmrb_err_t)。ただし FFI から使いやすい
     よう「負値 = エラー、0 以上 = 結果」の int 返しも可 (ヘッダに
     Doxygen コメントで明記。main/ 以下のヘッダなので Doxygen 対象)。
   - 構造化データは**固定レイアウトのバイト列**で渡す (Spinel FFI は
     struct を渡せない。Ruby 側は getbyte でパースする。カーネル Ruby は
     既に同種のバイナリ処理をしているので一貫する)。
   - **poly を境界に持ち込まない** (Phase 0 の知見): FFI の戻り・引数は
     typed String / Integer に限定し、Ruby 側でも受け取ったバイト列を
     symbol-hash へ詰め直すのはホットパスでは避ける (詰め直した時点で
     poly になり型特殊化が失われる)。ホットパスはバイト列のまま
     getbyte で読む設計を既定とする。
   - 例 (シグネチャ案。実装時に既存下位関数へ合わせて調整可):
     ```c
     /* Poll one message. Returns payload length (>=0), 0 with *type==0
        when timed out, or negative fmrb error. */
     int fmrb_spx_recv_message(uint8_t *buf, int cap, int timeout_ms,
                               int *type, int *src_pid);
     int fmrb_spx_send_raw(int dst_pid, int type,
                           const uint8_t *data, int len);
     int fmrb_spx_try_send_raw(int dst_pid, int type,
                               const uint8_t *data, int len);
     /* Fills packed window records; returns record count or negative. */
     int fmrb_spx_windows_snapshot(uint8_t *buf, int cap);
     int fmrb_spx_set_hid_target(int pid);
     int fmrb_spx_set_focused_window(int win_id);
     int fmrb_spx_bring_to_front(int win_id);
     int fmrb_spx_update_window_pos(int win_id, int x, int y);
     int fmrb_spx_update_window_size(int win_id, int w, int h);
     int fmrb_spx_suspend_app(int pid);
     int fmrb_spx_resume_app(int pid);
     int fmrb_spx_reap_app(int pid);
     int fmrb_spx_spawn_app_req(const char *name, int len);
     int fmrb_spx_app_info_snapshot(int pid, uint8_t *buf, int cap);
     uint32_t fmrb_spx_board_millis(void);
     void fmrb_spx_log_write(int level, const char *msg, int len);
     ```
   - `_get_window_list` が返す情報の項目 (id, pid, x, y, w, h, z,
     flags, タイトル等) は実装を読んで確定し、レコードレイアウトを
     `fmrb_spx.h` に定義・コメントで文書化する。
   - RTC / I2C (`sync_rtc`): Ruby から I2C を直接叩くのはやめ、
     `int fmrb_spx_sync_rtc(void)` として C 側に一括実装 (既存の
     mruby 用 RTC gem のロジックを C で呼ぶ)。ファイル同期系
     (`_get_sync_files` / `_sync_file`) も同様にバイト列 or 一括 C 化。
3. mruby バインディング側 (lib/add の kernel.c) は、可能な範囲で
   fmrb_spx_* を呼ぶ形にリファクタして実装を一本化する
   (lib/ 以下を触るので `rake clean` を忘れない)。
   共有ロジックの置き場所は main/ 側 (fmrb_spx_kernel.c)。
4. 単体テスト: Linux ビルドで、C のテスト関数 (既存のテスト機構が
   あればそれ、なければ起動時に一度呼ぶ debug 関数) から
   fmrb_spx_recv_message / send_raw の往復を確認。

### T1-6: Ruby 側 FFI 宣言と hello-kernel 疎通 (1 日)

1. `main/prebuild_scripts/spinel/fmrb_ffi.rb` を新設:
   ```ruby
   module FmrbSpx
     ffi_func :fmrb_spx_board_millis, [], :int
     ffi_func :fmrb_spx_log_write, [:int, :str, :int], :void
     ffi_func :fmrb_spx_recv_message, [:ptr, :int, :int, :ptr, :ptr], :int
     # ... (fmrb_spx.h の全関数。型対応は spinel docs/FFI.md 準拠)
   end
   ```
   バッファ渡しは `ffi_buffer` / `:ptr` / `ffi_read_*` の流儀を
   docs/FFI.md と examples/ffi/ で確認して合わせること。
2. `main/prebuild_scripts/spinel/hello_kernel.rb`: FmrbSpx 経由で
   Log に "spinel hello" を出し、board_millis を 3 回読み、
   10 メッセージ poll したら return する最小プログラム。
3. ビルド統合 (`main/CMakeLists.txt` +
   `main/prebuild_scripts/compile_ruby_to_bytecode.cmake` を参考に
   新ファイル `compile_ruby_to_spinel.cmake`):
   - fork の spinel バイナリの場所は CMake 変数 `SPINEL_BIN`
     (rake から渡す) で受ける。
   - `.rb → spinel --no-main --entry <name>_entry -c → .c` を
     生成し COMPONENT_SRCS に追加。
   - 生成 C のコンパイルフラグに `-DSP_GC_STACK_MAX=8192`
     (T1-4 と同値) と `-I` (spinel_rt ヘッダ) を付ける。
4. エンジン切替: rake 引数または環境変数 `FMRB_KERNEL_ENGINE`
   (`mruby` デフォルト / `spinel`) を CMake に伝搬。
   この Phase では「spinel 選択時に hello_kernel をカーネルタスクの
   代わりに起動する」debug 経路でよい (spawn 統合は Phase 2)。
   FreeRTOS タスクとして `hello_kernel_entry()` を呼ぶラッパタスクを
   `main/kernel/fmrb_kernel.c` に条件コンパイルで追加。
5. 動作確認: `rake clean_all` 後、`FMRB_KERNEL_ENGINE=spinel rake
   build:linux` → docker 起動でログに "spinel hello" と millis、
   poll 結果が出ること。`FMRB_KERNEL_ENGINE=mruby` (または未指定) で
   従来どおり起動し、`tools/dev_run_check.sh` の画面キャプチャが
   従来と同等であること (デグレなし確認)。

## 受け入れ基準

1. fork: `--no-main --entry` が実装され、スモークテストがあり、
   `make test` が Phase 0 基準 (1,991 pass / 1 既存 cosmetic fail) を維持し
   `make bench` が劣化なし。T1-1b の 2 修正が入っている。
2. fmruby-core: `FMRB_KERNEL_ENGINE=mruby` で従来と同一動作
   (dev_run_check.sh で起動確認)。
3. `FMRB_KERNEL_ENGINE=spinel` で hello_kernel がタスクとして動き、
   FFI 経由の Log 出力・millis 取得・メッセージ poll が確認できる。
4. fmrb_spx.h の API が文書化 (Doxygen コメント) されている。
5. mruby バインディングのリファクタ後も既存機能に回帰がない
   (dev_run_check.sh + 手動シナリオ: メニュー操作、アプリ起動)。

## 落とし穴・注意

- lib/add を触ったら `rake clean`。エンジン切替を試すたびに生成物の
  食い違いが出やすいので、切替時は `rake clean_all` を習慣にする。
- Spinel の FFI 宣言は module 本体に直接書く必要がある (docs/FFI.md)。
  宣言の型と C 実装の型のズレはリンクエラーにならず実行時破壊になる
  ので、`fmrb_spx.h` と `fmrb_ffi.rb` の対応表をレビューすること。
- 生成 C とランタイムの `SP_GC_STACK_MAX` 不一致は BSS サイズ不一致で
  静かに壊れる。CMake の 1 変数から両方へ渡す実装にする。
- ESP32 ビルド (`rake build:esp32`) はこの Phase では壊れていても
  よい、は**不可** (ビルド対象から外すのは禁止)。spinel 経路を
  ESP32 では無効化するのではなく、`FMRB_KERNEL_ENGINE=mruby` 既定で
  従来どおりビルドが通ることを確認する (spinel コンポーネントは
  ESP32 では Phase 5 までビルド対象外に**しない**で済むよう、
  コンパイル可能な範囲のファイルのみ import している。もし
  malloc_trim 等でコンパイルが通らない場合は、Phase 5 を待たず
  ここで最小ガード (`#if defined(ESP_PLATFORM)`) を入れる)。

## 完了レポート

`doc/spinel_aot/reports/phase1_report.md`:
- fork のブランチ/コミット一覧 (PR 候補と fmrb 固有の区別)
- fmrb_spx API 一覧と window record レイアウト
- hello_kernel の動作ログ抜粋
- 既知の課題と Phase 2 への引き継ぎ
