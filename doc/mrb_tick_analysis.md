# mrb_tick 実装分析とレースコンディション問題

## 概要

`mrb_tick`は元々RaspiPicoのタイマー割り込みで呼ばれる想定だったが、family-mrubyでは**FreeRTOSタスクから定期的に複数VMの状態を外部変更**する実装になっており、これがレースコンディションを引き起こしている。

## 実装比較

### RaspiPico (元の想定)
```
タイマー割り込み → mrb_tick() 直接呼び出し
- 単一VM環境
- 割り込みコンテキストで実行
- 高精度タイミング
```

### family-mruby (現在)
```
FreeRTOS Task: mruby_tick_task (優先度5)
  └→ 10ms周期でvTaskDelay()
      └→ 全登録VM (最大16個) をループ
          └→ mrb_tick(vm[i]->mrb)  ★外部から状態変更★
```

ファイル: `lib/replace/picoruby-machine/ports/esp32/hal.c`

```c
static void mruby_tick_task(void* arg) {
    const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

    while (1) {
        vTaskDelay(tick_interval);  // 10ms待機

        // 全VMにtickを送信
        if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < MAX_MRB_VMS; i++) {
                if (g_tick_manager.vms[i].active && g_tick_manager.vms[i].mrb) {
                    // C funcall中、またはIRQ無効時はスキップ
                    if (MRB_C_FUNCALL_EXIT == g_tick_manager.vms[i].in_c_funcall &&
                        MRB_ENABLE_IRQ == g_tick_manager.vms[i].irq) {
                        mrb_tick(g_tick_manager.vms[i].mrb);  // ★問題箇所★
                    }
                }
            }
            xSemaphoreGive(g_tick_manager.mutex);
        }
    }
}
```

### 本流PicoRuby (最新)
```
本流task.cにはscheduler_lock機能があり、
同期実行中のタスク切り替えを防ぐ仕組みが実装されている。
```

ファイル: `/home/kishima/fmrb/investigate/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c`

## 問題の詳細: レースコンディション

### 発生メカニズム

[fix_shell_crash_race_condition.md](fix_shell_crash_race_condition.md)参照

```
[system_gui thread (優先度8)]       [mruby_tick_task (優先度5)]
  ├─ mrb_state *mrb を所有
  │
  ├─ dispatch_hid_event_to_ruby()
  │   └─ if (switching_) チェック
  │      → FALSE, 通過
  │
  │                                   vTaskDelay() 終了
  │                                   (優先度逆転で実行)
  │                                   │
  │   fmrb_task_delay_ms(1)           │
  │   (待機に入る)                    │
  │                              ─────┘
  │                              mrb_tick(system_gui->mrb)
  │                                 └─ tick_++
  │                                    └─ timeslice--
  │                                       └─ switching_ = TRUE ★設定★
  │
  │   (待機終了)
  │   mrb_funcall(mrb, self, "on_event", ...)
  │      └─ mrb_vm_exec()
  │         └─ switching_ == TRUE 検出
  │            └─ goto L_RETURN (早期リターン)
  │               └─ CI stackリーク発生！
```

### 根本原因

1. **所有権の侵害**
   - system_guiスレッドが所有する`mrb_state`
   - mruby_tick_taskが外部から`switching_`を変更

2. **TOCTOU脆弱性**
   - チェック時点では`switching_ == FALSE`
   - 使用時点では`switching_ == TRUE`に変化

3. **スレッド境界違反**
   - mruby VMは本来、所有スレッド内でのみ操作されるべき
   - 外部スレッドからの非同期な状態変更

## mrb_tick()の動作

ファイル: `lib/patch/picoruby-mruby/src/task.c:144-182`

```c
void mrb_tick(mrb_state *mrb)
{
  tick_++;  // グローバルtickカウンタ

  // ① タイムスライス満了チェック
  mrb_tcb *tcb = q_ready_;
  if (tcb && 0 < tcb->timeslice) {
    tcb->timeslice--;
    if (tcb->timeslice == 0) {
      switching_ = TRUE;  // ★タスク切り替えフラグ★
    }
  }

  // ② スリープ中タスクの起床チェック
  if ((int32_t)(wakeup_tick_ - tick_) < 0) {
    // WAITINGキュー内のタスクをチェック
    tcb = q_waiting_;
    while (tcb != NULL) {
      if (t->reason == TASKREASON_SLEEP &&
          (int32_t)(t->wakeup_tick - tick_) < 0) {
        // タスクを起床 (WAITING → READY)
        q_delete_task(mrb, t);
        t->status = TASKSTATUS_READY;
        q_insert_task(mrb, t);

        // プリエンプション判定
        preempt_running_task(mrb);  // ★switching_ = TRUE★
      }
    }
  }
}
```

### タスク切り替えが起きる2つのケース

#### Case 1: タイムスライス満了
```
tick=0  : task "main" 実行開始, timeslice=10
tick=1  : timeslice=9
...
tick=10 : timeslice=0 → switching_ = TRUE
```

#### Case 2: スリープタスクの起床
```
tick=100: task "timer" が sleep(5) 実行
          → WAITING queue, wakeup_tick=105
tick=105: mrb_tick() でチェック
          → "timer" を READY に移動
          → preempt_running_task()
          → switching_ = TRUE
```

## 本流の解決策: scheduler_lock

ファイル: `/home/kishima/fmrb/investigate/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c`

### scheduler_lock メカニズム

```c
/* L52-53 */
#define MRB_TASK_SCHEDULER_LOCK_MAX 255

/* L55-61 */
static inline void
task_check_scheduler_lock(mrb_state *mrb)
{
  if (mrb->task.scheduler_lock > 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR,
      "Cannot use asynchronous Task API during synchronous execution");
  }
}
```

すべての非同期Task API (`suspend`, `resume`, `terminate`等) が呼び出し時にチェック:
- `mrb_task_suspend()` - L1051
- `mrb_task_resume()` - L1063
- `mrb_task_terminate()` - L1075

### mrb_execute_proc_synchronously()

```c
/* L1129-1217 */
MRB_API mrb_value
mrb_execute_proc_synchronously(mrb_state *mrb, mrb_value proc_val,
                                mrb_int argc, const mrb_value *argv)
{
  // 1. スケジューラをロック
  mrb->task.scheduler_lock++;

  // 2. 一時的なタスクを作成
  mrb_task *t = task_alloc(mrb);
  task_init_context(mrb, t, proc);

  // 3. タスク切り替えなしで実行
  while (t->c.status != MRB_TASK_STOPPED) {
    t->state.result = mrb_vm_exec(mrb, ...);
  }

  // 4. スケジューラのロック解除
  mrb->task.scheduler_lock--;

  return result;
}
```

**効果**:
- `scheduler_lock > 0`の間は、非同期タスクAPIが使用不可
- `mrb_tick()`が`switching_`を設定しても無視される(TODO: 要確認)
- イベントハンドラ実行中にタスク切り替えが起きない

## family-mrubyでの対応方針

### 短期対応 (1-2週間)

[fix_shell_crash_race_condition.md](fix_shell_crash_race_condition.md)のオプション1を実装:

```c
// task.h に追加
void mrb_task_begin_critical(mrb_state *mrb);
void mrb_task_end_critical(mrb_state *mrb);

// app.c で使用
mrb_task_begin_critical(mrb);
mrb_funcall(mrb, self, "on_event", 1, event_hash);
mrb_task_end_critical(mrb);
```

### 中期対応 (1-2ヶ月) ★推奨★

本流の`scheduler_lock`と`mrb_execute_proc_synchronously()`を移植:

1. サブモジュール更新
2. 本流task.c採用
3. app.cを修正:
```c
mrb_value on_event_proc = mrb_iv_get(mrb, self, MRB_IVSYM(on_event));
mrb_execute_proc_synchronously(mrb, on_event_proc, 1, &event_hash);
```

### 長期対応 (3-6ヶ月)

Tickメッセージング方式に変更:

```c
// mruby_tick_task
static void mruby_tick_task(void* arg) {
    while (1) {
        vTaskDelay(tick_interval);

        // メッセージブロードキャスト (VM触らない)
        fmrb_msg_t tick_msg = {
            .type = FMRB_MSG_TYPE_TICK,
            .src_pid = PROC_ID_KERNEL
        };
        fmrb_msg_broadcast(&tick_msg, 0);
    }
}

// 各app_task
void app_task_main(void* arg) {
    while (1) {
        fmrb_msg_receive(ctx->app_id, &msg, TIMEOUT);

        switch (msg.type) {
            case FMRB_MSG_TYPE_TICK:
                mrb_tick(mrb);  // 自分のコンテキストで実行
                break;
        }
    }
}
```

**利点**:
- スレッド境界違反の完全解消
- マルチコア環境でも安全
- `fmrb_msg`インフラを活用

## 評価: mrb_tick実装の妥当性

### RaspiPico (割り込み実装)
✅ **妥当** - 単一VM、割り込みコンテキストで安全

### family-mruby (FreeRTOSタスク実装)
❌ **不適切** - スレッド境界違反、レースコンディション

### 本流PicoRuby (scheduler_lock実装)
✅ **妥当** - scheduler_lockにより保護

## 参考資料

### ソースコード
- family-mruby hal.c: `lib/replace/picoruby-machine/ports/esp32/hal.c:53-75`
- family-mruby task.c: `lib/patch/picoruby-mruby/src/task.c:144-182`
- 本流 task.c: `/home/kishima/fmrb/investigate/picoruby/.../task.c:403-449, 1129-1217`

### 関連ドキュメント
- [fix_shell_crash_race_condition.md](fix_shell_crash_race_condition.md) - 詳細分析
- [upstream_merge_plan.md](upstream_merge_plan.md) - 統合計画
- [current_patch_list.md](current_patch_list.md) - パッチ一覧

### コマンド
```bash
# 本流のscheduler_lock実装を確認
grep -n "scheduler_lock" /home/kishima/fmrb/investigate/picoruby/.../task.c

# family-mrubyのmrb_tick呼び出し箇所
grep -rn "mrb_tick" lib/replace/picoruby-machine/ports/esp32/hal.c
```

## まとめ

1. **現状**: family-mrubyの`mrb_tick`実装はスレッド境界違反によりレースコンディションを引き起こす
2. **短期対応**: クリティカルセクション保護 (オプション1)
3. **推奨対応**: 本流のscheduler_lock機能を統合
4. **根本対応**: Tickメッセージング方式への移行
