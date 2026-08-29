# 解説 2: FreeRTOS Emscripten port の詳細

> 状態: 完了 (P4 時点の実装を記述) | 更新: 2026-08-29 | 外部説明用の解説群 その 2

本移植の核心である wasm/port/ (4 ファイル、約 900 行) を、実行の仕組みが
追える粒度で解説する。カーネル本体 (wasm/vendor/freertos/ の tasks.c /
queue.c / list.c / stream_buffer.c) は ESP-IDF v5.5.4 同梱のものを
**無改変**で使っており、書いたのは port 層だけである。

## ファイル構成

| ファイル | 行数 | 役割 |
|---|---|---|
| port.c | 537 | スレッドモデル・tick 供給・タスク削除。核心 |
| portmacro.h | 166 | カーネルとの接続点 (マクロ・型)。設計の要約コメント付き |
| FreeRTOSConfig.h | 155 | カーネル設定。Linux sim の実効値に揃えてある |
| wait_for_event.c/h | 91 | 待ち合わせ部品 (上流 POSIX port から流用、実質無改変) |

## スレッドモデル: 「乗り物は Worker、運転権はカーネル」

### タスク生成 (pxPortInitialiseStack)

カーネルは xTaskCreate でスタックブロックを確保し、port に初期化を頼む。
通常の port はここに CPU レジスタの初期文脈を書くが、この port は違う:

1. ブロックの先頭に per-task 記録 `Thread_t` (pthread ハンドル、開始関数、
   死亡フラグ、専用イベント、スタックサイズ) を刻む。
2. `pthread_create` で**本物のスレッド (= Web Worker)** を 1 本作る。
   スタックサイズは xTaskCreate の指定値 (下限 64KB にクランプ。
   `prvGetStackBytes`)。生まれたスレッドは `prvWaitForStart` で即座に
   自分のイベントを待って眠る。
3. カーネルには Thread_t の直下を「スタックトップ」として返す。以後
   TaskHandle_t (TCB の先頭は pxTopOfStack) から `prvGetThreadFromTask` で
   Thread_t を逆引きできる。

つまり**カーネルのスタックブロックは Thread_t の置き場でしかなく、タスクの
コードは Emscripten が確保した pthread スタックの上で走る**。上流 POSIX port
と同じ構図で、これがスタック計測が無意味になる理由でもある。

### 文脈切替 (vPortYield → prvSwitchThread)

```
vPortYield():
  prvCatchUpTicks()            … critical section の外で時計を最新化
  enter critical
  vTaskSwitchContext()         … カーネルが次のタスクを決める (無改変の本体)
  prvSwitchThread(次, 自分):
    critical 入れ子数を自分のスタックに退避
    event_signal(次の Thread_t)  … 次のタスクの Worker を起こす
    event_wait(自分の Thread_t)  … 自分は眠る (Atomics.wait で本当に眠る)
    復帰したら入れ子数を戻す
  exit critical
```

critical section の入れ子数はグローバル 1 個だが「今走っているタスクの
持ち物」なので、切替時に各スレッドのスタックへ退避して持ち回る。
wait_for_event の「sticky なフラグ」(signal が wait より先に来ても取り
こぼさない) が、この手渡しの安全性を担っている。

### 割り込み系マクロの退化 (portmacro.h)

ISR が存在しないので、portSET_INTERRUPT_MASK 系はすべて同じ入れ子
カウンタに落ち、portMUX_TYPE (スピンロック) は「型だけ残した int」に
なる。カウンタ自体は無意味ではなく、**「critical section 中は tick を
供給しない」の判定に使う** (実機で「割り込み禁止中に tick 割り込みは
来ない」のと同じ規則)。

もう 1 つの実機との違いは portSTACK_TYPE を uint8_t にしたこと (ESP-IDF と
同じ)。上流 POSIX port は unsigned long で、スタック指定が黙って 8 倍に
なる癖があるが、それを持ち込まず「.app.toml の task_stack_kb が実機と
同じ意味を持つ」ようにしてある。

## tick 供給: 追いつき方式 (prvCatchUpTicks)

```
due  = (現在時刻 - 起動時刻) / 1ms     … 本来供給されているべき tick 総数
while (供給済み < due) { xTaskIncrementTick(); }
```

- **1 tick ずつ**呼ぶ (まとめて飛ばさない)。カーネルの遅延リストは
  1 tick 刻みでしか歩けないため。
- 基準が起動時刻なので、CPU 占有で遅れても次の機会に全部追いつく。
  ずれは蓄積しない。
- critical section 中・再入時は何もしない。

呼び出し点は 2 つだけ:

1. **vPortYield() の入口** — ブロックしようとするタスクが最新の時計を見る。
2. **アイドルフック (esp_vApplicationIdleHook)** — 全タスクが眠っている
   ときに時計を進められる唯一の場所。1ms 寝てから追いつき、切替要求が
   出たら taskYIELD する。ESP-IDF の tasks.c はこの名前のフックを無条件に
   呼ぶため、configUSE_IDLE_HOOK は 0 のままフックが 1 本になる。

観測用に「1 回の追いつきで進めた最大 tick 数」「追いつき呼び出し回数」を
数えており (ulPortGetMaxTickBurst 等)、PoC の検算に使った。

## タスク削除: pthread_cancel を使わない他殺

上流 POSIX port は vTaskDelete(他人) に pthread_cancel を使うが、
Emscripten では信用できない。この port は自前の性質を利用する:

> **走っていないタスクのスレッドは、必ず prvSuspendSelf() の event_wait で
> 眠っている。**

だからカーネルが TCB を片付ける瞬間 (vPortCleanUpTCB) に、

1. TLS 削除コールバックを回し (ESP-IDF 拡張。スロット配列の後半に
   コールバックが入る配置も踏襲)、
2. 相手の xDying を立てて event_signal で起こし、
3. pthread_join で回収する。

起こされた側は event_wait から戻った直後に xDying を見て、**カーネルの
状態に一切触れずに** pthread_exit する。join はカーネルがスタックブロック
(= Thread_t の置き場) を解放する前に完了させる。自殺
(vTaskDelete(NULL)) の場合は prvSwitchThread 内の xDying 検査で抜けるので、
join は取りこぼしの回収になる。

ただし「CPU を握ったままの相手」は眠っていないので、この方法でも殺せない。
それが P2 (自給タイムスライス) が必要な理由である。

## FreeRTOSConfig.h の要点

- 値は Linux sim の実効値に揃え、「実機向けに書かれたコードが同じ意味で
  動く」ことを優先。configUSE_PREEMPTION は 1 のまま — 失われるのは
  「非同期 tick による剥がし」だけで、API 境界での優先度切替は生きている。
- configCHECK_FOR_STACK_OVERFLOW=0 (カーネルの番犬は誰も書かないメモリを
  見張ることになるため)。溢れは Emscripten の検査に任せる。
- configUSE_TIMERS=0 (棚卸しで使用ゼロ。timers.c は vendor もしていない)。
- TLS は削除コールバック有効時に配列長が 2 倍になる ESP-IDF 配置を再現。

## 実行を一望する: 起動から描画まで

1. ページが core_web.js を読み、Emscripten が Worker 上で main() を呼ぶ
   (PROXY_TO_PTHREAD)。
2. main() (wasm/backend/main_wasm.c) は app_main を包んだタスクを 1 本
   作って vTaskStartScheduler() する。ESP-IDF では app_main が最初から
   タスク内にいるので、その形をここで再現している。
3. スケジューラ開始 (xPortStartScheduler): 時計の基準を取り、最初の
   タスクの Worker を起こし、main() のスレッドは終了イベントを待って眠る。
4. 以後は boot.c → 各タスク生成 → カーネル VM・デスクトップ spawn と、
   実機/Linux と同じ道をたどる。タスクが増えるたび Worker が増え、
   走るのは常に 1 本、時計は yield とアイドルで進む。
