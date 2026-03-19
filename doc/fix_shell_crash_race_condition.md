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

---

## 補足: レースコンディションの詳細分析

### タスクの種類と階層構造

このシステムには**2種類のタスク**が存在し、混同しやすいため整理します。

#### 1. FreeRTOSタスク（OSレベル）

各mrubyアプリケーションは独立したFreeRTOSタスクとして動作:

- `system_gui` FreeRTOSタスク (優先度8)
- `shell` FreeRTOSタスク (優先度5)
- `mruby_tick_task` FreeRTOSタスク (優先度5)

#### 2. mrubyタスク（VM内の協調マルチタスク）

**各mruby VM内部で**複数のRubyタスクが協調動作:

- `mrb_tcb` (Task Control Block) で管理
- キュー: `q_ready_`, `q_waiting_`, `q_suspended_`, `q_dormant_`
- `switching_` フラグでタスク切り替えを制御

### 階層構造の図解

```
┌──────────────────────────────────────────────────────┐
│ FreeRTOS Task: system_gui (優先度8)                  │
│  ┌─────────────────────────────────────────────┐    │
│  │ mruby VM (独立インスタンス)                  │    │
│  │  ┌──────────────────────────────────────┐   │    │
│  │  │ mruby task 1 (main)                  │   │    │
│  │  │ mruby task 2 (event handler)         │   │    │
│  │  │ mruby task 3 (timer)                 │   │    │
│  │  └──────────────────────────────────────┘   │    │
│  │                                              │    │
│  │  mrb->task.switching = TRUE/FALSE           │    │
│  │  mrb->task.tick = グローバルtickカウンタ    │    │
│  │  q_ready_ = [task1(RUNNING), task2, ...]   │    │
│  └─────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ FreeRTOS Task: shell (優先度5)                       │
│  ┌─────────────────────────────────────────────┐    │
│  │ mruby VM (別の独立インスタンス)              │    │
│  │  ┌──────────────────────────────────────┐   │    │
│  │  │ mruby task 1 (shell main)            │   │    │
│  │  │ mruby task 2 (input handler)         │   │    │
│  │  └──────────────────────────────────────┘   │    │
│  │                                              │    │
│  │  mrb->task.switching = TRUE/FALSE (独立)    │    │
│  │  mrb->task.tick = グローバルtickカウンタ    │    │
│  └─────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ FreeRTOS Task: mruby_tick_task (優先度5)             │
│  - 全VMの mrb_tick() を定期的に呼び出す              │
│  - 各VMのmrubyタスクスケジューリングに影響          │
│  ★問題: 外部から他のVMの内部状態を変更★           │
└──────────────────────────────────────────────────────┘
```

### mruby_tick_task による tick 処理の流れ

#### hal.c の tick タスク

```c
// lib/replace/picoruby-machine/ports/esp32/hal.c:53-75
static void mruby_tick_task(void* arg) {
    const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

    while (1) {
        vTaskDelay(tick_interval);  // MRB_TICK_UNIT (例: 10ms) 待機

        // ★全登録VMに対してtickを送信★
        if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < MAX_MRB_VMS; i++) {
                if (g_tick_manager.vms[i].active && g_tick_manager.vms[i].mrb) {
                    // C->Ruby関数呼び出し中、またはIRQ無効時はスキップ
                    if (MRB_C_FUNCALL_EXIT == g_tick_manager.vms[i].in_c_funcall &&
                        MRB_ENABLE_IRQ == g_tick_manager.vms[i].irq) {
                        mrb_tick(g_tick_manager.vms[i].mrb);  // ★他VMの内部状態を変更★
                    }
                }
            }
            xSemaphoreGive(g_tick_manager.mutex);
        }
    }
}
```

#### task.c の mrb_tick() 関数

```c
// components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c:143-182
void mrb_tick(mrb_state *mrb)
{
  tick_++;  // グローバルtickカウンタをインクリメント

  // ① タイムスライス満了チェック
  mrb_tcb *tcb = q_ready_;
  if (tcb && 0 < tcb->timeslice) {
    tcb->timeslice--;
    if (tcb->timeslice == 0) {
      switching_ = TRUE;  // ★タスクスイッチフラグON★
    }
  }

  // ② スリープ中タスクの起床チェック
  if ((int32_t)(wakeup_tick_ - tick_) < 0) {
    int task_switch = 0;
    wakeup_tick_ = tick_ + (1 << 16);

    // WAITINGキュー内のスリープタスクをチェック
    tcb = q_waiting_;
    while (tcb != NULL) {
      mrb_tcb *t = tcb;
      tcb = tcb->next;
      if (t->reason != TASKREASON_SLEEP) continue;

      // 起床時刻に到達したタスクをREADYに移動
      if ((int32_t)(t->wakeup_tick - tick_) < 0) {
          q_delete_task(mrb, t);           // WAITINGから削除
          t->status  = TASKSTATUS_READY;
          t->reason = TASKREASON_NONE;
          q_insert_task(mrb, t);           // READYキューに挿入（優先度順）
          task_switch = 1;
      } else if ((int32_t)(t->wakeup_tick - wakeup_tick_) < 0) {
        wakeup_tick_ = t->wakeup_tick;
      }
    }

    // タスクが起床した場合、プリエンプション判定
    if (task_switch) preempt_running_task(mrb);
  }
}
```

#### preempt_running_task() の動作

```c
// task.c:130-136
inline static void
preempt_running_task(mrb_state *mrb)
{
  // READYキュー内にRUNNINGタスクがある = より高優先度のタスクが起床した
  for (mrb_tcb *t = q_ready_; t != NULL; t = t->next) {
    if (t->status == TASKSTATUS_RUNNING) {
      switching_ = TRUE;  // ★プリエンプション発生★
    }
  }
}
```

### 具体的なレースシーケンス

```
時刻 | system_gui (FreeRTOS Task 優先度8)    | mruby_tick_task (FreeRTOS Task 優先度5)
-----|----------------------------------------|------------------------------------------
     | [system_gui VM内の状態]                |
     | - mruby task "main" が RUNNING        |
     | - timeslice = 1                        |
     | - switching_ = FALSE                   |
-----|----------------------------------------|------------------------------------------
T0   | dispatch_hid_event_to_ruby() 開始     |
T1   | if (switching_) チェック → FALSE, 通過|
T2   | mrb_callinfo *ci_before = mrb->c->ci   |
     |                                        |
T3   |                                        | vTaskDelay() 終了
     |                                        | (優先度5 < 8 なので実行はまだ)
     |                                        |
T4   | fmrb_task_delay_ms(1) などで待機      | ← system_guiが待機に入った隙に実行開始
     |                                        |
T5   |                                        | mrb_tick(system_gui->mrb) 実行
T6   |                                        |   tick_++ (system_gui VMのtick)
T7   |                                        |   timeslice-- → 0
T8   |                                        |   switching_ = TRUE ★設定★
T9   |                                        |   (system_gui VM内部の状態を外部から変更)
T10  |                                        | vTaskDelay() に戻る
     |                                        |
T11  | 待機終了、実行再開                     |
T12  | mrb_funcall(mrb, self, "on_event", 1, event_hash)
T13  |   -> mrb_vm_exec() 開始                |
T14  |   各VM命令実行前に switching_ チェック|
T15  |   switching_ == TRUE ★検出！★         |
T16  |   goto L_RETURN (早期リターン)         |
T17  |   ← cipush でpushしたフレームがpopされず|
T18  | CI stack leak! (delta=48 bytes)        |
```

### 根本的な問題点

1. **スレッド境界の違反**
   - mruby VMは本来、所有するFreeRTOSタスク内でのみ操作されるべき
   - しかし`mruby_tick_task`が他のVMの内部状態(`switching_`)を変更している

2. **TOCTOU (Time-of-Check to Time-of-Use) 脆弱性**
   - `if (switching_)` でチェック後、`mrb_funcall()` 実行前に状態が変わる
   - チェックと使用の間にアトミック性がない

3. **優先度逆転の可能性**
   - `mruby_tick_task` (優先度5) が `system_gui` (優先度8) の状態を変更
   - 通常は優先度8が先に実行されるが、待機中の隙に割り込まれる

### tick処理でタスクスイッチが起きる2つのパターン

#### パターン1: タイムスライス満了

```c
[tick 0]  system_gui VM内 mruby task "main" 実行開始、timeslice=10
[tick 1]  timeslice=9
[tick 2]  timeslice=8
...
[tick 9]  timeslice=1
[tick 10] timeslice=0 → switching_ = TRUE ★スイッチ発生★
```

#### パターン2: スリープ中タスクの起床

```
[tick 100] system_gui VM内 mruby task "timer" が sleep(5) 実行
          → status = WAITING, reason = SLEEP, wakeup_tick = 105
          → WAITINGキューに移動

[tick 101-104] system_gui VM内 mruby task "main" 実行中...

[tick 105] mrb_tick(system_gui->mrb) 実行
          ① wakeup_tick(105) <= tick(105) → チェック開始
          ② "timer" タスク発見: wakeup_tick(105) <= tick(105)
          ③ "timer" を WAITING → READY に移動
          ④ q_insert_task() で優先度順に挿入
             - 仮に "timer" の優先度が高い場合、q_ready_ の先頭に挿入
          ⑤ preempt_running_task() 実行
          ⑥ q_ready_ に RUNNING タスク("main")があるため
             switching_ = TRUE ★スイッチフラグON★
```

### READYキューの状態遷移例

起床前:
```
q_ready_:  [main(RUNNING, pri=10)] → [event_handler(READY, pri=5)] → NULL
q_waiting_: [timer(SLEEP, pri=8, wakeup=105)] → NULL
```

起床後（timerの優先度8が、mainの優先度10より低い場合）:
```
q_ready_:  [main(RUNNING, pri=10)] → [timer(READY, pri=8)] → [event_handler(READY, pri=5)] → NULL
                                      ↑新たに挿入
→ preempt_running_task() が main(RUNNING) を検出
→ switching_ = TRUE (優先度順でmainが先頭だが、次回のスケジューリングでtimerが実行される可能性)
```

起床後（timerの優先度12が、mainの優先度10より高い場合）:
```
q_ready_:  [timer(READY, pri=12)] → [main(RUNNING, pri=10)] → [event_handler(READY, pri=5)] → NULL
            ↑先頭に挿入              ↑RUNNINGタスクが存在

→ preempt_running_task() が main(RUNNING) を検出
→ switching_ = TRUE (次回スケジューリングでtimerに切り替え)
```

### オプション1（本案）がこの問題を解決する理由

1. **クリティカルセクションの保護**
   - `dispatching_event_` フラグで `mrb_funcall()` 実行中であることを明示
   - この間は `switching_` を直接設定せず、`switching_pending_` に遅延

2. **外部からの状態変更を許容**
   - `mruby_tick_task` からの `mrb_tick()` 呼び出しは継続
   - ただし、クリティカルセクション中は影響を遅延

3. **アトミック性の確保**
   - チェック(`if (switching_)`) から使用(`mrb_funcall()`) まで状態が変わらない
   - `mrb_task_end_critical()` で遅延された切り替えを適用

### より根本的な設計改善案（将来の検討事項）

現在の修正案（オプション1）は**最小限の変更で問題を解決**しますが、より根本的には以下の改善が考えられます:

#### 案A: Tick通知をメッセージで送る

各VMが自分でtickを処理:

```c
// mruby_tick_task
static void mruby_tick_task(void* arg) {
    while (1) {
        vTaskDelay(tick_interval);

        // 全VMにTICKメッセージを送信（外部からVMを触らない）
        fmrb_msg_t tick_msg = {
            .type = FMRB_MSG_TYPE_TICK,
            .src_pid = PROC_ID_KERNEL
        };
        fmrb_msg_broadcast(&tick_msg, 0);  // non-blocking
    }
}

// 各app_task_main
void app_task_main(void* arg) {
    while (1) {
        fmrb_msg_receive(ctx->app_id, &msg, TIMEOUT);

        switch (msg.type) {
            case FMRB_MSG_TYPE_TICK:
                mrb_tick(mrb);  // 自分のコンテキストで実行
                break;
            // ...
        }
    }
}
```

**利点**:
- 外部からVMの内部状態を触らない（スレッドセーフ）
- レースコンディション完全解消
- tick精度が保たれる

**欠点**:
- メッセージオーバーヘッド
- 実装の変更範囲が大きい

#### 案B: 各VMスレッド内でtickカウント

```c
void app_task_main(void* arg) {
    uint32_t last_tick = fmrb_task_get_tick_count();

    while (1) {
        fmrb_msg_receive(ctx->app_id, &msg, 10);  // 10ms timeout

        // Tick更新（自分で）
        uint32_t now = fmrb_task_get_tick_count();
        uint32_t elapsed = now - last_tick;
        for (uint32_t i = 0; i < elapsed; i++) {
            mrb_tick(mrb);  // 自分のVMのみ更新
        }
        last_tick = now;

        // 処理...
    }
}
```

**利点**:
- 完全に独立、スレッドセーフ
- `mruby_tick_task` 不要

**欠点**:
- メッセージ待ちの間tickが進まない
- タイムアウト精度低下の可能性

### 結論（更新）

**短期対応**: オプション1（本ドキュメントの修正案）を実装
- レースコンディションを確実に解消
- 最小限の変更
- 既存の設計を維持

**長期検討**: 案A（Tick通知メッセージ）への移行を検討
- より根本的な解決
- スレッド境界の明確化
- 将来的なマルチコア対応も視野