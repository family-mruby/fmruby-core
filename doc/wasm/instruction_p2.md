# 実装指示書 P2: task_hal の自己プリエンプション化

対象: 実装担当セッション。前提: plan.md と implementation_plan.md の P2 節を
読むこと。wasm とは独立に、既存の Linux sim / ESP32 実機で完結するフェーズ。
実機の kill 問題 (doc/archive/app_kill_fix) と同根で、単独でも本体の改善になる。
タスクごとにコミット (push はしない)。report は doc/wasm/report/p2.md へ。

## ゴール

CPU-bound な Ruby アプリを「別タスクの mruby_tick が mrb->task.switching を
撃って剥がす」現行方式から、「VM 自身のスレッドがバイトコード境界で実時間を
見て自分に switching を立てる」自給方式に変えられるようにする。
二段構えにする: 新設フラグで自給側を有効化でき、**既定は従来動作のまま**。
wasm (協調 port) では top-half を起動せず自給のみで成立させるのが最終目的だが、
本フェーズの検証は既存の Linux sim (プリエンプティブ port) と ESP32 で行う。

## 編集対象と罠

- HAL の原本は
  `lib/patch/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c`
  (228 行)。components/picoruby-esp32/picoruby/ 以下のコピーは rake が
  `cp -rf lib/patch/picoruby-mruby` (rakelib/setup.rake:151) で上書きするので
  触らない。lib/ を触ったら `rake clean`。
- 現行構造 (task_hal.c 冒頭のコメントに詳細):
  - top-half `mruby_tick_task` (task_hal.c:70): 4ms (MRB_TICK_UNIT) ごとに
    各 VM の pending_ticks++、3 tick (MRB_TIMESLICE_TICK_COUNT) ごとに
    switching=TRUE、VM へ notify。
  - bottom-half `mrb_hal_task_take_pending_ticks` (task_hal.c:172): VM スレッドが
    排出して mrb_tick を適用。

## 設計上の急所 (最初にここを固める)

**「take_pending_ticks で時間を見て switching を立てる」だけでは busy-loop を
剥がせない。** busy-loop 中の VM は switching が立って初めてスケジューラ
(task_run_body) へ戻る。つまり自給の時間チェックは、VM が Ruby 実行中に
周期的に通る場所に要る。

確認済みの事実:

- switching の検査点は vm.c の `RETURN_IF_TASK_STOPPED` マクロ
  (components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/
  vm.c:1666)。NEXT / END_DISPATCH で展開され、**バイトコード境界ごとに毎回
  通る**。ここが唯一の周期 hook 地点。
- マクロ内の task_across_c_boundary は switching が真のときしか呼ばれない
  (短絡評価) ので、既存の呼び出しには相乗りできない。
- vm.c は lib/patch に無い (以前の tick 修正は上流にマージ済みで、submodule の
  vm.c がそれを含む)。ただし patch 機構は
  `cp -rf lib/patch/picoruby-mruby` の一括コピーなので、
  `lib/patch/picoruby-mruby/lib/mruby/src/vm.c` を置けば rake 変更なしで
  差し替わる (mruby-task の task.c が同じ方式の前例)。

推奨する形:

1. vm.c の RETURN_IF_TASK_STOPPED (またはその直前) に、フラグ有効時のみ
   「N 回に 1 回 (計数で間引く)、HAL の自己チェック関数を呼ぶ」を足す。
   vm.c は全文コピーの patch になるので、**差分は最小に保ち、差分箇所に
   目印コメントを付ける** (上流追従のため)。以前の vm.c 修正が上流に入った
   前例があるので、hook をマクロ 1 個で差せる形に整理して上流提案する余地も
   report に書く。
2. HAL 側 (task_hal.c) に自己チェック関数 (例: mrb_hal_task_self_tick(mrb)) を
   新設: fmrb_hal_time_get_us (components/fmrb_hal/fmrb_hal_time.h:17) の
   前回チェックからの経過を MRB_TICK_UNIT で tick 換算して pending_ticks に
   自給し、現行 top-half と同じ cadence (4ms x 3 tick = **12ms**) で
   switching=TRUE を立てる。
   - implementation_plan に「3ms 相当」とあるのは現行値と食い違う。受け入れ
     条件が「従来と同等のタイムスライス」なので **12ms 周期を正とする**。
   - 時刻源に xTaskGetTickCount を使わない (協調 port では busy-loop 中に
     tick が進まない。実時間を使う理由そのもの)。
3. 依存の確認を先にやる: task_hal.c は現在 FreeRTOS ヘッダのみ (ファイル頭の
   コメント参照)。fmrb_hal_time.h を include できるか、task_hal.c がどの
   include path でビルドされるか (components/picoruby-esp32/CMakeLists.txt) を
   確認し、通らなければ include path を足す。ファイル頭の設計コメントも
   現状に合わせて更新する。

時間チェックの間引き計数は VM 性能に直結する。チェック 1 回のコストと 12ms
境界の検出遅れのトレードオフを軽く計測して値を決め、report に記録する。

## フラグ設計

- コンパイル時マクロ (例: MRB_TASK_TICK_SELF_SUPPLY)。
  - 未定義 (既定): 完全に従来どおり。top-half あり、vm.c の追加 hook は
    コンパイルされない (実行コストゼロ)。
  - 定義時: 自給有効。top-half は起動しない (tick_manager_ensure_started で
    タスクを作らない)。mrb_hal_task_idle_cpu の ulTaskNotifyTake(4ms) は
    notify 元がいなくなるが、4ms タイムアウトで戻るのでそのままでよい
    (implementation_plan の判断)。
- Linux 検証用にビルドから立てられるようにする (rake の環境変数 → CMake
  define。FMRB_APP_ENGINE の通し方を手本に)。ESP32 は当面既定 (top-half) のまま。

## 検証

sim は MCP ツール (sim_up / sim_app / sim_input / sim_screenshot / sim_down) で
行う。標準構成 (Spinel カーネル) の sim 検証には**エディタを起動して 1 打鍵**を
必ず含める (fmruby-core/CLAUDE.md のテスト節)。

1. 既定ビルドの回帰: `rake clean` → `rake build:linux` → sim ブート、
   デスクトップ / エディタ操作、タイマ系アプリの挙動が従来どおり。
2. 自給ビルド: busy-loop アプリ (`while true; end` 相当。flash/app に一時作成。
   末尾の起動トレーラ begin/new/.start/rescue が無いと何も動かない) を
   起動しても、デスクトップとエディタが操作できること (飢餓しない)。
   飢餓の再現条件は doc/reference/task_priority.md の実証記録を使う。
3. タイムスライス実測: 一時的なログ計装で switching の発火周期を測り、
   両モードで従来 (12ms) と同等であることを確認。数字を report へ。
4. ESP32 実機 (Tab5): 既定ビルド (top-half 併存) でブートと通常操作の回帰。
   flash / serial_log は MCP ツールで。**焼く前にコード経路を最後まで読む**。
   実機が繋がっていなければ「未実施」と report に明記してユーザに依頼する。

## 受け入れ条件 (implementation_plan P2)

1. 既定ビルドで Linux sim の回帰が全部通る (挙動・性能とも従来どおり)。
2. 自給ビルドで busy-loop アプリ併存時にデスクトップ・エディタが操作できる。
3. タイムスライス実測が従来と同等 (12ms 前後)。
4. Tab5 実機で既定ビルドのブート・通常操作の回帰 (実機不可なら明記して持ち越し)。

## report に書くこと

- hook 地点の確定 (ファイル:行) と vm.c patch の差分の要約、上流提案の余地。
- 間引き計数の値と根拠 (チェック 1 回のコスト、検出遅れの実測)。
- 既定ビルドに影響ゼロであることの確認方法。
- タイムスライス実測値 (両モード)。
- ESP32 でも自給に一本化できそうかの所見 (implementation_plan の宿題。
  実機計測後の判断材料)。

## やらないこと (P2 の範囲外)

- wasm port 上での動作確認 (P4a で結合)。
- ESP32 の自給一本化 (将来判断)。
- fmrb_app_kill の強制経路の改修 (同根だが別テーマ。気づきだけ report へ)。
