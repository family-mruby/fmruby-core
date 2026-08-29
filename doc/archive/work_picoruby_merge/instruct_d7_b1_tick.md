# 追加指示: D7+B1 (FreeRTOS tick top-half / picoruby-machine 再導出)

rederive_vm_task.md の D7/B1 に対する追加指示。依頼者の決定事項と、レビューで確定した
設計方針をまとめる。着手前に本書を読み、実装はこの方針に従うこと。

## 依頼者の決定事項

1. **global_mrb の並行コンパイル対策は mutex 排他でよい**。
   並行コンパイルは実際にはほとんど起きないため、専用VM固定などの大掛かりな設計は不要。
   各コンパイル呼び出し点で mutex を取得し「global_mrb 設定 → コンパイル → 復元」を
   ワンセットで行う。性能は考慮しなくてよい。
2. **tick の発生源は割り込みではなく FreeRTOS タスク (従来どおり)**。
   本家 PicoRuby は OS なしマイコンを想定してタイマ割り込みで tick しているが、
   fmrb は RTOS 上で動くため割り込みは過剰であり、RTOS タスクを tick 源とした経緯がある。
   この方針は維持する。upstream の割り込みモデルとの整合の取り方は次節。

## なぜ RTOS タスク tick + 案D で安定稼働できるのか (設計の根拠)

### upstream の contract の本質は「割り込み」ではない

upstream の mruby-task が mrb_tick に要求している契約をコードから読み取ると、
「割り込みで呼ぶこと」ではなく **「VM スレッドの task queue 操作と真に並行実行されないこと」**である。

- ベアメタル: mrb_tick はタイマ ISR。VM 側は mrb_task_excl_enter/exit (= IRQ 禁止) で
  自分の queue 操作区間から ISR を締め出す。ISR と VM は同一 CPU 上の逐次的な割り込み
  なので、「同時に」queue を触ることはない。
- posix port: mrb_tick は SIGALRM ハンドラ。シグナルは VM スレッド自身を中断して
  同スレッド上で走るため、これも逐次的であり真の並行はない。
- つまり upstream の全 port は「mrb_tick と VM の queue 操作は 1 本の実行流の上で
  逐次化されている」という不変条件の上に成り立っている。

fmrb の旧構成 (FreeRTOS 別タスクから mrb_tick を直接呼ぶ) はこの不変条件を破っていた。
ESP32 はデュアルコアなので timer タスクと VM タスクが物理的に同時に queue を書き、
ci->proc corruption が起きた。**問題は「タスクか割り込みか」ではなく「逐次化されて
いるか」**である。

### 案D はこの不変条件をより強い形で回復する

案D の top/bottom-half 分割後、スレッド間で共有される状態は次の 2 ワードだけになる。

- `mrb->task.switching`: top-half が TRUE を書くだけの単方向フラグ (volatile 1 word)。
- pending tick カウンタ: top-half が増加、VM スレッドが読み取り+0クリア。
  **ここだけが真の read-modify-write 競合なので、クリティカルセクションまたは
  atomic exchange で実装すること (必須。レビュー指摘事項)**。

mrb_tick の実行と task queue の全操作は VM 自身のスレッド (task_run_body ループ先頭の
bottom-half) に閉じる。upstream が「IRQ 禁止区間で保護」しているものを、fmrb は
「そもそも単一スレッドに閉じる」ことで満たす。これは upstream の契約の上位互換であり、
割り込みモデルより強い保証になる。

### 安定稼働の性質 (何が保証され、何が保証されないか)

保証されるもの:

- **tick の取りこぼしなし**: VM が Ruby 実行中でも C 呼び出し中でも、pending カウンタが
  蓄積するので tick の「回数」は失われない。ブロッキング呼び出しから戻った時に
  まとめて適用され、sleep の時間基準はドリフトしない。
- **CPU-bound タスクの preempt**: switching は VM の全 OP 境界で検査される
  (upstream 化された RETURN_IF_TASK_STOPPED)。busy loop する Ruby コードも
  1 命令以内にスケジューラへ戻り、ループ先頭の bottom-half で tick が適用される。
- **wakeup の因果順序**: sleep しているタスクの起床は必ず VM スレッド上の mrb_tick で
  行われるため、「起床処理と queue 操作の競合」というカテゴリの障害が構造的に消える。

保証されない (upstream の割り込みモデルでも同様に保証されないもの):

- **C 関数内での preempt**: OP 境界に到達しない長い C 呼び出し中は switch できない。
  これは upstream も task_across_c_boundary で同じく延期する。fmrb 固有の劣化ではない。
- **tick 適用の即時性**: tick の「適用」はループ先頭まで遅延する。遅延の上限は
  「最長の 1 opcode または最長の C 呼び出し」。timeslice 粒度の変化と合わせて
  rederive_vm_task.md の意図的差異の節に記録済み。

### idle 時のレイテンシ (実装上の推奨)

タスクが全員 sleep 中のとき、スケジューラは `mrb_hal_task_idle_cpu` で休む。
ここで長い固定 delay にすると sleep の起床がその分遅れる。推奨実装:

- idle_cpu は **FreeRTOS の task notification (または semaphore) を timeout 付きで待つ**。
- top-half の timer コールバックが pending を増やしたら同じ notification を叩く。
- これで idle 中でも tick 到着から次のループ先頭までのレイテンシがほぼゼロになり、
  busy-wait も避けられる。単純な `vTaskDelay(1)` でも動作はするが、起床精度が
  tick 周期ぶん劣化するので、まず notification 方式で実装すること。

## 実装指示

### 1. port 選択マトリクス (最初に確定させる)

upstream の仕組み (確認済み): `conf.ports :a, :b` はビルド設定ごとの指定で、
**各 gem は自分が持つ ports/<name>/ のうちリスト先頭一致の 1 つをコンパイルする**。
明示しないとホストビルドは `['posix']` に自動フォールバックする (build.rb:199)。

**罠**: mruby-task の posix port は SIGALRM から mrb_tick を直接呼ぶ。fmrb Linux ビルドで
conf.ports 未指定のままだと、案D と SIGALRM の二重 tick 源が静かに同居し、排除した
はずの cross-thread mrb_tick が復活する。**両ターゲットの build_config で conf.ports を
必ず明示し、mruby-task が posix に落ちない構成にすること。**

出発点として以下を提案する (要ビルド検証。socket 等 posix port が必要な gem がある点に注意):

| ビルド | conf.ports (先頭から優先) | mruby-task が拾う port | machine が拾う port | socket が拾う port |
|--------|--------------------------|----------------------|--------------------|--------------------|
| linux  | `:esp32_linux, :freertos, :posix` | freertos (新設) | esp32_linux | posix |
| esp32  | `:esp32, :freertos, :posix` | freertos (新設) | esp32 | esp32 |

- posix はリスト末尾に残す (socket 等のため)。mruby-task は freertos を新設するので
  posix より先に一致し、SIGALRM 版は選ばれない。
- esp32 ターゲットは gem の C ソースを CMake (components/picoruby-esp32/CMakeLists.txt の
  PICORUBY_SRCS) 側でもビルドしている。**どちらのビルドシステムが task_hal.c を
  コンパイルするかをターゲットごとに確認し、結果を resolutions.md に記録すること**
  (rake と CMake の二重コンパイル / どちらも拾わない、の両方を排除する)。

### 2. mruby-task 専用 port `ports/freertos/task_hal.c` (案b、top-half の実体)

配置は lib/patch の入れ子 submodule 上書き
(`lib/patch/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c`)。
FreeRTOS API のみで書き、ESP32 と Linux (POSIX FreeRTOS シミュレータ) で同一ソースを共有する。
esp_timer 等の IDF 固有 API は使わない。

実装する契約 (task_hal.h):

- `mrb_hal_task_init(mrb)`: timer タスク生成 (周期 = MRB_TICK_UNIT ms)、VM を registry に登録。
- timer コールバック (top-half): `switching_ = TRUE; pending[vm]++;` のみ。mrb_tick は呼ばない。
  notification で idle を起こす (前節)。
- `mrb_hal_task_take_pending_ticks(mrb)`: pending を返して 0 クリア。
  **クリティカルセクション or atomic exchange 必須**。
- `mrb_hal_task_idle_cpu(mrb)`: notification 待ち (timeout 付き)。
- `mrb_hal_task_sleep_us` / `mrb_task_enable_irq` / `mrb_task_disable_irq` / `mrb_hal_task_final`。
- pending カウンタと registry は **per-VM** (fmrb はアプリごとに VM を持つマルチVM構成)。
  upstream posix port の vm_list 方式が参考になる。

### 3. B1 picoruby-machine: merge-file 方式でよい

我々の replace 版はディレクトリ構造が upstream をほぼ鏡写しにしている
(upstream 側の変化は 19 ファイル +853/-208、新規は nrf52 port のみ)。
「replace = 全面書き直し」ではなく、**L0 と同じ per-file 3-way (git merge-file) で進める**。
base = 旧 pin の picoruby-machine、ours = lib/replace、theirs = 新 HEAD。

- tick 責務 (timer タスク、pending、旧 request_switch 相当) は machine から**撤去**し、
  ports/freertos/task_hal.c へ一本化する。machine 側は本来の責務
  (sleep、console I/O、HAL init 等) に限定する。
- 我々の独自資産 esp32_linux port は upstream に存在しないのでそのまま持ち越し。
  nrf52 は fmrb 未使用なので replace に取り込まなくてよい。

### 3.5 Linux シグナル禁止の制約 (B1 マージ時の必須ルール)

**fmrb の Linux ターゲットが使う FreeRTOS POSIX シミュレータは、タスクの suspend/resume と
tick 供給を Linux シグナルで内部実装している。したがって Linux ターゲットにリンクされる
コードは、シグナルを「使う」行為 (sigaction / signal() / setitimer / kill 等) が原則禁止。**
シミュレータの内部機構と衝突し、タスク切替や時間管理が壊れる。

- **upstream の machine ports/posix/hal.c は新 pin で sigaction(SIGALRM) + setitimer(ITIMER_REAL)
  を自前で仕込んでいる** (bare-posix 用の独自 tick)。B1 の merge-file でこのブロックを
  絶対に採用しないこと。tick は D7 (task_hal.c) の専任であり、machine が timer を arm する
  必要はもう無い。
- 我々の replace 版 ports/posix/ はこの制約を守る設計になっている (handler 設置や setitimer は
  無し)。以下は「使う」に当たらないので維持してよい:
  - `pthread_sigmask` による防御的マスク (stdin_reader スレッド等)。
  - `Machine_delay_ms` の chunked wall-clock ループ (シミュレータの SIGALRM で nanosleep が
    EINTR early-return する問題への対処。コメント参照)。
- 同じ理由で、mruby-task の SIGALRM port 抑止 (hal-task-freertos ダミー gem) は
  「二重 tick の回避」ではなく「シミュレータ破壊の回避」であり、必須要件である。
- socket 等の posix port にある EINTR リトライはシグナルを「受ける」側の堅牢化なので問題ない。
  マージで新たに取り込むコードに sigaction / setitimer / signal() が現れたら、その場で停止して
  依頼者に確認すること。

### 4. global_mrb (compiler Option A)

決定事項 1 のとおり mutex 方式。コンパイル呼び出し点 (sandbox 経由が主のはず) で
mutex → global_mrb 設定 → コンパイル → 復元。設定箇所と mutex の実体がどこかを
resolutions.md の compiler 節に記録すること。

## 検証項目 (Linux ビルドで)

1. preempt: busy loop する Task と sleep する Task の混在で、busy 側が timeslice で
   切り替わり sleep 側が起床すること。
2. sleep 精度: `sleep_ms(100)` 実測が 100ms + tick 周期程度に収まること
   (idle notification 方式の効果確認)。
3. ブロッキング後のまとめ適用: 長い C 呼び出し (Machine.delay_ms 等) 後に
   sleep タスクの起床がドリフトしないこと。
4. 長時間走行: 1-3 を混在させたストレスを数十分回し、tick 破壊 (task context
   corrupted) が再発しないこと。
5. 二重 tick 源の不在確認: Linux ビルドのリンク結果に mruby-task の
   ports/posix/task_hal.c (SIGALRM) が**含まれていない**ことをビルドログ/シンボルで確認。
6. シグナル禁止の確認 (3.5節): 最終バイナリ中の sigaction / setitimer 呼び出し元が
   FreeRTOS シミュレータ内部 (と、許容済みの pthread_sigmask) のみであることを
   nm / objdump で確認し、resolutions.md に記録する。upstream machine posix hal.c の
   SIGALRM tick ブロックが混入していればここで検出できる。

注意: Linux の FreeRTOS シミュレータは実質単一スレッド実行のため、pending カウンタの
原子性はテストでは検証できない (実機のみで顕在化する)。**コードレビューで
クリティカルセクションの使用を確認することがテストの代わりになる。**
実機 (デュアルコア) での長時間走行は依頼者の確認項目として tasklist に残すこと。
