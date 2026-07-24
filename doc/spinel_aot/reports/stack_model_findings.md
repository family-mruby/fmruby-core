# Spinel AOT スタックモデル調査 (Phase 4 事前調査)

調査日: 2026-07-24
対象: vendor/spinel `5129af95009e` (SPINEL_PIN fmrb-dev `184ff369` 系)
動機: desktop の Spinel 化検討にあたり、「ヒープは PSRAM で余裕だが内部 RAM の
RTOS タスクスタックが貴重」という ESP32 の制約下で、AOT 化がタスクスタック使用量を
増やさないかを確認する。

## 結論

Spinel は **Ruby の呼び出し深さをそのままネイティブ (C / RTOS タスク) スタックに写す**。
Ruby メソッド = C 関数、Ruby ローカル = C スタック上の変数、Ruby 再帰 = C 再帰。
値を明示 VM スタックに逃がさない。したがって:

- **desktop の RAM 削減効果はヒープ側 (PSRAM 可) に限られ、内部 RAM のタスク
  スタックはむしろ増える方向**。ヒープ減=得、と単純化してはいけない。
- **深い再帰の故障モードが質的に劣化する** (回復可能な Ruby 例外 → ハードクラッシュ)。

## 根拠1: 呼び出し規約 (生成コード実測)

`def fib(n); a=fib(n-1); b=fib(n-2); a+b; end` の生成 C:

```c
static mrb_int sp_fib(mrb_int lv_n) {
    SP_GC_SAVE();
    mrb_int lv_a = 0;              /* Ruby local a -> C stack */
    mrb_int lv_b = 0;              /* Ruby local b -> C stack */
    if ((lv_n < 2LL)) return lv_n;
    lv_a = sp_fib(sp_int_sub(lv_n, 1LL));   /* Ruby recursion -> C recursion */
    lv_b = sp_fib(sp_int_sub(lv_n, 2LL));
    return sp_int_add(lv_a, lv_b);
}
```

オブジェクトローカルの場合も C スタックに置かれ、そのアドレスを GC ルート配列へ登録する:

```c
static const char * sp_build(mrb_int lv_n) {
    SP_GC_SAVE();
    const char * lv_s = ...;
    SP_GC_ROOT(lv_s);             /* &lv_s (C stack addr) を sp_gc_roots に push */
    lv_s = sp_build(sp_int_sub(lv_n, 1LL));
    return sp_str_plus(lv_s, ...);
}
```

`sp_gc_roots` (lib/sp_gc.h) は値スタックではなく、**C スタック上ローカルのアドレスを
積む影ルートスタック**。値の実体は常に C スタックに存在する。mruby のように呼び出し
フレームをヒープ (`mrb_context` / callinfo) に積む構造ではない。

## 根拠2: C スタック深度ガードが無い (故障モード)

`lib/sp_runtime.h:6289` のコメント: 「depth is bounded by the C stack, like ordinary
recursion」。C スタック本体の深度チェックは入っていない。runtime に `SystemStackError`
の文字列はあるが、それは `sp_brk_stack` 等の**固定補助配列 (深さ 64)** のガード用で
あり、C スタックは守らない。

実測 (深い再帰を小スタックで実行):

```
def rec(n); return 0 if n<=0; 1 + rec(n-1); end
rec(2_000_000) を `ulimit -s 512`(KB) で実行 → Segmentation fault (rc=139)
```

- mruby: 深い再帰は回復可能な Ruby 例外 (SystemStackError 相当) で停止
- Spinel: RTOS タスクスタックを踏み抜いて **SIGSEGV / メモリ破壊**

自明な leaf 関数は -O2 で最適化され per-frame バイト数は小さく出るため、フレームあたりの
実コストは**代表 desktop コード + ESP32 実トゥールチェーンで測る**必要がある。ここで
確定したのは「深さが 1:1 で写る」という定性。

## 根拠3: 内部 RAM を圧迫する副次要因

### (A) GC ルート配列 (静的 DRAM)

`sp_gc_roots[SP_GC_STACK_MAX]` は静的配列。lib/sp_gc.h のコメントいわく
「デフォルト 65536 = 512KB の静的バッファで minimal binary で最大の静的確保」。
fmruby は既に `SP_GC_STACK_MAX=8192`(=64KB) に縮小済
(components/fmrb_spinel_rt/CMakeLists.txt:15)。`SP_TLS` = per-worker のため、
**SP_MULTI_CTX で per-ctx になるなら常駐インスタンス数分だけ内部 DRAM に乗る**
(要確認: SP_MULTI_CTX 下で per-ctx か共有か)。

### (B) Fiber (mmap 64KB / 本、ESP32 backend 無し)

`lib/sp_fiber.c`: `SP_FIBER_STACK_SIZE = 64*1024`。`sp_Fiber_new` は fiber ごとに
64KB + ガードページを **mmap** する。context switch backend は x86-64 / aarch64 の asm
または POSIX ucontext のみ (lib/sp_fiber_ctx.h) で、**Xtensa (ESP32-S3) / FreeRTOS
向けが無い**。desktop/kernel が Fiber を使う場合、Phase 5 で (1) Xtensa/FreeRTOS への
移植、(2) fiber スタック 64KB×本数分の内部 RAM、の両方が必要。mruby では fiber
スタックはヒープ (PSRAM 可) だったため、これも「PSRAM → 内部 RAM」の移動になる。

## 内部 RAM 圧迫への対策 (調査で判明)

### (A) GC ルート配列 → estalloc/PSRAM は既に実現済み

Phase 3/3.5 のマルチインスタンス化で、静的配列からインスタンスヒープへ既に移されている:

- `sp_ctx.h:73` `void ***gc_roots; /* default: static array; MC: heap */`
- `sp_ctx.c:132-135` `cfg->alloc(cfg->mem_ud, rn*sizeof(void**))` でヒープ確保。
  サイズは `cfg->root_stack_entries ?: SP_GC_STACK_MAX`。
- `sp_ctx.h:213` `#define sp_gc_roots (SP_CTX()->gc_roots)`。
- `sp_gc.c:26` 静的 64KB 配列は `#ifndef SP_MULTI_CTX` で MC ビルドでは消える。

estalloc 配線も済み:
- `fmrb_spinel_host.c:31-33` `cfg.alloc=est_alloc_hook (est_calloc)`, `cfg.mem_ud=est`。
- `fmrb_kernel.c:527,543` est は `fmrb_get_mempool_ptr(mempool_id)` 上に `est_init`。
- `fmrb_mempool.c:9-16` 各タスク mempool は `EXT_RAM_BSS_ATTR` = **PSRAM**。

→ 現 MC ビルドでルート配列 (8192*8=64KB) は既に PSRAM プールから est_calloc 確保され、
内部 DRAM の静的配列は無い。追加調整: (1) `cfg.root_stack_entries` を desktop 実測ピーク
`nroots` に右サイズ化 (現状 0=8192)。(2) 性能: root push/pop は最ホット級操作で PSRAM 上に
なるため T4-5 で ESP32 実測。律速なら「ヒープは PSRAM / ルート配列は内部 RAM」に分離する
別 alloc を fork へ起案 (今は alloc 一本)。

### (B) Fiber 禁止で 64KB/本の mmap を回避できる

- root fiber は静的 struct で mmap しない (sp_fiber.c:204,219)。メイン処理は実タスク
  スタック上。追加コスト無し。
- 64KB mmap は明示 `Fiber.new` または fiber-backed Enumerator
  (`Enumerator.new{|y|...}`, `.next` 外部反復; sp_enum.h:4-5) のときだけ。
- ブロックベース内部反復 (`.each`/`.map`/`.select`) は fiber 不使用。
- そもそも context switch backend は x86-64/aarch64 asm か POSIX ucontext のみで
  Xtensa/FreeRTOS 向けが無く mmap も無い = ESP32 では移植まで Fiber は動かない。
  → **Fiber 禁止はメモリ最適化かつ ESP32 の前提条件**。
- 担保: (a) desktop Ruby を `Fiber.new`/`Enumerator.new`/`.next`/`.lazy` で grep する gate、
  (b) fork codegen に library/MC モードで Fiber/`Enumerator.new` yielder を
  コンパイルエラーにするハードガードを起案。

### 但し書き: (A)(B) で消えるのは静的配列と fiber mmap のみ

Ruby 呼び出し深さ → RTOS タスクスタック (内部 DRAM) のコストは (A)(B) では減らない。
Ruby ローカルと呼び出しフレームは C スタック上に居続ける。下記のタスクスタック
sizing gate と C スタック深度ガード起案は引き続き有効。

## desktop 投入の gate (推奨)

1. **Phase 2 の Spinel カーネルタスクで `uxTaskGetStackHighWaterMark` を測り、mruby
   カーネルと比較**。MC 化でスタック high-water が何倍になるかの基準線を取る。
2. **desktop 代表 mixin をコンパイルし、最深呼び出し経路の C フレーム総和を見積もる**
   (深い再帰・深いブロックネストの有無を静的に洗う)。
3. **fork へ起案候補**: C スタック深度ガード (呼び出し入口で SP を上限比較して
   SystemStackError を上げる)。Spinel を汎用エンジンにするなら必要な安全機構。
4. **SP_GC_STACK_MAX が SP_MULTI_CTX で per-ctx か共有か**を確認する。

## fork への起案候補 (まとめ)

- C スタック深度ガード (回復可能な SystemStackError 化)。
- Xtensa / FreeRTOS 向け fiber context-switch backend (Phase 5 で必要になる場合)。

上記は fork_pr_candidates.md へ転記する。
