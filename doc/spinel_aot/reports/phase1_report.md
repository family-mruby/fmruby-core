# Phase 1 進捗レポート: フォーク整備 + ライブラリモード + 組み込み基盤

作業ブランチ: fmruby-core `feature/spinel-aot`、Spinel fork `fmrb-dev`。
Phase 0 の Go 判定を受けて着手。本レポートは実装の進捗と設計評価を記録する
(進行中。完了時に受け入れ基準の充足を追記)。

## サマリ (現時点)

- **Spinel fork 側 (T1-1, T1-1b, T1-2, T1-3): 完了・push 済み・全ゲート通過**。
- fmruby-core 側 (T1-4): ランタイムコンポーネント作成済み、ビルド検証中。
- T1-1c: typed symbol-hash 設計評価は下記。
- 残: T1-5 (FFI シム fmrb_spx)、T1-6 (hello_kernel 疎通・エンジン切替)。

## Spinel fork コミット一覧 (fmrb-dev, origin push 済み)

Phase 0 の 5 件に加え、Phase 1 で以下を追加:

| commit | 種別 | 内容 |
|---|---|---|
| d9c6dbf9 | PR候補 | String#ljust/rjust/center が poly レシーバで dispatch (T1-1b U-1) |
| 9aa7cdd6 | PR候補 | library mode `--no-main`/`--entry` + `--inject` (T1-2/T1-3) |
| 5129af95 | PR候補 | value-consumed な poly return を void 化しない + value-type block local reset (T1-1b U-3) |

ゲート (HEAD=5129af95): **make test 1,991 pass / 1 fail** (既存 cosmetic)、
**make bench 58 pass / 0 fail**、**make test-lib-mode PASS**。
fmruby-core PoC parity (`tool/spinel_poc/run_all.sh`) **9/9 ok**。

### T1-1b ハードニング結果

- U-1 (poly の ljust/rjust/center 未 dispatch): 修正済 (d9c6dbf9)。
- U-3 (nested-array poly を ctor / mixin 算術に渡すと miscompile): 修正済 (5129af95)。
  2 つの汎用バグ: (a) `g_ret_no_new_poly` ゲートが「値が読まれる poly return」まで
  非 poly に留め void 関数を生成 → value-consumption 構文スキャンを追加し、
  値が消費される return のみ poly 化を許可 (optcarrot poke の性能は維持)。
  (b) value-type object の block local を `NULL` でリセットしていた (C 構造体なので
  非コンパイル) → `(sp_X){0}` でリセット。
- U-2 (同名 rescue 複数 arm) は Spinel 意図的制約のため未修正 (OS コード規約で回避)。
- Phase 0 の coverage 回避 (U-1/U-3 の workaround) は現状維持。U-1/U-3 が
  コンパイラ側で解消されたため、将来 workaround を外して素の構文でも一致することを
  確認可能 (任意)。

### T1-2 / T1-3 ライブラリモード

- `--no-main`: `int <entry>(void)` を非 static 生成。argv/atexit なし、END は
  return 直前に LIFO インライン、BEGIN は先頭維持。通常 `int main` 経路は byte 不変。
- `--entry NAME`: entry 名 (デフォルト spinel_program_main)。
- `--inject FILE`: 生成 C 末尾に生 C を連結 (最小の逃げ道)。Phase 2 で不要と
  確定したら削除検討。
- スモークテスト `test/lib_mode/smoke.sh` (`make test-lib-mode`): entry 生成 +
  main 不在 + stub リンク版/inject 版とも `-E` と byte 一致を検証。

## T1-1c: typed symbol-hash の設計評価

### 現状 (Phase 0 核心発見の再掲)

Spinel の hash 型は固定セット (src/types.h): `STR_INT/STR_STR/INT_INT/INT_STR`
(キー・値とも具体型) と `SYM_POLY`(symbol キー・値 poly) / `STR_POLY` / `POLY_POLY`。
**typed な symbol-hash 型が無い**ため、`win[:x]` 等はすべて poly 値になり、
カーネルのホットパスで型特殊化が失われる。

### データレイアウト評価

既存 typed hash はテンプレートとして使える:
- `sp_StrIntHash {const char**keys; mrb_int*vals; const char**order; len; cap; mask; mrb_int default_v;}` (sp_types.h:184)
- 追加する `sp_SymIntHash` は keys/order を `sp_sym*` に変えるだけ:
  `{sp_sym*keys; mrb_int*vals; sp_sym*order; len; cap; mask; mrb_int default_v;}`。
  symbol は interned mrb_int なので、**SymInt ≈ IntInt にキー表示 (inspect) の差**のみ。
  同様に `sp_SymStrHash` (vals が const char*)。値が poly のときは既存 SYM_POLY を使う。

### 実装規模 (3 層)

1. types (小): `TyKind` に `TY_SYM_INT_HASH` / `TY_SYM_STR_HASH` を追加、
   `types.c` の hash テーブル ({kind, key, val, cname}) に 2 行、`ty_hash_of` /
   `ty_hash_key` / `ty_hash_val` / `ty_hash_cname` を拡張。
2. runtime (中): `sp_SymIntHash` / `sp_SymStrHash` の struct と new/get/set/
   delete/each/length/merge/dup/inspect。IntInt/IntStr のコードをほぼ複製
   (キー比較が sp_sym 等値、キー表示が sp_sym_to_s)。poly 版 (sp_poly_length 等)
   の hash-kind switch にも 2 ケース追加。
3. analyze/codegen (中): symbol キー hash リテラルの値型推論を SYM_POLY 固定から
   「値が単一具体型なら SYM_INT/SYM_STR」に変更。`[]`/`[]=`/`each`/`fetch` 等の
   codegen dispatch に 2 kind 追加。poly 化トリガ (異種値混在で SYM_POLY へ widen)
   の維持。

### 評価と推奨

- **実現可能**。既存 typed hash (StrInt 系) が完全なテンプレートで、runtime は
  ほぼ機械的複製。難所は analyze の「symbol hash 値型推論」を StrPoly/SymPoly から
  分岐させる箇所 (str-keyed typed hash が既に存在するので前例あり)。
- **規模感**: str-keyed typed hash と同等の追加。types/runtime/codegen の 3 層に
  またがるため 3-5 日規模。silent miscompile リスクがあるためデュアルビルド +
  出力一致テストで抑える。
- **タイミング**: phase1.md の想定どおり **Phase 3 (ランタイム マルチインスタンス化)
  と同時期**が妥当。Phase 2 のカーネル Spinel 化は poly のままでも動作する
  (Phase 0 で実証済) ので、typed symbol-hash は性能最適化として Phase 3 で入れる。
  Phase 2 の FFI 境界では poly を持ち込まない設計 (T1-5) を先行させ、ホットパスの
  poly を最小化する。

## T1-4: fmruby-core ランタイムコンポーネント

- `components/fmrb_spinel_rt/` を新設:
  - `import_from_fork.rb`: fork の `lib/` から**全ヘッダ (24) + 最小 .c (10)** を
    `spinel_rt/` へコピーし、`IMPORT_INFO` に fork commit/SHA を記録。
    全ヘッダを入れる理由: `sp_runtime.h` が sp_re.h / sp_time.h / sp_marshal.h /
    sp_random.h / sp_fiber.h / sp_sched.h / sp_system.h を include するため。
    コンパイルは最小 (sp_gc/alloc/core/string/str/array/inspect/format/cold/io)。
    regexp/bigint/fiber/sched/net/system/crypto/pack/time/random は .c を入れず、
    `-ffunction-sections`/`--gc-sections` で未使用参照を除去。
  - スナップショット (fork commit 5129af95) をリポジトリにチェックイン。
  - `CMakeLists.txt`: idf_component_register + `-DSP_GC_STACK_MAX=8192`
    (INTERFACE で dependents=生成 C にも伝搬)、function/data-sections、
    `-Wl,--gc-sections` (INTERFACE)。
- ホストコンパイル検証: 10 .c すべて `-DSP_GC_STACK_MAX=8192` でクリーンコンパイル。
- `rake build:linux` 検証: **成功** (exit 0、fmruby-core.elf リンク完了)。
  ESP-IDF が `fmrb_spinel_rt` をコンポーネントとして認識・全 .c をコンパイル、
  エラーなし。現状は未参照のため最終リンクで gc-sections により実体は落ちる
  (T1-6 の hello_kernel が entry を参照した時点で組み込まれる)。→ **T1-4 完了**。

## T1-5: FFI シム層 fmrb_spx (完了)

- `main/kernel/include/fmrb_spx.h` + `main/kernel/fmrb_spx_kernel.c` を新設。
  mrb 非依存の C ABI で、mruby バインディング (kernel.c) と同じ下位関数
  (`fmrb_msg_receive/send`, `fmrb_app_get_window_list`, `fmrb_kernel_set_hid_target`
  等) を呼ぶ。設計原則を実装:
  - 戻り値は「負値=エラー、0 以上=結果/長さ」。
  - 構造化データは**固定レイアウトのバイト列**で渡す (window record 48B を
    fmrb_spx.h に文書化。Ruby は getbyte/ffi_read_* で読む)。
  - **poly を境界に持ち込まない** (payload はバイト列のまま、symbol-hash 化しない)。
  - `fmrb_spx_board_millis` は `clock_gettime(CLOCK_MONOTONIC)` (esp_timer 非依存)。
  - `fmrb_spx_recv_message` は `fmrb_msg_receive` が **ms を取る** (内部で
    MS_TO_TICKS) 点に合わせ ms を直接渡す (mruby _spin の ticks 渡しは是正)。
- kernel.c (mruby バインディング) の実装一本化リファクタは、動作中の mruby 経路の
  回帰リスクを避けるため **Phase 2 に延期** (fmrb_spx は独立実装として先行導入)。

## T1-6: hello_kernel 疎通 + エンジン切替 (完了・両エンジン検証済み)

- `main/prebuild_scripts/spinel/fmrb_ffi.rb`: FmrbSpx モジュールに ffi_func 宣言 +
  ffi_buffer (msg_buf/type_out/src_out/win_buf) + ffi_read_i32。
- `main/prebuild_scripts/spinel/hello_kernel.rb`: Log 出力 + board_millis×3 +
  メッセージ poll×10 の最小プログラム。`require_relative "fmrb_ffi"`。
- ビルド統合:
  - `compile_ruby_to_spinel.cmake`: 生成 C を COMPONENT_SRCS へ。SPINEL_BIN が
    あれば custom_command で再生成、無ければホスト事前生成物を要求。
  - **ホスト事前生成方式**を採用: fork の spinel バイナリは docker ビルドに
    マウントされないため、`rake spinel:gen` がホストで `.rb → --no-main
    --entry hello_kernel_entry → .c` を生成 (gen/ は gitignore)。docker は .c を
    コンパイルするのみ。
  - `FMRB_KERNEL_ENGINE` (env, デフォルト mruby): spinel 時に生成 C + fmrb_spx_kernel.c
    を追加、`FMRB_KERNEL_ENGINE_SPINEL` を定義、fmrb_spinel_rt を REQUIRE。
    Rakefile が env を docker に転送。
  - `fmrb_kernel.c`: `#ifdef FMRB_KERNEL_ENGINE_SPINEL` で、mruby カーネル VM の
    spawn の代わりに `hello_kernel_entry()` を呼ぶ FreeRTOS タスクを起動
    (kernel キューを登録してから poll)。
- **検証結果** (rake build:linux + dev_run_check.sh):
  - `FMRB_KERNEL_ENGINE=spinel`: core ログに
    `spx: spinel hello` / `board_millis[0..2]=...` /
    `spinel hello done: polls=10 messages=0` を確認。**FFI 経由の Log/millis/poll
    が実 firmware のタスクとして動作** (基準 3 充足)。
  - `FMRB_KERNEL_ENGINE=mruby` (デフォルト): desktop が従来どおり描画
    (dev_run_check.sh スクリーンショットで確認、回帰なし。基準 2 充足)。

## T1-4 追記: ランタイム compile set の拡張

当初「最小 .c セット」を意図したが、`sp_runtime.h` の static ヘルパが多数の
モジュールを相互参照する (`sp_poly_inspect` → bigint/regexp、`sp_re_mark_globals`
→ fiber) ため、hello_kernel の文字列補間経由でそれらが到達コードから参照され
リンク不能だった。対応: **sp_net.c / sp_crypto.c (外部 OS 依存 = sockets/libcrypt) を
除く全モジュール + regexp/*.c を compile** し、`--gc-sections` で未使用関数を除去。
`import_from_fork.rb` は glob で自動選定 (net/crypto のみ除外)、CMakeLists も
`file(GLOB ...)` で同期。スナップショット 47 files (headers 25 + .c 22)。

## 受け入れ基準の充足

1. fork: `--no-main --entry` 実装 + スモークテスト、make test 1,991/1 維持、
   make bench 58/0、T1-1b の 2 修正 (U-1/U-3)。**充足**。
2. `FMRB_KERNEL_ENGINE=mruby` で従来と同一動作 (desktop 描画確認)。**充足**。
3. `FMRB_KERNEL_ENGINE=spinel` で hello_kernel がタスクとして動き、FFI 経由の
   Log/millis/poll を確認。**充足**。
4. fmrb_spx.h が Doxygen コメントで文書化。**充足**。
5. mruby バインディングのリファクタは Phase 2 に延期 (fmrb_spx は独立導入なので
   既存 mruby 機能に回帰なし。desktop 描画で確認)。**回帰なしを確認**。

→ **Phase 1 完了**。Phase 2 (カーネル VM の Spinel 化・制御反転 poll 化) へ。

## コミット規約メモ

- fork の Phase 1 追加コミット (d9c6dbf9 は既存、9aa7cdd6 / 5129af95) のうち
  5129af95 は `Co-Authored-By: Claude Opus 4.8 (1M context)` トレーラ付き
  (ユーザ許可)。9aa7cdd6 は許可前のため未付与。Phase 0 の 5 コミットも未付与。
  upstream PR 前に一括で揃える場合は rebase で対応可能。
