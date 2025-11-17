# HID Event Dispatch Investigation Report

Date: 2025-11-13
Last Updated: 2025-11-17
Issue: Segmentation fault when dispatching HID events to Ruby on_event() method

## Problem Summary

HIDイベント（キーボード/マウス）をRubyの`on_event()`メソッドにディスパッチする際、ランダムなタイミング（1回目～10回目程度）でSegmentation faultが発生する問題を調査しました。

**重要な訂正**: 当初「毎回`mrb->c->ci`が増える」と考えていましたが、実際には**ランダムなタイミングで発生**することが判明しました。

## Investigation Process

### 1. Initial Symptoms

- HIDイベントを6-7回処理した後にSegfault
- ログでは`mrb_funcall()`が正常に返っているように見えるが、Rubyメソッドが実行されていない
- 空の関数にするとクラッシュしない

### 2. External Analysis

外部AIからの指摘：
1. 構造体キャストの問題（subtype二重読み）
2. 例外処理の欠如（`mrb_protect`未使用）
3. GC arena管理の欠如

### 3. picoruby標準パターンの調査

`doc/mrb_funcall_usage_patterns.md`を作成し、picoruby標準のmrbgemでの`mrb_funcall`使用パターンを調査：

**重要な発見:**
- 標準mrbgemでは`mrb_funcall`の使用は非常に限定的
- ほとんどのmrbgemはC関数を直接実装し、`mrb_funcall`を使わない
- `mrb_protect`もほとんど使用されていない
- 例外処理は`mrb->exc`を直接チェックするパターンが標準
- **繰り返し`mrb_funcall`を呼び出す場合、GC arena管理が必須**

### 4. GDB Debugging - Root Cause Identified

最適化を無効化（`-O0 -g3`）してGDBで詳細調査：

```
before funcall: mrb->c->ci = 0x555555829438 <g_mempool_system_app+104496>
after funcall:  mrb->c->ci = 0x555555829468 <g_mempool_system_app+104544>
                             ↑ 48バイト進んだまま（mrb_callinfo 1個分）
```

**Root Cause:**
- `mrb_funcall()`が内部で`mrb->c->ci`（call info pointer）を進める
- しかし、**元に戻さない**
- これが繰り返されると、`mrb->c->ci`がメモリプール範囲外を指すようになる
- `mrb_tasks_run()`の次のイテレーションで`mrb_vm_exec(mrb, mrb->c->ci->proc, mrb->c->ci->pc)`を呼び出すときにクラッシュ

### 5. Why This Happens

`components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c:431`:

```c
tcb->value = mrb_vm_exec(mrb, mrb->c->ci->proc, mrb->c->ci->pc);
```

マルチタスクスケジューラ（`mrb_tasks_run()`）は、VMコンテキスト（`mrb->c->ci`）がタスク間で正しく管理されていることを前提としています。しかし、`mrb_funcall()`はこの前提を破ってしまいます。

## Technical Details

### mrb_callinfo Stack Corruption

```
Initial state:   mrb->c->ci = 0x...438  (base)
After funcall 1: mrb->c->ci = 0x...468  (+48 bytes)
After funcall 2: mrb->c->ci = 0x...498  (+48 bytes)
After funcall 3: mrb->c->ci = 0x...4c8  (+48 bytes)
...
After funcall 7: mrb->c->ci = 0x...5e8  (out of bounds!)
                 ↓
              Segfault in mrb_vm_exec()
```

### Memory Pool Information

```
Symbol: g_mempool_system_app
Offset at crash: +104544 bytes (after 7 funcalls)
```

`mrb_callinfo`構造体のサイズは48バイトで、`mrb_funcall()`の各呼び出しでスタックに1つ積まれます。

## Why GC Arena Management Didn't Help

GC arena管理を追加しても問題は解決しませんでした。なぜなら：

1. GC arenaは**Rubyオブジェクト**を保護するための機構
2. `mrb->c->ci`は**VMの内部状態**（call info stack）
3. 両者は別の問題

## Attempted Solutions

### ❌ Solution 1: Add GC Arena Management

```c
int ai = mrb_gc_arena_save(mrb);
mrb_funcall(mrb, self, "on_event", 1, event_hash);
mrb_gc_arena_restore(mrb, ai);
```

**Result:** Still crashed. GC arenaはRubyオブジェクトを管理するが、call info stackは管理しない。

### ❌ Solution 2: Add mrb_protect

```c
mrb_value result = mrb_protect(mrb, ...);
```

**Result:** Still crashed. `mrb_protect`は例外処理のためであり、call info stack問題を解決しない。

### ❌ Solution 3: Remove Hash Allocation

```c
mrb_funcall(mrb, self, "on_event", 1, mrb_nil_value());
```

**Result:** Still crashed. Hashの有無は関係なく、`mrb_funcall()`自体が問題。

## Fundamental Problem - Root Cause Identified (2025-11-17)

### Detailed VM State Analysis

詳細なデバッグログを追加して、`mrb->c->ci`、`cibase`、`ciend`、tick、タスク情報を記録した結果、根本原因が判明しました。

### Observed Behavior Pattern

**1回目の呼び出し（REALLOC発生）:**
```
[before_funcall] ci=0x564ce2f993d0 (using 5/8 frames, 62%, offset=240 bytes)
[before_funcall] cibase=0x564ce2f992e0 ciend=0x564ce2f99460 (capacity=8 frames)

*** REALLOC DETECTED ***
cibase: 0x564ce2f992e0 -> 0x564ce2f9b238 (moved=YES, delta=8024 bytes)
ciend:  0x564ce2f99460 -> 0x564ce2f9b538 (moved=YES, delta=8408 bytes)

[after_funcall] ci=0x564ce2f9b328 (using 5/16 frames, 31%, offset=240 bytes)
[after_funcall] cibase=0x564ce2f9b238 ciend=0x564ce2f9b538 (capacity=16 frames)
```

**結果**: REALLOC発生だが、`ci`のオフセット（240バイト＝5フレーム）は正しく維持されている。

**2-8回目の呼び出し（正常）:**
```
[before_funcall] ci=0x564ce2f9b328 (using 5/16 frames, 31%, offset=240 bytes)
[after_funcall]  ci=0x564ce2f9b328 (using 5/16 frames, 31%, offset=240 bytes)
```

**結果**: `cibase`、`ciend`、`ci`すべて変化なし。正常。

**9回目の呼び出し（異常発生！）:**
```
[before_funcall] ci=0x564ce2f9b328 (using 5/16 frames, 31%, offset=240 bytes)
[after_funcall]  ci=0x564ce2f9b358 (using 6/16 frames, 37%, offset=288 bytes)
                      ↑ 48バイト進んでいる！
```

**結果**:
- `cibase`と`ciend`は変化なし（REALLOCなし）
- **`ci`だけが48バイト進んでいる（240→288バイト）**
- フレーム数が5→6に増加
- その後クラッシュ

### Root Cause Analysis

**問題**: `mrb_funcall()`が条件によって`cipop()`を呼ぶ場合と呼ばない場合がある。

`components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/vm.c` の `mrb_funcall_with_block()` (777-848行目):

```c
// Line 826-833: C関数の場合
if (MRB_PROC_CFUNC_P(ci->proc)) {
  // ... C関数実行 ...
  cipop(mrb);  // ← cipop()を呼ぶ
  return val;
}

// Line 835-843: Rubyメソッドの場合
else {
  ci->cci = CINFO_SKIP;
  val = mrb_run(mrb, ci->proc, self);
  // ← cipop()を呼ばない！
  return val;
}
```

**判明した事実**:
- 1-8回目: `on_event`がC関数として扱われる → `cipop()`が呼ばれる → `ci`が元に戻る
- 9回目: `on_event`がRubyメソッドとして扱われる → `cipop()`が呼ばれない → `ci`が進んだまま

### Why Does the Classification Change?

なぜ`MRB_PROC_CFUNC_P(ci->proc)`の判定が途中で変わるのかは、さらなる調査が必要です。可能性：
1. JITコンパイル等で手続きオブジェクトが置き換わる
2. メソッドキャッシュの更新
3. GCによるオブジェクトの移動や再配置
4. マルチタスクスケジューリングのタイミング

### Confirmed Facts

1. **REALLOCは問題ではない**: REALLOCが発生しても`ci`のオフセットは正しく維持されている
2. **問題はcipop()の欠如**: Rubyメソッドパスで`cipop()`が呼ばれないことが直接の原因
3. **タイミングはランダム**: マルチタスク環境で特定の条件下でC関数→Rubyメソッドの切り替えが発生

## Next Investigation Steps

根本原因は特定できましたが、さらに以下を調査する必要があります：

1. **なぜ`on_event`の分類が変わるのか**
   - `MRB_PROC_CFUNC_P(ci->proc)`の判定が途中で変わる理由
   - GDB で `ci->proc` の内容を調査
   - `ci->proc->flags` の値を確認

2. **Ruby側の`on_event`定義を確認**
   - `lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb` の実装
   - Rubyメソッドとして定義されているのか、C関数として定義されているのか

3. **picorubyのメソッド解決メカニズム**
   - メソッド呼び出し時にどのように手続きオブジェクトを選択しているか
   - メソッドキャッシュの仕組み

## Potential Solutions

### Solution A: Fix mrb_funcall_with_block() (Target Solution)

**本調査の目的は、この問題の根本原因を特定し、適切な修正方法を見つけることです。**

Rubyメソッドパスにも`cipop()`を追加する案：

```c
// vm.c Line 835-843
else {
  ci->cci = CINFO_SKIP;
  val = mrb_run(mrb, ci->proc, self);
  cipop(mrb);  // ← 追加
  return val;
}
```

**検討事項**:
- この修正が正しいかどうかは、picorubyの設計思想を理解する必要がある
- `mrb_run()`が内部で`cipop()`を呼ぶことを想定している可能性もある
- picoruby開発者への確認が必要
- なぜC関数とRubyメソッドで挙動が異なるのか、その理由を理解する必要がある

### 回避策について（対象外）

以下の回避策は**本調査の対象外**です。根本原因の修正を優先します：

#### ~~Polling Pattern (回避策)~~
- C側から`mrb_funcall()`を使わず、Ruby側でポーリングする方式
- **対象外理由**: 問題を回避するだけで、根本原因を解決しない

#### ~~Direct mrb->c->ci Restoration (回避策)~~
- `mrb->c->ci`を強制的に復元する方式
- **対象外理由**: VMの内部状態を直接操作するため危険であり、本質的な解決ではない

## Conclusion

根本原因は`mrb_funcall_with_block()`のRubyメソッドパスで`cipop()`が呼ばれないことです。

**次のステップ**:
1. なぜ`on_event`の分類（C関数/Rubyメソッド）が途中で変わるのかを調査
2. `mrb_run()`の実装を確認し、`cipop()`を呼ぶべきかどうかを判断
3. picoruby開発者に問題を報告し、適切な修正方法を確認
4. 必要に応じて`lib/patch`にパッチを作成

## Debug Log Output Summary

詳細なデバッグログを有効化して記録した結果（2025-11-17）:

```
Event 1: REALLOC発生、ci offset維持 (240→240 bytes)
Event 2-8: 正常動作、ci不変
Event 9: ci が進む (240→288 bytes)、その後クラッシュ
```

完全なログは実行時に確認可能。`check_mrb_ci_valid()` 関数が以下を出力：
- Tick、App ID、App Name、Status
- cibase、ci、ciend のアドレス
- スタック深度と使用率
- REALLOC検出

## References

- `doc/mrb_funcall_usage_patterns.md` - picoruby standard patterns
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c` - Multi-task scheduler
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/doc/guides/gc-arena-howto.md` - GC arena guide

## Lessons Learned

1. **問題はランダムに発生する**: 毎回ではなく、特定の条件下で発生
2. **REALLOCは直接の原因ではない**: REALLOCが発生してもciのオフセットは正しく維持される
3. **cipop()の欠如が根本原因**: Rubyメソッドパスで`cipop()`が呼ばれない
4. **詳細なデバッグログが重要**: VM状態を記録することで問題のパターンが見えた
5. **GC arena management != call info stack management**: 別の概念として管理が必要
6. **C関数とRubyメソッドの扱いが異なる**: `mrb_funcall()`内部で分岐している
