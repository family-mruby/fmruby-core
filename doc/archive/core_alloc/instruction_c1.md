# 作業指示書 C1: タスクごとの CPU 使用率を測る (変更はしない)

対象: 実装担当のサブエージェント。前提: plan.md、doc/reference/task_priority.md、doc/shell_input_lag/report/s1.md。
report は `report/c1.md` へ。**割り振りは変えない** (ユーザがこの結果をレビューしてから決める)。

## T1: 測る仕組み

- 試験用のビルドで `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y` (と必要なら `CONFIG_FREERTOS_USE_TRACE_FACILITY=y`) を
  有効にする。**リポジトリの sdkconfig / sdkconfig.defaults* は変えない** (scratchpad の追加の defaults ファイルを
  `SDKCONFIG_DEFAULTS` に足す、別の build ディレクトリ。doc/flash_write_flicker/report/f1.md と doc/iram_reduction/
  report/r3.md の T1 のやり方。生成 rb が更新されて build/ の app-flash が作り直す罠に注意)。
- `uxTaskGetSystemState` で一定の間隔 (例 5 秒) の差分を取り、タスクごとの割合と core ごとの合計 (IDLE を含む) を
  ログに出す計装 (一時的なもの、または既定で無効の計測の口)。計装自体の負荷を書く。

## T2: 場面ごとに測る (P4-Nano、ミュートのまま)

- (a) 待機、(b) エディタで打鍵、(c) Monitor・MML・Breakout.py + Shell で打鍵 (doc/shell_input_lag の (b))、
  (d) 動画の再生、(e) 遠隔の画面の配信 (tab5_screenshot をくり返すなど)。それぞれ 30 秒以上。
- 表: 場面ごとに core 0 / core 1 の使用率、上位 10 タスクの割合 (どの core か)、display_p4 の render の時間と
  枚数、Shell の打鍵の遅れ (c のみ)。
- 割り振りを変えた場合の見込み (表示を core 0 へ移したら core 0 が足りるか、など) を、数字から読める範囲で書く
  (試験はしない)。

## 作業の決まり

- 作業ブランチ `feature/core-alloc` (親が develop から切る)。計装をコミットするなら既定で無効の形で。
  push とマージはしない。`.env` は書き換えずコミットしない (scratchpad の env_before_core_alloc.diff と一致)。
- sdkconfig、graphics-audio、submodule は変えない。
- 実機は**ミュートのまま**。最後は develop と同じ版 (計測なし) を焼いて戻す。
- 親から「実機の操作を止めて」と伝えられたら、すぐ止める。
- report は日本語の常体。ユーザがレビューする資料なので、表を中心に、読み方を短く添える。

## 止まる条件

- 計測の仕組みに sdkconfig の採用 (リポジトリの defaults の変更) が要ると分かったとき。
- 割り振りや優先度を変えないと測れない場面があるとき (変えずに、案として返す)。
- **割り振り・優先度・その他の利用者から見た動きを変える必要があると思ったとき。この段階では何も変えない**。
