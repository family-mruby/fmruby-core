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

## 残タスク (Phase 2 への布石)

- T1-5: FFI シム層 `main/kernel/fmrb_spx_kernel.c` + `fmrb_spx.h`。kernel.c の
  mrb バインディングが呼ぶ下位 C 関数を調査し、mrb 非依存の C ABI (バイト列渡し、
  poly を境界に持ち込まない) を定義。
- T1-6: Ruby FFI 宣言 (`fmrb_ffi.rb`) + `hello_kernel.rb` + `compile_ruby_to_spinel.cmake`
  + `FMRB_KERNEL_ENGINE` 切替 + FreeRTOS タスク疎通。docker + dev_run_check.sh で確認。

## コミット規約メモ

- fork の Phase 1 追加コミット (d9c6dbf9 は既存、9aa7cdd6 / 5129af95) のうち
  5129af95 は `Co-Authored-By: Claude Opus 4.8 (1M context)` トレーラ付き
  (ユーザ許可)。9aa7cdd6 は許可前のため未付与。Phase 0 の 5 コミットも未付与。
  upstream PR 前に一括で揃える場合は rebase で対応可能。
