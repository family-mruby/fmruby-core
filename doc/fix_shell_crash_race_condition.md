# シェルアプリ起動時のクラッシュ修正案

## 問題の概要

シェルアプリを起動すると、セグメンテーションフォルトでクラッシュします。
- クラッシュは `system_gui` スレッド内で発生
- 直前に「CI stack leak」警告(delta=8920バイト)が出力される
- shellスレッド自体は正常に起動しているが、system_guiがイベント処理中にクラッシュ

## 根本原因

これは**レースコンディション**です:

1. `system_gui` が HID イベントを処理するために `dispatch_hid_event_to_ruby()` を実行
2. この関数内で `mrb_task_is_switching(mrb)` をチェック (app.c:339行目)
3. チェック時点では `switching_` は FALSE
4. **ここでレース発生**: 別スレッド(shell)が `q_insert_task` → `preempt_running_task` → `switching_ = TRUE`
5. `mrb_funcall(mrb, self, "on_event", 1, event_hash)` が実行される (app.c:356行目)
6. `mrb_funcall` 内部の `mrb_vm_exec` が `switching_` を検出して早期リターン
7. **CIフレームがpopされずにリーク**
8. リークが蓄積(8920バイト = 約186フレーム分)
9. 最終的にCIスタック破損によりSEGSEGV

### レース発生箇所

```c
// app.c:338-356
if (mrb_task_is_switching(mrb)) {
    // wait...
    while (mrb_task_is_switching(mrb)) {
        fmrb_task_delay_ms(1);
    }
}
// ← ★ここで switching_ が TRUE になる可能性★
mrb_callinfo *ci_before = mrb->c->ci;
mrb_funcall(mrb, self, "on_event", 1, event_hash);  // ← レース発生
```

## 修正案: オプション1 - 排他制御の強化

### 方針

`mrb_funcall` 実行中は新しいタスク切り替えを開始させない。
イベント配信中であることを示すフラグを追加し、このフラグが立っている間は
`preempt_running_task()` で `switching_` を TRUE に設定せず、切り替えを遅延させる。

### 修正する箇所

#### 1. mruby.h の mrb_task_state 構造体にフラグを追加

**ファイル:** `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/include/mruby.h`

**変更前:**
```c
typedef struct mrb_task_state {
  mrb_tcb *queues[MRB_NUM_TASK_QUEUE];
  volatile uint32_t tick;
  volatile uint32_t wakeup_tick;
  volatile mrb_bool switching;
} mrb_task_state;
```

**変更後:**
```c
typedef struct mrb_task_state {
  mrb_tcb *queues[MRB_NUM_TASK_QUEUE];
  volatile uint32_t tick;
  volatile uint32_t wakeup_tick;
  volatile mrb_bool switching;
  volatile mrb_bool dispatching_event;  // Prevent task switching during event dispatch
  volatile mrb_bool switching_pending;  // Task switch requested during critical section
} mrb_task_state;
```

#### 2. task.h に関数プロトタイプを追加

**ファイル:** `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/include/task.h`

**追加する内容:**
```c
// Critical section management for event dispatch
void mrb_task_begin_critical(mrb_state *mrb);
void mrb_task_end_critical(mrb_state *mrb);
```

**追加場所:** 既存の関数プロトタイプの後、`MRB_END_DECL` の前 (120行目付近)

#### 3. task.c に新しいマクロと関数を実装

**ファイル:** `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c`

**3-1. マクロ定義を追加 (43行目付近、既存のマクロの後)**

```c
#define dispatching_event_   mrb->task.dispatching_event
#define switching_pending_   mrb->task.switching_pending
```

**3-2. preempt_running_task() を修正 (130-136行目)**

**変更前:**
```c
inline static void
preempt_running_task(mrb_state *mrb)
{
  for (mrb_tcb *t = q_ready_; t != NULL; t = t->next) {
    if (t->status == TASKSTATUS_RUNNING) switching_ = TRUE;
  }
}
```

**変更後:**
```c
inline static void
preempt_running_task(mrb_state *mrb)
{
  for (mrb_tcb *t = q_ready_; t != NULL; t = t->next) {
    if (t->status == TASKSTATUS_RUNNING) {
      // If in critical section (dispatching event), delay the switch
      if (dispatching_event_) {
        switching_pending_ = TRUE;
      } else {
        switching_ = TRUE;
      }
    }
  }
}
```

**3-3. 新しい関数を追加 (ファイル末尾、mrb_task_is_switching の後)**

```c
void
mrb_task_begin_critical(mrb_state *mrb)
{
  dispatching_event_ = TRUE;
  switching_pending_ = FALSE;
}

void
mrb_task_end_critical(mrb_state *mrb)
{
  dispatching_event_ = FALSE;
  // If task switch was requested during critical section, apply it now
  if (switching_pending_) {
    switching_ = TRUE;
    switching_pending_ = FALSE;
  }
}
```

#### 4. app.c の dispatch_hid_event_to_ruby() を修正

**ファイル:** `components/picoruby-esp32/picoruby/mrbgems/picoruby-fmrb-app/ports/esp32/app.c`

**変更箇所:** 353-366行目

**変更前:**
```c
// Save CI pointer before funcall to detect CI stack leak
mrb_callinfo *ci_before = mrb->c->ci;

mrb_funcall(mrb, self, "on_event", 1, event_hash);

// Detect CI stack leak: if ci moved forward, mrb_vm_exec returned
// without popping the frame pushed by cipush (task switching race).
// Restore ci to prevent accumulation that leads to NULL proc crash.
if (mrb->c && mrb->c->ci > ci_before) {
    FMRB_LOGW(TAG, "CI stack leak detected after on_event: ci=%p, expected=%p (delta=%td)",
              mrb->c->ci, ci_before,
              (ptrdiff_t)((char*)mrb->c->ci - (char*)ci_before));
    mrb->c->ci = ci_before;
}
```

**変更後:**
```c
// Save CI pointer before funcall to detect CI stack leak
mrb_callinfo *ci_before = mrb->c->ci;

// Enter critical section to prevent task switching during mrb_funcall
extern void mrb_task_begin_critical(mrb_state *mrb);
extern void mrb_task_end_critical(mrb_state *mrb);
mrb_task_begin_critical(mrb);

mrb_funcall(mrb, self, "on_event", 1, event_hash);

// Exit critical section (apply pending task switch if any)
mrb_task_end_critical(mrb);

// Detect CI stack leak: if ci moved forward, mrb_vm_exec returned
// without popping the frame pushed by cipush (task switching race).
// Restore ci to prevent accumulation that leads to NULL proc crash.
if (mrb->c && mrb->c->ci > ci_before) {
    FMRB_LOGW(TAG, "CI stack leak detected after on_event: ci=%p, expected=%p (delta=%td)",
              mrb->c->ci, ci_before,
              (ptrdiff_t)((char*)mrb->c->ci - (char*)ci_before));
    mrb->c->ci = ci_before;
}
```

#### 5. kernel.c も同様に修正

**ファイル:** `components/picoruby-esp32/picoruby/mrbgems/picoruby-fmrb-kernel/ports/esp32/kernel.c`

kernel.c にも同じパターンの mrb_funcall 呼び出しがあるか確認し、あれば同様に修正します。

**確認箇所:** `mrb_funcall` を検索して、イベント配信やメッセージ処理で使用されている箇所

## 動作原理

### Before (レース条件あり)

```
[system_gui thread]              [shell thread]
   |
   check switching_ == FALSE
   |                              |
   |                              q_insert_task()
   |                              preempt_running_task()
   |                              switching_ = TRUE
   |                              |
   mrb_funcall()
     -> mrb_vm_exec()
        detects switching_
        returns early (CI leak!)
```

### After (修正後)

```
[system_gui thread]              [shell thread]
   |
   check switching_ == FALSE
   dispatching_event_ = TRUE
   |                              |
   |                              q_insert_task()
   |                              preempt_running_task()
   |                              sees dispatching_event_ == TRUE
   |                              switching_pending_ = TRUE  (遅延)
   |                              |
   mrb_funcall()
     -> mrb_vm_exec()
        switching_ is still FALSE
        executes normally
        pops CI frame properly ✓
   |
   dispatching_event_ = FALSE
   if switching_pending_:
       switching_ = TRUE  (遅延された切り替えを適用)
```

## 利点

1. **mrubyコアへの影響が最小限**
   - task.c と mruby.h のみ変更
   - 既存のロジックは維持

2. **アプリケーション側は単純**
   - begin_critical / end_critical で囲むだけ

3. **パフォーマンスへの影響が最小**
   - フラグチェックのオーバーヘッドのみ
   - タスク切り替えは遅延されるだけで失われない

4. **デバッグしやすい**
   - CI leak 検出コードはそのまま残る
   - 修正後も警告が出れば他の問題を検出可能

## テスト計画

1. シェルアプリを起動してクラッシュしないことを確認
2. シェルアプリを連続で起動・終了してレース条件が発生しないことを確認
3. 複数のアプリを同時に起動してテスト
4. CI stack leak 警告が出ないことを確認
5. マウス操作やキーボード入力が正常に動作することを確認

## 代替案との比較

### 代替案1: Mutex/Spinlock を使用

- **利点:** より強力な排他制御
- **欠点:** FreeRTOSとmrubyの協調スケジューリングが複雑になる
- **判断:** オーバーキル。フラグベースで十分

### 代替案2: タスク切り替えを完全に無効化

- **利点:** 実装が簡単
- **欠点:** リアルタイム性が損なわれる
- **判断:** パフォーマンスへの影響が大きすぎる

### 代替案3: CI leak を許容してリカバリー

- **利点:** mrubyコアの変更不要
- **欠点:** 根本的な解決にならず、リークが続く可能性
- **判断:** 一時的な回避策にはなるが、正しい解決策ではない

## 結論

**オプション1(本案)を推奨**します。理由:
- 根本原因を解決
- mrubyコアへの影響が最小限
- 実装がシンプルで理解しやすい
- パフォーマンスへの影響が最小
