# HID Event Dispatch Investigation Report

Date: 2025-11-13
Issue: Segmentation fault when dispatching HID events to Ruby on_event() method

## Problem Summary

HIDイベント（キーボード/マウス）をRubyの`on_event()`メソッドにディスパッチする際、7回目前後の呼び出しでSegmentation faultが発生する問題を調査しました。

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

## Fundamental Problem

**`mrb_funcall()` is not compatible with picoruby's multi-task scheduler in this usage pattern.**

`mrb_funcall()`は、**同期的なRuby呼び出し**を想定しており、呼び出し元のVMコンテキストが変更されないことを前提としています。しかし、マルチタスクスケジューラは各タスクのVMコンテキストを保存・復元するため、`mrb_funcall()`による`mrb->c->ci`の変更が問題を引き起こします。

## Recommended Solutions

### Solution A: Polling Pattern (Recommended)

C側からイベントをpushするのではなく、Ruby側からpullする方式に変更：

#### C Side (app.c)

```c
// FmrbApp#_poll_event() -> Hash or nil
// Poll one HID event from message queue (non-blocking)
static mrb_value mrb_fmrb_app_poll_event(mrb_state *mrb, mrb_value self)
{
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        return mrb_nil_value();
    }

    // Try to receive message (non-blocking, timeout=0)
    fmrb_msg_t msg;
    fmrb_err_t ret = fmrb_msg_receive(ctx->app_id, &msg, 0);

    if (ret == FMRB_OK && msg.type == FMRB_MSG_TYPE_HID_EVENT) {
        // Build and return event hash
        return build_hid_event_hash(mrb, &msg);
    }

    return mrb_nil_value();  // No event available
}
```

#### Ruby Side (fmrb-app.rb)

```ruby
def main_loop
  loop do
    return if !@running

    # Poll HID events
    while event = _poll_event()
      on_event(event)
    end

    timeout_ms = on_update
    _spin(timeout_ms)
  end
end
```

**Advantages:**
- No `mrb_funcall()` from C to Ruby
- Ruby calls its own methods naturally
- Compatible with multi-task scheduler
- Simple and clean architecture

### Solution B: Direct mrb->c->ci Restoration (Not Recommended)

```c
static void dispatch_hid_event_to_ruby(mrb_state *mrb, mrb_value self, const fmrb_msg_t *msg)
{
    mrb_callinfo *saved_ci = mrb->c->ci;

    mrb_funcall(mrb, self, "on_event", 1, event_hash);

    mrb->c->ci = saved_ci;  // Force restore
}
```

**Disadvantages:**
- Dangerous: modifies internal VM state
- May cause other issues (memory leaks, GC problems)
- Not maintainable

### Solution C: Message Queue + Ruby Callback Registration

Rubyアプリ起動時にコールバックを登録し、C側でキューに溜めておく方式：

```ruby
def initialize
  super()
  register_event_callback  # Register callback in C
end
```

**Disadvantages:**
- Complex implementation
- Still uses `mrb_funcall()` internally

## Conclusion

**Recommended approach: Solution A (Polling Pattern)**

This is the most compatible and maintainable solution for picoruby's multi-task environment:

1. C側はイベントをメッセージキューに保存するだけ
2. Ruby側が`_poll_event()`でイベントを取得
3. Ruby側が自分の`on_event()`を呼び出す
4. No `mrb_funcall()` from C → No call info stack corruption

## Implementation Plan

1. Remove `dispatch_hid_event_to_ruby()` function
2. Implement `mrb_fmrb_app_poll_event()` in `app.c`
3. Update `fmrb-app.rb` to poll events in `main_loop`
4. Test with keyboard and mouse events
5. Update documentation

## References

- `doc/mrb_funcall_usage_patterns.md` - picoruby standard patterns
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c` - Multi-task scheduler
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/doc/guides/gc-arena-howto.md` - GC arena guide

## Lessons Learned

1. **`mrb_funcall()` should be used carefully in multi-task environments**
2. **picoruby standard mrbgems avoid `mrb_funcall()` when possible**
3. **GC arena management != call info stack management**
4. **Always check picoruby standard patterns before implementing new features**
5. **Polling patterns are often simpler and more robust than callback patterns**
