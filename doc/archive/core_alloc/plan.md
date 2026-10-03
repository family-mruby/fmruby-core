# P4 の core の割り振り

> 状態: 完了 | 更新: 2026-10-03 | 測った結果、core 1 は最大 55% で詰まっておらず、描画の長さの大半は PPA の完了待ち。USB キーボードでの打鍵も問題なし (ユーザ確認)。割り振りは変えない (ユーザ決定)。結果は reference/cpu_usage.md

## 目的

アプリを多く動かしても、操作 (打鍵・入力) が引っかからないように、P4 (Tab5 / NARYAv4) の 2 つの core の使い方を見直す。

## 分かっていること

- 今の割り振り (`components/fmrb_common/include/fmrb_task_config.h`、doc/reference/task_priority.md):
  core 0 = host (10)、audio_p4 (6)、hw_proxy (6)、USB・HID (5)、video (4)、ble_fs、遠隔の一部。
  core 1 = display_p4 (5)、kernel (9)、デスクトップ (3)、Shell・ユーザのアプリ (2)、Services (1)、
  status_led、touch。方針は「HW 系は core 0 / VM 系は core 1」だが、表示は core 1。
- doc/shell_input_lag/report/s1.md: Breakout.py を動かすと display_p4 が core 1 の約 85-90% を使い、
  優先度 2 の Shell の 1.5 ms の計算が最大 900 ms に伸びた。
- FreeRTOS の実行時間の統計 (`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`) は無効で、core ごとの空きは未測定。

## 方針 (ユーザ決定 2026-10-02)

- **割り振りを変える前に、CPU の使用率をユーザがレビューする**。C1 は測るだけで、割り振りは変えない。
- 計測は試験用のビルドで (sdkconfig は変えない。追加の defaults ファイルと別の build ディレクトリ)。
- レビューの後に、候補 (表示を core 0 へ、ユーザのアプリを固定しない、合成を軽くする) を比べる。

## 段階

| 段階 | 内容 | 状態 |
|---|---|---|
| C1 | タスクごと・core ごとの CPU 使用率を、いくつかの場面で測って表にする (instruction_c1.md)。ユーザのレビュー | **完了** (report/c1.md、2026-10-03 レビュー)。計測の仕組みは既定で無効の形でコミット (cpu_stats.c) |
| C2 | レビューを受けて、割り振りを変える試験と前後の比較 | **行わない** (ユーザ決定 2026-10-03: 変える効果が小さい) |

## 受け入れ条件 (C1)

- 場面ごと (待機、エディタで打鍵、アプリ 4 本 + Shell で打鍵、動画、遠隔の配信) に、core 0 / core 1 の使用率と、
  上位のタスクの割合が表になっている。測り方と誤差 (計測自体の負荷) が書いてある。

## 結論 (2026-10-03)

割り振りは変えない。速くしたいときは、全画面・高解像度の合成 (1 枚 30 ms の計算) と遠隔の配信の画面の取り込みを軽くするほうが効く。計測版のビルドで大きなファイルを書くと落ちる件 (report/c1.md 7 章) は、計測版だけの既知の問題として記録 (未調査)。
