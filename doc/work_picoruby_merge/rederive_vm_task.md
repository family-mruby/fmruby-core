# 再導出設計: vm.c (D1) / task.c (D4) / task_hal (D7)

新 mruby (本家 mruby/mruby, pin f56d44e) は VM と task scheduler を大幅に書き換えた
(vm.c 976/480, task.c 380/189)。本書は D1/D4/D7 の各パッチを「なぜ必要か」から
「新実装のどこにどう再適用するか」まで具体化した再導出の設計図である。

検証方針 (依頼者): **Linux 版で検証**する。fmrb の Linux ビルドも ESP32 と同じ
FreeRTOS ベースの tick パス (SDL FreeRTOS / esp32_linux port) を通るため、Linux 検証で
同じ経路 (案D top/bottom-half) を確認できる。

前提知識:
- `#define switching_ (mrb->task.switching)` (mruby-task/include/task.h:165)。
  vm.c と task.c は同じ `mrb->task.switching` を見る。単一 volatile word なので cross-thread set は安全。
- mrbgem.rake が `MRB_USE_TASK_SCHEDULER` を全 build に define → vm.c と HAL port の両 TU で task フィールド有効。
- 新 HAL 界面 (mruby-task/include/task_hal.h): `mrb_hal_task_init/final`, `mrb_hal_task_idle_cpu`,
  `mrb_hal_task_sleep_us`, `mrb_task_enable_irq/disable_irq`, および timer が呼ぶ `mrb_tick(mrb)`。
  upstream は「timer が各 VM に mrb_tick() を呼ぶ」設計 (posix port は SIGALRM=同一スレッド)。

---

## D1: src/vm.c (tick task-switch 安全化) → **不要化・upstream 採用 (パッチ撤去)**

### 意図 (なぜ必要だったか)

async な task-switch (tick 割込みが `mrb->task.switching` を立てる) を、VM が「安全な点」でしか
honor しないようにする。C 関数が mrb_funcall 等で VM に再入している最中 (ci に cci>0 がある) に
yield すると、中間の C フレームを保存/復元できず、`mrb->c->ci` の深さがずれて
"task context corrupted: no proc on resume"、さらに mrb->jmp が宙吊りになり後続の longjmp が
死んだフレームに飛ぶ。memory: project_mruby_tick_disabled_for_rubykaigi.md。

### 我々の旧パッチ (要点)

- `mrb_task_yield_ok(mrb)`: `mrb->c->ci` から `cibase` まで走査し `cci>0` があれば FALSE。
- `RETURN_IF_TASK_STOPPED` 改変: STOPPED か (switching && yield_ok) の時に `mrb->jmp=prev_jmp` を
  復元して early return。

### 新 upstream 実装 (src/vm.c 1605-1680) — **我々の厳密な上位互換**

upstream は同じ問題を独自に解決済み (issues #6862/#6863/#6864/#6868/#6887):

- `task_across_c_boundary(mrb)`: `ci > cibase` を走査し `cci>0` で TRUE
  (cibase は本 mrb_vm_exec の entry frame として正しく除外。**我々の `>=` より正確**)。
- `RETURN_IF_TASK_STOPPED`:
  ```c
  if (((mrb)->task.switching && (mrb)->c != (mrb)->root_c &&
       !(mrb)->exc && !(mrb)->gc.iterating && !task_across_c_boundary(mrb)) ||
      (mrb)->c->status == MRB_TASK_STOPPED) {
    (mrb)->jmp = prev_jmp;
    return mrb_nil_value();
  }
  ```
- 我々がカバーしていなかったケースまで追加でカバー:
  - `!mrb->exc`: 例外送出中の switch を延期 (L_RAISE で handler に飛んだ直後に yield すると
    OP_EXCEPT 前に抜けて rescue 不能になるバグ #6887)。
  - `!mrb->gc.iterating`: ObjectSpace walk 中の switch を延期 (#6862)。
  - `mrb->c != mrb->root_c`: root context では honor しない (#6887, UI-loop-on-root パターン)。
  - `mrb->jmp = prev_jmp` 復元も同じ (#6863)。

### 結論・作業

- **我々の tick 修正は upstream 化された** (fmrb の ESP32 検証済み修正が本家 issue として取込)。
- **D1 は完全に撤去**。lib/patch/picoruby-mruby/lib/mruby/src/vm.c を**削除**し upstream 版を使う。
  (Rakefile setup の vm.c コピー行も削除 → Rakefile/pin 切替フェーズで実施)
- 検証観点: 新 vm.c の condition が我々の全シナリオ (mruby_tick task 破壊, RubyKaigi デモ) を
  カバーするか実機/Linux で確認。理論上は上位互換なので回帰は無い想定。

---

## D4: mruby-task/src/task.c (案D top/bottom-half tick split) → **一部撤去・一部再導出**

### 意図

fmrb の tick 源は FreeRTOS の**別タスク (別スレッド)**。upstream の想定 (timer が mrb_tick を
直接呼ぶ) をそのままやると、mrb_tick は task queue を書き換える (sleeping→ready 移動等) ため、
VM を走らせているスレッド以外からの queue 変更で **ci->proc corruption** が起きる。

案D (top-half / bottom-half 分割):
- **top-half** (FreeRTOS timer タスク, 別スレッド): `mrb->task.switching = TRUE` を立て、
  per-VM の pending-tick カウンタを増やすだけ。mrb_tick は呼ばない。
- **bottom-half** (VM 自スレッド = scheduler ループ): 溜まった pending 回数だけ `mrb_tick(mrb)` を適用。
  queue 操作が単一スレッドに閉じる。
- switching は volatile 単語なので cross-thread set 安全。VM は毎 OP 境界で switching を見て
  scheduler へ戻る (D1=upstream の RETURN_IF_TASK_STOPPED) ので、CPU-bound task も確実に preempt される。

### 我々の旧パッチ構成 (task.c)

1. `extern uint32_t mrb_hal_task_take_pending_ticks(mrb_state *mrb);` (HAL port 提供)。
2. `MRB_API void mrb_task_request_switch(mrb_state *mrb)` { switching_ = TRUE; } (top-half から呼ぶ)。
3. bottom-half: `mrb_task_run` のループと `mrb_task_run_once` で
   `pending = take_pending_ticks(mrb); for(i<pending) mrb_tick(mrb);`。
4. `mrb_task_reset_context` で stack を nil クリア。

### 新 upstream task.c での差分

- **(4) stack nil クリアは upstream 化済** (task.c l.124-126 mark 経路, l.300-303 init 経路で
  `SET_NIL_VALUE`)。→ **撤去** (再適用不要)。
- scheduler ループは `task_run_body` (static, l.564-) に再編。`mrb_task_run` は
  `mrb_protect_error` でこれを包む。`mrb_task_run_once` は単発版。
- queue 排他は `mrb_task_excl_enter/exit(mrb)` (IRQ 排他)。upstream は mrb_tick を「IRQ」として扱う
  (コメント: "q_ready_ is mutated by the mrb_tick IRQ")。fmrb では top/bottom-half がこの IRQ を代替。

### 再導出方針 (task.c)

- **(1)(2) を再追加**: `extern ... mrb_hal_task_take_pending_ticks`、必要なら `mrb_task_request_switch`
  (ただし port は task.h の `switching_` マクロで `mrb->task.switching=TRUE` を直接立てられるため、
  request_switch は任意。pending カウンタ増加が本質)。
- **(3) bottom-half を `task_run_body` の `while(1)` ループ先頭** (l.573 `t = q_ready_;` の直前) に挿入:
  ```c
  /* fmrb 案D bottom-half: apply ticks accumulated cross-thread by the FreeRTOS
     timer on the VM's own thread, where task-queue access is single-threaded. */
  {
    uint32_t pending = mrb_hal_task_take_pending_ticks(mrb);
    while (pending--) mrb_tick(mrb);
  }
  ```
  - ここに置く理由: CPU-bound task が switching で scheduler に戻ると必ずこのループ先頭を通る。
    idle 経路 (`mrb_hal_task_idle_cpu`) だけに置くと busy task が tick されず preempt されないので不可。
  - `mrb_task_run_once` にも同等の bottom-half が要るか要確認 (単発 scheduler を使う経路があれば)。
- **(4) は追加しない** (upstream 済)。
- 注意: `mrb_tick` 自身が `mrb_task_excl_enter/exit` を取るなら二重取得に注意。bottom-half は
  排他の外 (VM スレッド) で呼ぶ。upstream の mrb_tick 内排他実装を確認して整合を取る。

### D4 付随: mruby-task/mrbgem.rake (HAL auto-load)

- 旧パッチ: upstream の「hal-*-task gem 自動選択/エラー」ロジックを削除し「ports が提供」に置換。
- 新 upstream mrbgem.rake は **`spec.build.effective_ports` / `conf.ports` 方式**で port 選択
  (posix/glib/win を内蔵)。hal-*-task gem 依存は既に廃止済 → 我々の旧削除パッチは陳腐化。
- fmrb の要件: posix SIGALRM port を使わず **FreeRTOS port** を使う。実装選択肢 (要ビルド時確定):
  - (案a) fmrb の FreeRTOS task_hal 実装を **picoruby-machine 側 (esp32_linux/hal_freertos.c, B1)** で提供し、
    mruby-task の port は選択しない (または no-op port)。effective_ports の解決規則を要確認。
  - (案b) fmrb 独自 port `ports/<name>/task_hal.c` を追加し `conf.ports` で選択。
  - どちらでも top-half (switching+pending) と `mrb_hal_task_take_pending_ticks` を提供する点は同じ。
- → **port 選択の具体はビルド配線時に確定** (effective_ports の挙動確認が必要)。

---

## D7: task_hal.c (FreeRTOS port, top-half の実体) → **再導出**

- 旧: hal-posix-task/src/task_hal.c を SIGALRM 撤去版に置換。新location = mruby-task/ports/posix/task_hal.c
  (新版も SIGALRM 使用のまま)。
- fmrb 実装 (Linux/ESP32 共通の FreeRTOS tick):
  - `mrb_hal_task_init(mrb)`: FreeRTOS timer タスク/タイマ生成 (周期 = MRB_TICK_UNIT ms)。
  - timer コールバック (別スレッド): `mrb->task.switching = TRUE; pending_ticks[vm]++;` のみ (mrb_tick 呼ばない)。
  - `mrb_hal_task_take_pending_ticks(mrb)`: pending を返し 0 クリア (VM スレッドから呼ばれる)。
  - `mrb_hal_task_idle_cpu(mrb)`: FreeRTOS の短い vTaskDelay 等 (busy-wait 回避)。
  - `mrb_hal_task_sleep_us(mrb, usec)`: vTaskDelay 換算。
  - `mrb_task_enable_irq/disable_irq`: tick タスク優先度制御 or クリティカルセクション。
- 既存の fmrb 資産: picoruby-machine の esp32_linux/hal_freertos.c, ports/esp32/machine.c,
  ports/posix/hal.c が FreeRTOS tick を持つ (B1)。これらと重複しないよう統合先を一本化する。
- pending カウンタの型/格納: 旧設計は per-VM。単一 VM 前提なら static でも可。マルチ VM 対応なら
  mrb ごとに保持 (mrb->ud か HAL 内テーブル)。

---

## 依存関係と実施順

1. **D1**: vm.c パッチ撤去 (upstream 採用)。最初にやるのが安全 (差分が減る)。
2. **D4 task.c**: stack-clear 撤去 + bottom-half を task_run_body へ。
3. **D7 task_hal**: FreeRTOS top-half 実装 (B1 machine と統合)。
4. **mrbgem.rake / port 選択**: effective_ports の挙動を見てビルド配線時に確定。
5. 検証: Linux ビルド → `Task` を使う .rb で preempt/sleep/wake が正しいか + tick 破壊が再発しないか。
   実機 ESP32 は依頼者。ただし今回は Linux が同一 tick パスのため Linux で主検証可能。

## 未確定・要確認 (実装時)

- effective_ports がカスタム port / no-op をどう扱うか (D4 mrbgem.rake の port 選択)。
- mrb_tick 内の `mrb_task_excl_enter/exit` と bottom-half の排他整合 (二重取得の回避)。
- `mrb_task_run_once` 経路でも bottom-half が要るか。
- pending カウンタのマルチ VM 対応要否 (fmrb は基本単一 VM)。
- global_mrb (compiler Option A) の設定タイミングと task 経路の干渉が無いか。
