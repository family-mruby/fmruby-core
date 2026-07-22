# Phase 3 指示書: ランタイムのマルチインスタンス化 (Spinel fork 改修)

前提: Phase 1 完了 (fork とライブラリモードがある)。Phase 2 と並行可能。
作業はすべて **Spinel fork 内** で行う (fmruby-core は触らない。
fmruby-core への取り込みは Phase 4 の冒頭で spinel_rt を再 import する)。

## 目的

1 プロセス (firmware) 内で複数の Spinel コンパイル済みプログラム
(カーネル / desktop / shell) を、それぞれ独立したヒープ・GC で
同時実行できるようにする。各プログラムは別 OS スレッド
(FreeRTOS タスク / pthread) で動く。プログラム内部はシングルスレッド
(Spinel の Thread 機能は使わない) という前提を置いてよい。

## 背景 (調査済みの構造)

- ランタイムの可変グローバルは約 70 個で、`lib/sp_alloc.c` (ヒープ管理
  ~10 個)、`lib/sp_gc.c` (GC 状態 ~12 個) に集中。さらに約 25 個は
  スレッド版ビルド用に既に `SP_TLS` 注釈が付いている
  (sp_gc_roots / sp_re_* / sp_random / sp_fiber_current 等。
  `grep -rn "SP_TLS" lib/` で一覧できる)。
- 生成 C はランタイム状態を**シンボル名で**参照する
  (`sp_gc_nroots`、`sp_exc_top` 等)。したがって errno 方式
  (名前をマクロで `SP_CTX()->field` に差し替える) を使えば
  **codegen はほぼ無改修**で済む。
- 例外スタック等の一部状態は `sp_runtime.h` 内で `static` 定義されて
  おり、TU (=プログラム) ごとに複製される。これは多重化に有利なので
  維持する。ただし「ランタイム .c 側からも同じ状態を触っていないか」を
  必ず確認すること (静かな二重定義バグの温床)。
- 単一スレッドビルドでは `SP_TLS` は空定義 (`lib/sp_types.h:43` 付近)、
  `SP_HEAP_LOCK` は no-op。

## 設計

### モード構成

新コンパイルフラグ `SP_MULTI_CTX` を導入する。

- **未定義 (デフォルト)**: 完全に従来どおり。`SP_CTX()` は
  単一の静的インスタンス `&sp_ctx_default` を返す (実質ゼロコスト、
  コンパイラが定数畳み込みできる形にする)。upstream の全テスト・
  ベンチはこのモードで**バイト同等**であることを目標にする。
- **定義時**: `SP_CTX()` はカレントコンテキストポインタを返す。
  実装はプラットフォームフックにする:
  ```c
  /* sp_ctx.h */
  sp_ctx *sp_ctx_current(void);              /* platform-provided */
  void    sp_ctx_set_current(sp_ctx *ctx);
  #define SP_CTX() sp_ctx_current()
  ```
  参照実装 (POSIX): `static __thread sp_ctx *g_sp_ctx;`。
  ESP-IDF 実装 (Phase 5 で使用) は FreeRTOS の
  タスクローカルストレージポインタを使う予定であることをコメントに
  書いておく (実装自体は Phase 5)。

### sp_ctx 構造体

`lib/sp_ctx.h` を新設し、以下を集約する (フィールド名は既存グローバル名
から `sp_`/`sp_gc_` プレフィックスを外したもの):

1. アロケータ/ヒープ状態 (`sp_alloc.c` から):
   sp_str_heap, sp_str_heap_bytes, sp_str_threshold,
   sp_str_threshold_init, sp_gc_threshold, sp_gc_threshold_init,
   stress フラグ類, sp_str_lcache。
2. GC 状態 (`sp_gc.c` から): sp_gc_heap, sp_gc_bytes, sp_gc_old_bytes,
   sp_gc_cycle, sp_gc_old_heap, sp_gc_mark_stack/top,
   sp_gc_vsnap 系, sp_gc_max_bytes 系, sp_gc_dbg_ctx。
3. GC ルートスタック: `void ***roots;`(動的確保) + `int nroots;` +
   `int roots_cap;`。従来の固定配列
   `sp_gc_roots[SP_GC_STACK_MAX]` は default モードでは互換維持
   (静的配列を ctx が指す) し、`SP_MULTI_CTX` では
   `sp_instance_create` の config でサイズ指定して heap 確保。
4. マーク/スイープのフック・登録テーブル類
   (`__attribute__((constructor))` で登録している hook 配列。
   sp_alloc.c:153 / sp_gc.c:83 付近を確認)。constructor 登録は
   プロセス全体で 1 回なので、hook テーブル自体はグローバル共有で
   よいか、ctx ごとに持つべきかを実物を読んで判断する
   (hook が状態を持たない関数ポインタ集合ならグローバル共有で可)。
5. SP_TLS 群のうちプログラム状態であるもの: 正規表現マッチ状態
   (sp_re_*)、乱数状態 (sp_random / sp_krand)、
   sp_pending_exc 系。fiber / sched 系は対象外
   (組み込みでは使わない。SP_MULTI_CTX と SP_THREADS の併用は
   「未サポート」と #error にしてよい)。
6. 環境系のプロセスグローバル (ENV、ARGV、stdout バッファリング等) は
   共有のままでよい。ただし `$stdout` のような Ruby グローバル変数の
   実体が生成 TU 側 static か runtime 側かを確認し、runtime 側なら
   ctx へ移す。

### 名前互換マクロ (errno 方式)

`sp_ctx.h` の末尾で、既存グローバル名を ctx フィールドへマップする:

```c
#define sp_gc_nroots   (SP_CTX()->gc_nroots)
#define sp_gc_heap     (SP_CTX()->gc_heap)
/* ... */
```

- これにより lib/*.c と生成 C の参照箇所は無改修で新レイアウトを使う。
- 変数の**定義**そのもの (sp_gc.c:20 等) は削除し、`sp_ctx_default` の
  初期化へ移す。マクロと同名の定義が残るとコンパイルエラーになるので
  網羅的に移すこと。
- ヘッダの include 順序に注意: `sp_runtime.h` 等がグローバルを
  extern 宣言している箇所はマクロ定義より前に消す/置換する。

### インスタンス API

```c
typedef struct {
  size_t gc_threshold;       /* 0 = default (256KB) */
  size_t str_threshold;      /* 0 = default */
  int    root_stack_entries; /* 0 = default (SP_GC_STACK_MAX) */
  void *(*alloc)(size_t);    /* NULL = calloc-based default */
  void  (*dealloc)(void *);
} sp_instance_config;

sp_ctx *sp_instance_create(const sp_instance_config *cfg);
void    sp_instance_destroy(sp_ctx *ctx);  /* frees all heaps/roots */
```

- alloc/dealloc フックはこの Phase で配線まで行う (calloc/free 直呼びを
  ctx 経由に変える)。ESP32 の heap_caps/PSRAM 対応 (Phase 5) の布石。
  変更箇所: sp_alloc.c の calloc/free、sp_gc.c の free、
  文字列ヒープの確保箇所 (sp_str_alloc 系)、mark stack / vsnap の
  realloc 類。**性能に効く箇所なので、default モードでは間接呼び出しを
  経ない** (直呼びのまま) ようマクロで分岐する。
- destroy はカーネルでは使わないが、テストのリーク検出に必要。

## タスク

### T3-1: グローバル状態の完全棚卸し (半日)

1. `lib/*.c lib/*.h` から可変ファイルスコープ変数を機械的に列挙
   (const / 初期化後読み取り専用は除外を明記)。
2. 各変数に分類タグを付けた表を作る:
   ctx へ移す / TU-static で既に隔離 / プロセス共有のままで良い /
   対象外 (fiber, sched, net 等の非対象メンバ)。
3. `sp_str.c` の `sp_char_cache` のような init-once 読み取りキャッシュは
   「プロセス共有 + 初期化の競合だけ対策 (idempotent な初期化なら
   そのままで可)」と判断してよい。判断根拠を表に書く。
4. この表を fork 内 `docs/internals/multi-instance.md` として追加
   (upstream PR の設計文書を兼ねる。英語で書く)。

### T3-2: sp_ctx 導入 (default モード無変更) (2-3 日)

1. sp_ctx.h / sp_ctx.c を追加し、T3-1 の表に従い状態を移す。
2. 互換マクロを整備。`make` が通り、**default モードで**
   `make test` 1,744 本全パス、`make bench` 劣化 2% 以内。
3. 生成 C の差分確認: 適当なベンチを `-S` で before/after 出力し、
   生成コード自体が変わっていないことを確認 (codegen 無改修の検証)。

### T3-3: SP_MULTI_CTX モード実装 (1-2 日)

1. `sp_ctx_current` の __thread 実装、`sp_instance_create/destroy`。
2. ライブラリモード (`--no-main`) との組み合わせ: entry 呼び出し前に
   ホストが `sp_ctx_set_current(sp_instance_create(&cfg))` を行う契約に
   する。entry 側 (生成 C) には変更を入れない。
   この契約を fork の docs (multi-instance.md) に書く。
3. ランタイムを `-DSP_MULTI_CTX` でビルドした
   `libspinel_rt_mc.a` を Makefile に追加。

### T3-4: マルチインスタンステスト (1-2 日)

fork 内に `test/multi_instance/` を新設 (upstream のテスト流儀に
合わない場合は Makefile ターゲット `make test-mc` として独立させる)。

1. 素材: 小さな Ruby プログラム 2-3 本
   (例: A = 文字列連結と Hash を GC が数回走る量だけ回して集計を返す、
   B = 例外を投げて rescue するループ、C = 配列ソートループ)。
   それぞれ `--no-main --entry prog_a_entry` 等でコンパイル。
2. ホスト C テスト:
   - pthread を 3 本立て、各スレッドで instance create → entry を
     N 回呼ぶ → destroy。
   - 検証項目: 各プログラムの出力/戻り値が単独実行時と一致、
     ctx 間でヒープ統計が独立 (A のアロケートが B の gc_bytes に
     影響しない)、destroy 後にメモリリークなし
     (可能なら AddressSanitizer / valgrind で確認)、
     並行実行でクラッシュしない (GC は各インスタンス内で独立に走る。
     ロック不要であることの確認)。
   - 同一プログラムを 2 インスタンスで同時実行するケースも入れる
     (生成 TU の static 変数が共有されるため、**プログラム単位の
     多重起動は不可**という制約が成立するかを確認する。不可なら
     その制約を docs に明記する。fmruby の用途ではプログラムは
     kernel/desktop/shell で各 1 インスタンスなので制約があっても
     問題ない)。
3. root スタックサイズを 64 のような極小値にして溢れさせ、
   溢れ時の挙動 (現状 `_sp_gc_root_push` が 0 を返す) がクラッシュで
   なくエラーになることを確認。必要なら abort + メッセージに改善。

### T3-5: 仕上げ (半日)

1. default モードの最終回帰: `make test` / `make bench` /
   `make optcarrot` (存在すれば)。
2. コミット整理: (1) 状態棚卸し docs、(2) sp_ctx 導入 (無挙動変化)、
   (3) SP_MULTI_CTX、(4) テスト、の順の PR 可能な粒度に。
3. objcopy フォールバックの手順検証 (保険):
   `objcopy --prefix-symbols=k_ libspinel_rt.a` 方式で 2 プログラムを
   リンクできるかを 1 時間だけ試し、可否をレポートに記録
   (本命が遅延したとき Phase 4 を先行させる判断材料)。

## 受け入れ基準

1. default ビルドで upstream テスト 1,744 本全パス、bench 劣化 2% 以内、
   生成 C 無変化。
2. SP_MULTI_CTX で multi_instance テストが ASan (または valgrind)
   クリーンで全パス。3 スレッド並行で 1000 回反復しても安定。
3. 設計文書 (multi-instance.md) が fork に入っている。
4. コミットが PR 可能な粒度に整理されている。

## 落とし穴・注意

- `SP_CTX()` がアロケーション高速パス (sp_runtime.h の inline) に
  入るため、default モードでの間接化コストをゼロに保つ実装
  (定数アドレス) が最重要。bench で必ず確認。
- 互換マクロ名が lib 内のローカル変数名・構造体メンバ名と衝突すると
  発見しづらいバグになる。`sp_gc_nroots` 等をマクロ化したら
  全ファイルを -Werror でビルドし、shadow 警告 (-Wshadow) も
  一時的に有効にして確認する。
- `__attribute__((constructor))` の初期化は「プロセスで 1 回」。
  ctx ごとの初期化と混同しないこと。ctx 初期化は
  sp_instance_create に集約する。
- SP_THREADS との併用は考えない (#error)。ただし SP_THREADS ビルドを
  壊さないこと (make の両アーカイブがビルドできること)。

## 完了レポート

`doc/spinel_aot/reports/phase3_report.md`:
- 状態棚卸し表の要約 (ctx へ移した数 / 共有のまま / TU-static)
- default モードの test/bench 結果 (upstream 比)
- マルチインスタンステストの内容と結果
- 「同一プログラムの多重インスタンス」の可否と制約
- objcopy フォールバックの検証結果
- upstream PR 候補コミットの一覧
