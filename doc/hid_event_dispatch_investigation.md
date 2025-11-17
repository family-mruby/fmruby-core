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

### Root Cause Analysis - cipop() Inconsistency

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

### BREAKTHROUGH - mrb_tick() is the Actual Trigger (2025-11-17)

**重大な発見**: `mrb_tick()`を無効化するとクラッシュが完全に消失することが判明しました。

#### Experiment

`lib/patch/picoruby-machine/ports/posix/hal.c` line 91 の`mrb_tick()`呼び出しをコメントアウト:

```c
// Line 91
//mrb_tick(g_tick_manager.vms[i].mrb);  // ← Commented out
```

#### Results

**mrb_tick()有効時**:
- ランダムなタイミング（1回目～10回目）でクラッシュ発生
- `ci`ポインタが予期せず進む（240→288バイト）
- `cipop()`が呼ばれず、`ci`が戻らない
- バイトコードが破損しているように見える

**mrb_tick()無効時**:
- クラッシュが完全に消失
- `ci`ポインタは常に安定（offset=240バイト維持）
- すべての`on_event`呼び出しが成功
- "on_event: gui app" が毎回出力される

#### Log Evidence - With mrb_tick() Disabled

```
=== BEFORE mrb_funcall ===
Current frame (ci):
ci->proc=0x564e72de1d00 flags=0x100 CFUNC=0
... (ci remains at offset 240 throughout all calls)

on_event: gui app
=== AFTER mrb_funcall ===
... (ci still at offset 240)
```

**すべての呼び出しで上記パターンが繰り返され、クラッシュは発生しない。**

### Root Cause Chain

1. **mrb_tick()が周期的に呼ばれる**: 独立タスクから周期的に実行される
2. **mrb_funcall()実行中にtick処理が干渉**: タスクスケジューリングとの干渉が発生
3. **グローバル変数への依存の可能性**: タスク機能は別の開発者が追加した開発中機能であり、グローバル変数に依存している可能性がある
4. **CIスタックの破損またはコンテキスト切り替えの問題**: tick処理によってVMの内部状態が破壊される
5. **cipop()が呼ばれない**: Rubyメソッドパスで`cipop()`が呼ばれず、`ci`が進んだまま戻らない
6. **ciポインタが永続的に進む**: 次回の`mrb_vm_exec()`でクラッシュ

### Why Does the Classification Change?

~~なぜ`MRB_PROC_CFUNC_P(ci->proc)`の判定が途中で変わるのかは、さらなる調査が必要です。可能性：~~
~~1. JITコンパイル等で手続きオブジェクトが置き換わる~~
~~2. メソッドキャッシュの更新~~
~~3. GCによるオブジェクトの移動や再配置~~
~~4. マルチタスクスケジューリングのタイミング~~

**更新**: 上記の仮説は誤りでした。実際の原因は**mrb_tick()によるVM内部状態の干渉**です。

### Confirmed Facts

1. **REALLOCは問題ではない**: REALLOCが発生しても`ci`のオフセットは正しく維持されている
2. **問題はcipop()の欠如**: Rubyメソッドパスで`cipop()`が呼ばれないことが直接の原因
3. **タイミングはランダム**: マルチタスク環境で特定の条件下でC関数→Rubyメソッドの切り替えが発生
4. **mrb_tick()が実際のトリガー**: tick処理を無効化すると問題が完全に消失する
5. **タスク機能の開発状況**: 別の開発者が追加した開発中機能であり、グローバル変数依存の可能性がある

## Next Investigation Steps

mrb_tick()が実際のトリガーであることが判明したため、以下を調査する必要があります：

1. **mrb_tick()の実装を詳細に調査**
   - `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c` (lines 144-177)
   - どのようにタスクスケジューリングを行っているか
   - グローバル変数への依存関係を特定

2. **mrb_tick()とmrb_funcall()の干渉メカニズムを解明**
   - なぜtick処理がci stackを破壊するのか
   - tick処理がVM内部状態（`mrb->c->ci`）に与える影響
   - リエントラント性の問題の有無

3. **タスク機能の設計を確認**
   - 別の開発者が追加した機能であるため、設計意図を確認
   - マルチタスク環境でのVMコンテキスト管理方法
   - タスク切り替え時のci stackの扱い

4. **適切な修正方法を決定**
   - mrb_tick()をリエントラント対応にする
   - tick処理中のci stack保護機構を追加
   - タスクスイッチングロジックのバグ修正
   - または、mrb_funcall()とtick処理の排他制御を追加

## Potential Solutions

### Solution A: Fix mrb_tick() Implementation (Primary Target)

**本調査の目的は、この問題の根本原因を特定し、適切な修正方法を見つけることです。**

mrb_tick()がmrb_funcall()実行中のVMコンテキストを破壊している問題を修正する必要があります。

#### Option A1: Make mrb_tick() Reentrant-Safe

```c
// task.c - mrb_tick() implementation
void mrb_tick(mrb_state *mrb) {
  // VM状態をチェックし、mrb_funcall実行中なら何もしない
  if (mrb->c->ci->cci == CINFO_SKIP) {
    return;  // Skip tick processing during Ruby method call
  }

  // 既存のtick処理...
}
```

#### Option A2: Add CI Stack Protection

tick処理中にci stackを保護する機構を追加：

```c
// task.c - mrb_tick() implementation
void mrb_tick(mrb_state *mrb) {
  mrb_callinfo *saved_ci = mrb->c->ci;

  // tick処理...

  // ci stackが変更されていないことを確認
  if (mrb->c->ci != saved_ci) {
    mrb->c->ci = saved_ci;  // Restore
  }
}
```

#### Option A3: Add Mutual Exclusion

mrb_funcall()とtick処理の排他制御を追加：

```c
// グローバルフラグまたはmrb_state内フラグを使用
bool in_funcall = false;

// In mrb_funcall_with_block():
in_funcall = true;
// ... 既存のコード ...
in_funcall = false;

// In mrb_tick():
if (in_funcall) return;
// ... tick処理 ...
```

**検討事項**:
- タスク機能は別の開発者が追加した開発中機能
- グローバル変数への依存関係を特定する必要がある
- タスクスイッチング時のVMコンテキスト管理方法を理解する必要がある
- picoruby開発者への確認が必要

### Solution B: Fix mrb_funcall_with_block() (Secondary Option)

**注意**: この修正だけでは、mrb_tick()の干渉問題は解決しない可能性があります。

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
- mrb_tick()の干渉問題は別途対処が必要

### 回避策について（対象外）

以下の回避策は**本調査の対象外**です。根本原因の修正を優先します：

#### ~~Polling Pattern (回避策)~~
- C側から`mrb_funcall()`を使わず、Ruby側でポーリングする方式
- **対象外理由**: 問題を回避するだけで、根本原因を解決しない

#### ~~Direct mrb->c->ci Restoration (回避策)~~
- `mrb->c->ci`を強制的に復元する方式
- **対象外理由**: VMの内部状態を直接操作するため危険であり、本質的な解決ではない

#### ~~Disable mrb_tick() (Temporary Workaround)~~
- mrb_tick()呼び出しを完全に無効化する方式
- **対象外理由**: タスク機能が完全に動作しなくなるため、一時的な回避策としてのみ使用可能

## Conclusion

**最終的な根本原因**: `mrb_tick()`がmrb_funcall()実行中のVM内部状態（ci stack）を破壊していることが判明しました。

### Key Findings

1. **mrb_tick()を無効化するとクラッシュが完全に消失**
   - `lib/patch/picoruby-machine/ports/posix/hal.c:91`の`mrb_tick()`をコメントアウト
   - すべてのHIDイベント処理が正常に動作

2. **cipop()の欠如は症状であり原因ではない**
   - `mrb_funcall_with_block()`のRubyメソッドパスで`cipop()`が呼ばれない
   - しかし、これは設計上の動作である可能性が高い
   - 実際の問題は、mrb_tick()がこの状態を破壊すること

3. **タスク機能の問題**
   - 別の開発者が追加した開発中機能
   - グローバル変数への依存の可能性
   - mrb_funcall()との干渉メカニズムが未解明

### Next Steps

1. **mrb_tick()の実装を詳細に調査**
   - グローバル変数への依存関係を特定
   - タスクスケジューリングがci stackに与える影響を解明

2. **適切な修正方法を決定**
   - mrb_tick()をリエントラント対応にする
   - tick処理中のci stack保護機構を追加
   - mrb_funcall()とtick処理の排他制御を追加

3. **picoruby開発者への報告**
   - タスク機能の設計意図を確認
   - mrb_tick()とmrb_funcall()の共存方法を相談
   - 必要に応じて`lib/patch`にパッチを作成

### Temporary Workaround

現在、mrb_tick()を無効化することで問題を回避しています。これは一時的な措置であり、タスク機能を使用する場合は適切な修正が必要です。

## Debug Infrastructure

この調査のために以下のデバッグインフラストラクチャを構築しました：

### 1. Debug Helper Module (app_debug.c/h)

mruby/proc.hとmruby-compiler2のヘッダ競合を避けるため、専用モジュールを作成：

**Files**:
- `lib/add/picoruby-fmrb-app/ports/esp32/app_debug.h` - ヘッダ（前方宣言のみ）
- `lib/add/picoruby-fmrb-app/ports/esp32/app_debug.c` - 実装（mruby内部ヘッダを分離）

**Functions**:
1. `app_debug_log_proc_details()` - RProc詳細情報をログ出力
   - flags、CFUNC判定、irep情報
   - C関数ポインタまたはRubyバイトコード情報

2. `app_debug_dump_irep_bytecode()` - バイトコード、シンボル、定数プールをダンプ
   - 命令デコード（オペコード名、引数A/B/C）
   - シンボルテーブル（最大10個）
   - 定数プール（文字列、整数、浮動小数点）

3. `app_debug_dump_callstack()` - コールスタック全体をダンプ
   - cibaseからciまでの全フレーム
   - 各フレームのproc情報（C関数/Rubyメソッド）
   - ファイル名情報（debug_info利用）

### 2. VM State Logging (app.c)

`dispatch_hid_event_to_ruby()`に包括的なログを追加：

```c
// BEFORE mrb_funcall
app_debug_log_proc_details(mrb, prev_ci->proc, TAG);
app_debug_log_proc_details(mrb, mrb->c->ci->proc, TAG);
app_debug_dump_callstack(mrb, TAG);
check_mrb_ci_valid(mrb, "before_funcall");

// AFTER mrb_funcall
app_debug_log_proc_details(mrb, mrb->c->ci->proc, TAG);
app_debug_dump_callstack(mrb, TAG);
check_mrb_ci_valid(mrb, "after_funcall");
```

### 3. Build Configuration

`components/picoruby-esp32/CMakeLists.txt`にapp_debug.cを追加：

```cmake
set(PICORUBY_SRCS
  ...
  ${CMAKE_CURRENT_LIST_DIR}/../../lib/add/picoruby-fmrb-app/ports/esp32/app_debug.c
)
```

## Debug Log Output Summary

詳細なデバッグログを有効化して記録した結果（2025-11-17）:

```
Event 1: REALLOC発生、ci offset維持 (240→240 bytes)
Event 2-8: 正常動作、ci不変
Event 9: ci が進む (240→288 bytes)、その後クラッシュ
```

**With mrb_tick() disabled**:
```
All events: ci offset安定 (240 bytes維持)、クラッシュなし
```

完全なログは実行時に確認可能。`check_mrb_ci_valid()` 関数が以下を出力：
- Tick、App ID、App Name、Status
- cibase、ci、ciend のアドレス
- スタック深度と使用率
- REALLOC検出

Debug helper関数が以下を出力：
- RProc詳細（flags、CFUNC判定、irep情報）
- バイトコードダンプ（命令、シンボル、定数）
- コールスタック全体（cibase→ci）

## References

### Code Files

- `lib/add/picoruby-fmrb-app/ports/esp32/app.c` - HID event dispatch implementation
- `lib/add/picoruby-fmrb-app/ports/esp32/app_debug.c` - Debug helper functions
- `lib/add/picoruby-fmrb-app/ports/esp32/app_debug.h` - Debug helper header
- `lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb` - FmrbApp class with on_event method
- `lib/patch/picoruby-machine/ports/posix/hal.c` - HAL implementation with mrb_tick() call
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c` - Multi-task scheduler (mrb_tick implementation)
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/vm.c` - VM implementation (mrb_funcall_with_block)

### Documentation

- `doc/mrb_funcall_usage_patterns.md` - picoruby standard patterns
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/doc/guides/gc-arena-howto.md` - GC arena guide

## Lessons Learned

1. **問題はランダムに発生する**: 毎回ではなく、特定の条件下で発生
2. **REALLOCは直接の原因ではない**: REALLOCが発生してもciのオフセットは正しく維持される
3. **cipop()の欠如は症状であり原因ではない**: ~~Rubyメソッドパスで`cipop()`が呼ばれない~~ → 実際の原因はmrb_tick()の干渉
4. **詳細なデバッグログが重要**: VM状態を記録することで問題のパターンが見えた
5. **GC arena management != call info stack management**: 別の概念として管理が必要
6. **C関数とRubyメソッドの扱いが異なる**: `mrb_funcall()`内部で分岐している
7. **mrb_tick()が実際のトリガー**: 周期的なtick処理がmrb_funcall()と干渉してci stackを破壊
8. **機能の無効化による問題の切り分けが効果的**: mrb_tick()を無効化することで、真の原因を特定できた
9. **開発中機能のリスク**: 別の開発者が追加した開発中機能は、予期しない動作をする可能性がある
10. **マルチタスク環境でのVM状態管理の重要性**: タスクスケジューリングとVMコンテキストの適切な管理が必須
