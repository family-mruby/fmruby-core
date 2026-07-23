# Phase 3 + 3.5 完了レポート: ランタイムのマルチインスタンス化

作業ブランチ: Spinel fork `fmrb-dev` (tip `7063b350`)。fmruby-core は変更なし
(Phase 4 冒頭で spinel_rt を再取り込みする方式)。

## サマリ

- Phase 3: ランタイムの可変グローバル状態を per-instance の `sp_ctx` に移し、
  `-DSP_MULTI_CTX` で 1 プロセス内に複数の Spinel コンパイル済みプログラムを
  独立ヒープ + 独立 GC で同時実行できるようにした。Linux で **live 実行 +
  マルチインスタンス独立性を実証**。
- Phase 3.5: メモリ確保 (`malloc/calloc/realloc/free/strdup`) を per-instance
  バックエンドへ完全に振り分け、インスタンスごとのメモリ隔離 (ESP32 の
  estalloc プール前提) を成立させた。呼び出し箇所は書き換えず、マクロ差し替えを
  全 mc TU へ強制注入する方式。
- default ビルドは全工程で回帰なし。全コミットは汎用 (upstream PR 候補)。

## Phase 3: 状態の per-instance 化

### 棚卸しと設計

- 移設対象は「ライブラリ側 (libspinel_rt.a に一度だけコンパイルされ全 TU で
  共有される) の可変グローバル」に限られることを確認。生成 TU 側の static
  (例外/catch スタック、GC バケット、argv キャッシュ等) は既に per-TU で隔離済み。
- `sp_ctx` 構造体 + `SP_CTX()` (thread-local な現在インスタンスポインタ) を導入。
  default ビルドでは元の名前が元のグローバルに解決 (byte 同一、ホットパス無変更)、
  `SP_MULTI_CTX` 時のみ元の名前が ctx フィールドへのマクロになる (errno 方式、
  codegen / 生成 C は無改修)。

### 移設した状態

- 文字列アロケータ / オブジェクト GC の閾値・ヒープ・ルートスタック・mark
  スクラッチ・verify スナップショット・program フック (`sp_alloc.c` / `sp_gc.c`)
- regexp の last-match 状態 (`$~`)、RNG 状態
- value-introspection の vtable 12 本 (シンボル名 / JSON / poly-inspect /
  object-inspect 等。生成 TU init が program ごとに設定するため per-instance 必須)
- `SP_MULTI_CTX` + `SP_THREADS` は `#error` (相互排他)。fiber / scheduler は
  マルチインスタンス対象外。`sp_marshal_v` (by-value vtable) は共有のまま残置
  (Marshal 同時利用は稀。将来 follow-up)。

### constructor トラップ (live 化の要)

生成 TU は per-program フック (GC globals-mark、JSON/poly vtable、文字列 sweep)
をプロセス constructor で設定していた。`SP_MULTI_CTX` ではこれらが per-instance
の ctx フィールドであり、プロセス constructor 時点では現在インスタンスが未設定
(`SP_CTX()` が NULL) のため main 前に NULL 参照でクラッシュした。

修正: フック設定を constructor からプログラム entry へ移す。

- `SP_TU_CTOR` マクロ (default = `__attribute__((constructor))`、MC = 空)。
- `sp_runtime.h` に `sp_tu_ctx_init()` (MC のみ) を追加し、両 installer を呼ぶ。
- codegen は entry 先頭で `#ifdef SP_MULTI_CTX sp_tu_ctx_init(); #endif` を
  `sp_re_init()` の前に emit (ランタイム既定を先に入れ、sp_re_init が
  symbol/regex/user-globals の override を上乗せ)。
- 文字列 sweep フックと `SPINEL_GC_VERIFY` は `sp_instance_create` で設定/読取。

すべて default ビルドではガードで消えるため byte 同一。

### live 実行 + 独立性の実証

`test/multi_ctx/smoke.sh` (`make test-multi-ctx`):

1. 単一インスタンスの出力が通常コンパイル (`-E`) と byte 一致。
2. N スレッド並行で各自インスタンスの alloc/GC 律速プログラムが全員正答
   (独立ヒープ、cross-instance 破壊なし)。
3. 同構成を AddressSanitizer 下で実行 = clean (並行 alloc/GC/sweep でメモリ
   エラーなし)。

## Phase 3.5: メモリ確保の完全フック化

### 方式: マクロ差し替えの強制注入

約 490 箇所の呼び出し置換はリスクが高く upstream 追従とも衝突するため採らず、
libc 名を per-instance ラッパへマクロで差し替える方式を採用 (グローバルの
errno 方式と同種)。

- `lib/sp_mem_override.h` (新設): `stdlib.h`/`string.h` を通した後、
  `sp_mem_malloc/calloc/realloc/free/strdup` を宣言し、libc 名を関数形式マクロで
  それらへ差し替える。
- mc ビルドの CFLAGS (`MC_DEF`) に `-include lib/sp_mem_override.h` を追加する
  だけで、`lib/*.c` / `regexp/*.c` / `sp_runtime.h` の inline / **生成プログラム
  TU** がすべて自動でカバーされる。呼び出し箇所も codegen も無改修。default
  ビルドには一切注入されず byte 同一。
- 実装本体は `sp_ctx.c` の 1 箇所。冒頭で `#undef` して本物の libc に到達し、
  現在インスタンスのバックエンド (未設定時は libc) へ振り分ける。バックエンドは
  zero-fill 契約なので `malloc` と `calloc` を 1 フックに集約。枯渇時は
  `sp_oom_die` (メッセージ + `exit`)。
- `malloc_trim` は `SP_MULTI_CTX` で no-op。
- 境界 (フック対象外): stdio 内部バッファ (`fopen`/`printf`/`getline` 等) は
  libc 内部でシステムヒープに確保。関数ポインタとしての `free` 参照 (現状皆無)
  も差し替わらないが、nm ゲートが検出する。

### nm ゲート

`make check-mc-syms` (`test/multi_ctx/check_syms.sh`): `libspinel_rt_mc.a` の
全メンバと、新規にビルドした生成プログラム TU が、`malloc/calloc/realloc/free/
strdup` (+ `reallocarray`/`posix_memalign`/`aligned_alloc`) への未定義参照を
一切持たないことを検証。定義元の `sp_ctx.o` のみ libc を参照してよい。`-include`
を落とした TU (ビルド系統の退行) を機械検出する。`test-multi-ctx` の前段に配線。

### estalloc によるマルチインスタンステスト

`test/multi_ctx/estalloc.sh`。バックエンドに estalloc (TLSF、`test/multi_ctx/
estalloc/` に BSD-3 で vendored、ランタイム本体には含めない) を用い、各インス
タンスに固有の固定長プールを与える。libc 以外のアロケータで走らせることで、
alloc/free の取り違えや cross-instance 混在が即座に露見する。

- S1 プール隔離 + 統計独立: 2 インスタンスを別プールで作り、片方だけプログラムを
  実行。実行側のプール使用量 > アイドル側、かつ実行側を destroy してもアイドル側の
  プールは不変 (ヒープが真に分離)。
- S2 並行: 3 スレッド x 20 反復、各自プールで全員成功。
- S3 プール枯渇: プログラムに対し過小なプール → バックエンドが NULL → `sp_oom_die`
  で `exit(1)` (静かな NULL 参照にしない)。
- S4 ルートスタック溢れ: `root_stack_entries` を極小 (64) にした deep 再帰 →
  `sp_gc_root_overflow_die` (メッセージ + `abort`)。溢れ時にルートを黙って落として
  後で use-after-free する挙動を、決定的な abort に改善 (この改善は `SP_MULTI_CTX`
  限定。default は従来の return-0 を維持し byte 同一)。
- S1-S3 は AddressSanitizer + リーク検出下でも実行し clean (全確保が静的プール内
  のため、clean は no-leak / no-corruption の証左)。

## 回帰と性能

- default: `make test` = **1991 pass / 1 fail** (既知 cosmetic `nilclass_bool_ops_
  conversions` のみ、ベース維持)、`make bench` = **58 pass / 0 fail**。生成 C は
  codegen 無改修のため byte 同一。
- `SP_THREADS` (mt) アーカイブビルド = clean (マルチインスタンス変更はすべて
  `SP_MULTI_CTX` ガード下で mt には影響しない)。
- mc アロケーション overhead: 確保飽和のマイクロベンチ (純アロケーションループ)
  で default 0.30s → mc 0.32s = 約 6-7%。これは per-instance 間接呼び出し +
  zero-fill を含む最悪ケースで、実ワークロードではより小さい見込み。5% を恒常的に
  超える実ケースが出た場合は raw (非 zero) フックの追加を検討する (config 追加に
  なるため安易に増やさない)。専用の mc ベンチハーネスは未整備 (default bench は
  override 非注入で不変)。

## マルチインスタンスの現状能力

- 別プログラムを 1 プロセスで複数、独立ヒープ + 独立 GC で同時実行できる
  (スレッドごとに `sp_ctx_set_current(sp_instance_create(...))` → entry)。
- 同一プログラムの多重起動: 生成 TU が 1 つなら問題ない。ただし異なる生成 TU 2 つは
  同一バイナリにリンク不可 (各 TV がランタイムの非 static シンボルを emit するため)
  = プログラムごとに別バイナリ。kernel/desktop/shell はそれぞれ 1 インスタンスなので
  実運用上の制約にはならない。
- teardown: `sp_instance_destroy` は arena (ctx / ルートスタック / mark スクラッチ /
  verify スナップショット) を解放する。生存 GC オブジェクトを個別に破棄はしない
  (プールバックエンドではプール全体を回収するため、これで leak-free)。

## upstream PR 候補コミット (fmrb-dev)

すべて汎用 (fmruby 固有なし)。棚卸し docs → sp_ctx 移設 → live 化 → allocation
override → テスト、の順で PR 可能な粒度。

```
93b69815 docs: multi-instance runtime design + global-state inventory
1fea726d runtime: sp_ctx scaffolding + relocate alloc/GC state
8465dc11 runtime: relocate regexp + RNG state into sp_ctx
876c0a5f runtime: relocate value-introspection vtable into sp_ctx
0fa1a475 runtime: make SP_MULTI_CTX live -- per-instance TU hook init + test
3394a85c docs: per-instance TU init replaces process constructors
e7682c2f build: gitignore the on-demand libspinel_rt_mc.a archive
63075453 runtime: route allocation through the instance backend + nm gate
8e2f553f test: multi-instance on a custom allocator (estalloc) + root overflow
7063b350 docs: allocation override method, nm gate, test harnesses
```

## Phase 3 判定

ランタイムのマルチインスタンス化は **機能完了**。Linux で複数インスタンスの
独立ヒープ + 独立 GC 同時実行、per-instance バックエンドへの完全なメモリ振り分け
(nm ゲートで担保)、カスタムアロケータ (estalloc) でのプール隔離・並行・枯渇・
ルート溢れを実証。残るは ESP32 実機での estalloc 配線 (Phase 4 冒頭の取り込み
以降) と、必要に応じた raw フック / `sp_marshal_v` / `sp_str_lcache` の follow-up。
