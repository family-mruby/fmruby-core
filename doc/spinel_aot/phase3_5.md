# Phase 3.5 指示書: アロケーション完全フック化 (マクロ差し替え方式)

前提: Phase 3 の sp_ctx 化は fork (`tmp/spinel` branch `fmrb-dev`,
tip `e7682c2f` 時点) で「状態の分離」まで完了している:

- T3-1: 棚卸し + 設計文書 `docs/internals/multi-instance.md`
- T3-2/T3-3: sp_ctx 移設 (alloc/GC 状態、regexp/RNG、vtable)、
  SP_MULTI_CTX 有効化、per-instance TU init (constructor トラップ解決)、
  `sp_instance_config` (mem_ud + alloc/realloc/dealloc フック) と
  `sp_mem_*` ラッパ宣言、`lib/libspinel_rt_mc.a` ビルド
- T3-4 (一部): `test/multi_ctx/smoke.sh` (`make test-multi-ctx`、ASan クリーン)

**未実施 = 本 Phase の範囲**: メモリの分離。sp_mem_* を使っているのは
sp_ctx.c のみで、ランタイム全域 (~490 箇所: sp_runtime.h の inline 161、
sp_str.c 77、regexp 53 ほか) は libc malloc/calloc/realloc/free/strdup
直呼びのまま。このため SP_MULTI_CTX でも全インスタンスが libc ヒープを
共有しており、estalloc プール分離 (ESP32 の大前提) がまだ成立しない。

作業はすべて **Spinel fork 内** (fmruby-core は触らない。00_common.md の
fork 規約に従う)。

## 方式: 呼び出し箇所の置換はしない (ユーザ決定 2026-07-23)

~490 箇所の機械的置換は打ち間違い・見落とし・upstream 追従衝突のリスクが
大きいため採らない。代わりに **errno 方式 (Phase 3 のグローバル変数と同じ)
をアロケーションに適用**する:

### sp_mem_override.h (新設、根っこ 1 箇所)

```c
/* SP_MULTI_CTX ビルドの全 TU に -include で強制注入される。
   default ビルドでは一切 include されない (ソース無変更・byte 同一)。 */
#include <stdlib.h>      /* 本物の宣言を先に通す */
#include <string.h>
void *sp_mem_malloc(size_t);
void *sp_mem_calloc(size_t, size_t);
void *sp_mem_realloc(void *, size_t);
void  sp_mem_free(void *);
char *sp_mem_strdup(const char *);

#define malloc(n)    sp_mem_malloc(n)
#define calloc(a,b)  sp_mem_calloc(a,b)
#define realloc(p,n) sp_mem_realloc(p,n)
#define free(p)      sp_mem_free(p)
#define strdup(s)    sp_mem_strdup(s)
```

- 注入は Makefile の mc 用 CFLAGS に `-include lib/sp_mem_override.h` を
  足すだけ。lib/*.c・regexp/*.c・sp_runtime.h の inline・**生成 C の TU**
  (sp_runtime.h を include するため) がすべて自動でカバーされる。
  codegen は無改修。
- 実装本体は sp_ctx.c に置き、ファイル冒頭で `#undef` して
  `SP_CTX()->mem_alloc` 系 (フック未設定時は libc) を呼ぶ。
- 事前スキャン済みの安全性: lib/ に `->free(` / `.free(` のメンバ呼び出し
  なし、`free` を識別子に使う箇所なし。`__attribute__((malloc))` は
  直後が `(` でないため展開されない (関数形式マクロの性質)。

### フック意味論

- `sp_mem_calloc` はフック 1 本 (zero-fill 保証の alloc、estalloc では
  est_calloc) に寄せる。`sp_mem_malloc` も同フックで賄い、zero-fill の
  余剰コストは bench で確認する。**劣化が出た場合のみ** raw (非 zero)
  フックの追加を検討 (config 変更になるので安易に増やさない)。
- `sp_mem_realloc(NULL, n)` = malloc、`sp_mem_free(NULL)` = no-op を保証
  (libc 同等)。
- `malloc_trim` (sp_gc.c) は SP_MULTI_CTX では no-op (既存 Darwin 分岐と
  同様のガード)。
- **境界 (対象外) を明文化**: stdio 内部バッファ (fopen/printf/getline 等
  ~11 呼び出し箇所の libc 内部確保) はフック対象外。phase3.md の
  「stdout バッファリング等はプロセス共有」決定と整合し、ESP32 では
  newlib 経由でシステムヒープに行く。multi-instance.md にこの線引きを
  記載する。getenv 等の非アロケーション libc も対象外。

## タスク

### T3.5-1: sp_mem_override.h + sp_mem_* 実装 (半日-1 日)

1. 上記ヘッダを追加し、mc ビルド (libspinel_rt_mc.a と
   test-multi-ctx の生成 C コンパイル) に `-include` を配線。
2. sp_ctx.c に sp_mem_malloc/calloc/realloc/free/strdup を実装
   (フック経由、未設定時 libc)。
3. default ビルド (`make` / `make test` / `make bench`) が**完全無変更**で
   あることを確認 (override はどの TU にも入らないので構造的に自明だが、
   生成 C の `-S` diff で機械確認する)。

### T3.5-2: nm ゲート (半日)

1. Makefile ターゲット (例 `check-mc-syms`) を追加:
   `libspinel_rt_mc.a` と test-multi-ctx の生成オブジェクトに
   malloc/calloc/realloc/free/strdup (+念のため reallocarray /
   posix_memalign / aligned_alloc) への**未定義シンボル参照が無い**ことを
   nm で検証。sp_ctx.o だけが libc を参照してよい (実装本体)。
2. `make test-multi-ctx` の依存に入れて常時実行にする。
   -include が効いていない TU の混入 (ビルド系統の退行) を機械検出する。

### T3.5-3: multi-instance テスト完成 (1 日)

phase3.md T3-4 の残りを実施:

1. `test/multi_ctx/` に estalloc ケースを追加: estalloc (BSD-3, 2 ファイル)
   を test 配下に vendored copy し、固定長バッファに est_init した
   `ESTALLOC*` を mem_ud + フックで渡す。全確保がプール内で賄われ、
   インスタンスごとの est 統計 (used/free) が独立していることを検証。
2. pthread 3 本 x N 反復 (同時実行安定性)、destroy 後リークなし (ASan)、
   ヒープ統計の独立性 (A のアロケートが B に影響しない)。
3. root スタック極小 (64) での溢れ挙動: クラッシュではなくエラーに
   なることを確認 (必要なら abort + メッセージへ改善)。
4. プール枯渇ケース: est が NULL を返したとき sp_oom_die 経路で
   メッセージを出して停止することを確認 (静かな NULL 参照にしない)。

### T3.5-4: 仕上げ + レポート (半日)

1. default モード最終回帰: `make test` / `make bench` (劣化 2% 以内) /
   `make gate` (存在すれば)。SP_THREADS ビルドが壊れていないことも確認。
2. multi-instance.md に本方式 (override ヘッダ、stdio 境界、フック契約) を
   追記。
3. `doc/spinel_aot/reports/phase3_report.md` を作成し、**Phase 3 + 3.5 を
   まとめて**完了レポート化: 棚卸し要約 / default 回帰数値 (upstream 比) /
   マルチインスタンステスト内容と結果 / 同一プログラム多重起動の可否 /
   stdio 境界の明文化 / upstream PR 候補コミット一覧の更新。
4. コミットは「(1) override + 実装、(2) nm ゲート、(3) テスト、(4) docs」
   の PR 可能な粒度で。fmrb-dev へ push。

## 受け入れ基準

1. default ビルド: upstream テスト全パス、bench 劣化 2% 以内、
   生成 C byte 同一 (Phase 3 からの継続基準)。
2. SP_MULTI_CTX: nm ゲート合格 (libc アロケーション参照は sp_ctx.o のみ)、
   estalloc ケースを含む multi_ctx テストが ASan クリーンで全パス、
   3 スレッド並行反復で安定。
3. mc ビルドの bench 相当で zero-fill 化による顕著な劣化がないこと
   (目安 5% 以内。超える場合は raw フック追加の判断材料としてレポートに
   数値を残す)。
4. phase3_report.md が存在し、Phase 3 全体の受け入れ基準を網羅している。

## 落とし穴・注意

- override ヘッダは**必ず stdlib.h を先に include** してからマクロ定義する
  (システムヘッダの宣言をマクロで壊さない)。マクロ定義後に stdlib.h が
  再 include されてもガードで無害。
- 関数形式マクロは `名前(` の並びだけ展開する。関数ポインタとしての
  `free` (呼び出しでない参照) は展開されず libc のままになるので、
  もし将来そういうコードが現れたら nm ゲートが検出する (現状は無い)。
- sp_ctx.c 以外で `#undef sp_mem 系` をしない。実装本体は 1 ファイルに
  閉じる。
- フック確保と libc 確保の free 混在はクラッシュ源。境界 (stdio) の
  ポインタがフック free に流れ込まないことをテストで踏む
  (sp_io の File 系は fmruby カーネルでは未使用だが upstream テストでは
  使われる — mc モードでのテスト実行時に確認)。
- tmp/spinel では本指示書時点で Phase 3 実装 AI の作業履歴がある。
  着手前に `git log` で fmrb-dev tip を確認し、この指示書の前提
  (`e7682c2f`) から進んでいる場合は差分を読んでから作業する。

## 完了後の fmruby-core 側 (本 Phase ではやらない)

fmrb-dev を push → `components/fmrb_spinel_rt/SPINEL_PIN` の commit 更新 →
`import_from_fork.rb` 再実行 (sp_ctx.c/sp_mem_override.h が snapshot に
入る) → Phase 4 冒頭の取り込み手順へ。この 3 点セットは Phase 4 側の
作業として phase4.md / 00_common.md に記載済み。
