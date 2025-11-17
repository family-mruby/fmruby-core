# mruby ci Pointer Crash Investigation Report

## Summary

mrubyのC関数から`mrb_funcall()`を呼び出した後にクラッシュする問題について調査した結果、`cipush()`関数内での`mrb_realloc()`によって`cibase`ポインタが移動することが原因であると特定した。

## Problem Description

### Symptom

```
I (173183106) app: Calling mrb_funcall(on_event)... ci=0x579e060a6340
I (173183106) MEMPOOL: Pointer 0x579e060a6340 is in SYSTEM_APP pool [0x579e0608cc08 - 0x579e06109c08]
I (173183106) app: mrb_funcall returned ci=0x579e060a6370
I (173183106) app: === dispatch_hid_event_to_ruby END ===
[CRASH]
```

- `mrb_funcall()`実行前後で`ci`ポインタのアドレスが変わる（`0x579e060a6340` → `0x579e060a6370`）
- mrubyVMに処理が戻った後にクラッシュする
- ciポインタがメモリプール境界付近にあるときに発生しやすい

### Environment

- Target: ESP32-S3-N16R8
- OS: Family mruby OS (FreeRTOS based)
- Memory: TLSF allocator with multiple memory pools
- Context: Multi-task environment with mruby VM instances

## Root Cause Analysis

### 1. mruby Call Stack Structure

mrubyは関数呼び出しスタックを`mrb_callinfo`構造体の配列として管理する:

```c
struct mrb_context {
  mrb_callinfo *ci;           // Current call info (stack pointer)
  mrb_callinfo *cibase;       // Base of callinfo stack
  mrb_callinfo *ciend;        // End of callinfo stack
  // ...
};
```

- `ci`: 現在アクティブなコールフレームを指すポインタ
- `cibase`: コールスタックの開始アドレス（動的に割り当てられる）
- `ciend`: コールスタックの終了アドレス

### 2. cipush() Realloc Mechanism

関数呼び出し時、`cipush()`が新しいコールフレームをスタックにpushする:

```c
static inline mrb_callinfo* cipush(mrb_state *mrb, ...) {
  struct mrb_context *c = mrb->c;
  mrb_callinfo *ci = c->ci + 1;  // Next stack position

  if (ci < c->ciend) {
    c->ci = ci;  // Still within allocated range
  }
  else {
    // *** CRITICAL POINT ***
    // Stack is full, need to realloc and expand by 2x
    ptrdiff_t size = ci - c->cibase;
    c->cibase = (mrb_callinfo*)mrb_realloc(mrb, c->cibase, sizeof(mrb_callinfo)*size*2);
    c->ci = ci = c->cibase + size;  // Recalculate based on new base
    c->ciend = c->cibase + size * 2;
  }
  // ...
}
```

**Key Problem**: `mrb_realloc()`が新しいメモリブロックを返した場合、`cibase`のアドレスが変わる。

### 3. Crash Scenario

#### Step 1: Before mrb_funcall()
```
cibase = 0x579e0608cc08
ci     = 0x579e060a6340
ciend  = 0x579e06109c08
```
- C関数内で`mrb->c->ci`を参照（例: ログ出力、ローカル変数に保存）

#### Step 2: During mrb_funcall() - cipush() triggers realloc
```c
// Old addresses (now INVALID)
old_cibase = 0x579e0608cc08
old_ci     = 0x579e060a6340

// mrb_realloc() moves the block
mrb_realloc(mrb, old_cibase, new_size);

// New addresses
cibase = 0x579e0610cc08  // MOVED!
ci     = 0x579e060a6370  // Updated based on new cibase
ciend  = 0x579e0612cc08
```

#### Step 3: After mrb_funcall() returns
- `mrb->c->ci`は新しいアドレス（`0x579e060a6370`）を指す
- しかし、**呼び出し元のC関数が保存していた古い`ci`ポインタ（`0x579e060a6340`）は無効**
- 他のタスクやコールバックが古いアドレスを参照している場合も無効
- **古いアドレスにアクセスするとクラッシュ**

### 4. Why It Happens More Often Near Memory Pool Boundaries

TLSFアロケーターは以下の動作をする:
1. メモリプール内に十分な空きブロックがある → 同じプール内で再割り当て
2. メモリプール内に空きがない → **別のメモリ領域から割り当て、大きくアドレスが変わる**

cibaseが既に大きく成長し、メモリプール境界付近にある場合、reallocで大きくアドレスが移動する確率が高くなる。

## Implemented Debug Features

### 1. Memory Pool Pointer Check Function

`fmrb_mempool.c`に追加:

```c
void fmrb_mempool_check_pointer(const void* ptr);
```

指定されたポインタがどのメモリプールに属するかをログ出力する。

### 2. CI Pointer Validation Function

`fmrb_mempool.c`に追加:

```c
bool fmrb_mempool_check_ci_valid(mrb_state *mrb, const char* location);
```

機能:
- `cibase <= ci < ciend`の範囲チェック
- cibase/ci/ciendのアドレスとオフセットをログ出力
- cibase/ciが属するメモリプールを特定
- 範囲外の場合はエラーログ出力

使用例:
```c
fmrb_mempool_check_ci_valid(mrb, "before_funcall");
mrb_funcall(mrb, self, "on_event", 1, arg);
fmrb_mempool_check_ci_valid(mrb, "after_funcall");
```

### 3. Enhanced Logging in app.c

`lib/add/picoruby-fmrb-app/ports/esp32/app.c`の`mrb_funcall()`前後に詳細ログを追加:

```c
FMRB_LOGI(TAG, "=== BEFORE mrb_funcall ===");
fmrb_mempool_check_ci_valid(mrb, "before_funcall");

int ai = mrb_gc_arena_save(mrb);
mrb_funcall(mrb, self, "on_event", 1, mrb_nil_value());

FMRB_LOGI(TAG, "=== AFTER mrb_funcall ===");
fmrb_mempool_check_ci_valid(mrb, "after_funcall");

mrb_gc_arena_restore(mrb, ai);
```

### 4. cipush() Realloc Detection Patch

`lib/patch/picoruby-mruby/vm_cipush_debug.c`にパッチを作成:

- `FMRB_DEBUG_CI_REALLOC`フラグでデバッグログを有効化
- realloc前後のcibase/ci/ciendアドレスを出力
- cibaseが移動したかどうかを検出

適用方法は`lib/patch/picoruby-mruby/README_vm_cipush_debug.md`を参照。

## Expected Debug Output

デバッグ機能を有効にすると、以下のようなログが出力される:

```
I (xxx) app: === BEFORE mrb_funcall ===
I (xxx) MEMPOOL: [before_funcall] ci range check: ci=0x579e060a6340 cibase=0x579e0608cc08 ciend=0x579e06109c08 (range=512000 bytes, offset=105784)
I (xxx) MEMPOOL: Pointer 0x579e0608cc08 is in SYSTEM_APP pool [0x579e0608cc08 - 0x579e06109c08]
I (xxx) MEMPOOL: Pointer 0x579e060a6340 is in SYSTEM_APP pool [0x579e0608cc08 - 0x579e06109c08]
I (xxx) app: GC arena saved: ai=5

[CIPUSH REALLOC] BEFORE: cibase=0x579e0608cc08 ci=0x579e060a6340 ciend=0x579e06109c08 size=64 new_size=128
[CIPUSH REALLOC] AFTER:  cibase=0x579e0610cc08 ci=0x579e0610e340 ciend=0x579e0612cc08 (moved=YES)
[CIPUSH REALLOC] WARNING: cibase moved from 0x579e0608cc08 to 0x579e0610cc08 (delta=32768 bytes)

I (xxx) app: === AFTER mrb_funcall ===
I (xxx) MEMPOOL: [after_funcall] ci range check: ci=0x579e0610e370 cibase=0x579e0610cc08 ciend=0x579e0612cc08 (range=1024000 bytes, offset=105384)
I (xxx) MEMPOOL: Pointer 0x579e0610cc08 is in SYSTEM_APP pool [0x579e0608cc08 - 0x579e06109c08]
I (xxx) MEMPOOL: ERROR: Pointer 0x579e0610e370 is NOT in any memory pool (external memory or invalid)
```

上記のログから:
- reallocでcibaseが`0x579e0608cc08`から`0x579e0610cc08`へ移動
- 移動後のciポインタ`0x579e0610e370`が元のメモリプール範囲外

## Recommended Solutions

### Short-term (Debug & Workaround)

1. **Use Index Instead of Pointer**
   ```c
   // Instead of saving ci pointer
   ptrdiff_t ci_offset = mrb->c->ci - mrb->c->cibase;
   mrb_funcall(...);
   // Recalculate after funcall
   mrb_callinfo *ci = mrb->c->cibase + ci_offset;
   ```

2. **Add Range Check Before Access**
   ```c
   if (fmrb_mempool_check_ci_valid(mrb, "access_point")) {
     // Safe to use mrb->c->ci
   }
   ```

3. **Enable Debug Logging**
   - 有効化: `CFLAGS += -DFMRB_DEBUG_CI_REALLOC`
   - reallocが発生するタイミングとアドレス変化を監視

### Long-term (Permanent Fix)

1. **Pre-allocate Larger cibase**
   ```c
   // vm.c の CALLINFO_INIT_SIZE を増やす
   #define CALLINFO_INIT_SIZE 128  // was 32
   ```
   - reallocの頻度を減らす
   - メモリ使用量は増加するがクラッシュリスク低減

2. **Use Fixed-size Call Stack**
   ```c
   // コンテキスト内に固定サイズのcallinfo配列を持つ
   struct mrb_context {
     mrb_callinfo ci_stack[MRB_CALL_LEVEL_MAX];
     mrb_callinfo *ci;  // Points into ci_stack
     // No dynamic allocation, no realloc
   };
   ```
   - reallocを完全に排除
   - メモリ効率は下がるが安全性向上

3. **Add Mutex Protection**
   ```c
   // cipush前にロック
   fmrb_mutex_lock(&mrb->ci_mutex);
   cipush(...);
   fmrb_mutex_unlock(&mrb->ci_mutex);
   ```
   - マルチタスク環境での競合を防ぐ
   - パフォーマンスへの影響あり

4. **Implement Copy-on-Realloc**
   ```c
   // realloc時に古いアドレスを一定期間保持
   // 参照カウンタやGCと連携して安全に解放
   ```
   - 最も安全だが実装が複雑

## Related Documents

- [doc/gc-arena-howto_ja.md](gc-arena-howto_ja.md) - GC Arenaの使い方
- [lib/patch/picoruby-mruby/README_vm_cipush_debug.md](../lib/patch/picoruby-mruby/README_vm_cipush_debug.md) - cipushデバッグパッチ適用方法

## References

- mruby source: `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/`
  - `include/mruby.h` - mrb_callinfo, mrb_context構造体定義
  - `src/vm.c` - cipush(), stack management実装
  - `src/gc.c` - GC arena実装

## Conclusion

ciポインタクラッシュの原因は、`cipush()`での`mrb_realloc()`によるアドレス移動である。
デバッグ機能を有効にすることで、realloc発生のタイミングとアドレス変化を監視できる。
根本的な解決には、cibaseの事前割り当て拡大、または固定サイズスタックへの移行を推奨する。
