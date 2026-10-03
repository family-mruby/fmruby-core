# P4 の core の割り振り

> 状態: 進行中 | 更新: 2026-10-03 | アプリが多いと表示の合成が core 1 の 85-90% を使い、同じ core の VM の順番が回らない。C1 でタスクごとの CPU の使用率を測ってユーザがレビュー → その後に割り振りを変えるか決める

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
| C1 | タスクごと・core ごとの CPU 使用率を、いくつかの場面で測って表にする (instruction_c1.md)。ユーザのレビュー | 着手 |
| C2 | レビューを受けて、割り振りを変える試験と前後の比較 | 未 |

## 受け入れ条件 (C1)

- 場面ごと (待機、エディタで打鍵、アプリ 4 本 + Shell で打鍵、動画、遠隔の配信) に、core 0 / core 1 の使用率と、
  上位のタスクの割合が表になっている。測り方と誤差 (計測自体の負荷) が書いてある。
