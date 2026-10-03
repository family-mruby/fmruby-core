# CPU の使用率 (P4、タスクごと・場面ごと)

> 状態: 計測済 | 更新: 2026-10-03 | P4-Nano (NARYAv4) の実測。どの場面でも core 1 は最大 55%、core 0 は配信中を除き 17% 以下で、両方とも余っている。表示の描画の長さの大半は PPA の完了待ち (眠っている時間)。core の割り振りは変えない (2026-10-03 決定)。経緯は archive/core_alloc/report/c1.md

## 1. 結論

- **2 つの core とも余裕がある**。いちばん重い「アプリ 3 本 + Shell で打鍵」でも core 1 が 51-52%、core 0 が 17%。
  ただし、全画面・高解像度のエディタ (b') だけは表示の計算が 1 枚 30 ms で、ほぼ全部が CPU。
- **表示 (display_p4) の描画の経過時間と CPU は別物**。アプリ 3 本のとき、描画は 5 秒のうち 85% の時間を占めるが、
  CPU を使っているのは 29% で、残りは PPA (画像の拡大・合成の回路) の完了を眠って待っている。眠っている間は、
  同じ core の低い優先度のタスク (アプリ) が動ける。
- **core の割り振りは変えない** (ユーザ決定 2026-10-03)。表示を core 0 に移しても PPA の待ちは縮まず、入力 (USB・HID、
  同じ優先度 5) や MJPEG の配信と取り合う危険が増える。USB キーボードでの打鍵も、アプリを多く動かした状態で問題が
  無かった (ユーザ確認)。
- 速くしたいときに効くのは、割り振りより、全画面・高解像度の合成 (1 枚 30 ms の計算) と、遠隔の配信のための画面の
  取り込み (1 枚あたり +7-8 ms) を軽くすること。

## 2. 場面ごとの使用率

数字は 5 秒の窓の平均 (%)。core の列は「その core が IDLE 以外を動かしていた割合」、タスクの数字は「1 つの core を
100% とした割合」。場面の作り方は 5 章。

| 場面 | core 0 | core 1 | 表示 display_p4 (core 1) |
|---|---:|---:|---:|
| (a) 待機 (デスクトップだけ) | 4.1 | 4.0 | 1.5 |
| (b) エディタ (窓) で打鍵 | 11.4 | 14.9 | 9.0 |
| (b') エディタ (全画面・高解像度) で打鍵 | 13.8 | 26.3 | 21.4 |
| (c) Monitor・MML・Breakout.py + Shell で打鍵 | 17.2 | 50.9 | 28.9 |
| (c) と同じアプリで打鍵なし | 11.1 | 44.2 | 26.3 |
| (c) の対照: Shell だけで打鍵 | 9.9 | 19.3 | 9.2 |
| (d) 動画 15 fps (288x160) | 10.6 | 26.0 | 22.5 |
| (d') 動画 30 fps | 16.7 | 37.5 | 33.4 |
| (e0) Breakout.py だけ | 9.5 | 32.0 | 25.8 |
| (e1) e0 + 遠隔の MJPEG 配信 (`/stream`) | **31.6** | 50.9 | 40.3 |
| (e2) e0 + 遠隔の H.264 配信 (`/ws_video`) | 13.4 | 49.2 | 38.7 |

## 3. タスクごとの使用率

主なタスクを場面ごとに並べた (%)。「-」はその場面で動いていないか 0.1% 未満、空欄は上位に出ていない (測った窓の上位の一覧に無い)。

### core 1 (表示とアプリ)

| タスク (優先度) | (a) | (b) | (b') | (c) | (c) 打鍵なし | (d') | (e1) |
|---|---:|---:|---:|---:|---:|---:|---:|
| display_p4 (5) | 1.5 | 9.0 | 21.4 | 28.9 | 26.3 | 33.4 | 40.3 |
| FM-Shell (2) | - | - | - | 9.0 | 3.9 | - | - |
| Monitor (2) | - | - | - | 5.9 | 6.1 | - | - |
| Breakout.py (2) | - | - | - | 2.9 | 3.5 | - | 4.0 |
| FM-Editor (2) | - | 3.0 | 2.9 | - | - | - | - |
| system_desktop (3) | 1.0 | | | | | 1.3 | |
| status_led (3) | 1.3 | | | | | | |

- アプリ (VM) は 1 本あたり 0.3-9%。Shell は打鍵 1 回で core 1 を約 5 ms 使う (描き直しと GC を含む)。
- status_led の 1-3% は、10 秒ごとのダンプ (`fmrb_task:` と VM プール、1 回約 130 ms) がほとんど。

### core 0 (入出力)

| タスク (優先度) | (a) | (b) | (c) | (d') | (e1) |
|---|---:|---:|---:|---:|---:|
| audio_p4 (6) | 2.6 | 3.3 | 6.7 | 5.5 | 7.4 |
| fmrb_host (10) | 0.4 | 1.9 | 3.8 | | |
| httpd (遠隔の入力) | - | 4.2 | 4.0 | - | |
| p4_video (4) | - | - | - | 8.2 | - |
| rd_mjpeg (4) | - | - | - | - | 11.2 |
| tiT (lwIP、固定なし) | | | | | 4.7 |

- audio_p4 は**ミュート中も合成を続ける**ので 2.6-7.4% 使う (ミュートは出力の最後の段で無音にする設計。
  reference/audio_output.md)。
- httpd の 4-5% は、計測で打鍵を遠隔の入力 (`/ws`) で送ったための負荷。USB キーボードなら出ない。

## 4. 表示の描画: 経過時間と CPU

`display_p4: render:` 行 (描画 1 枚の経過時間) と、CPU の割合を並べた。

| 場面 | 描画 枚/5 秒 | 1 枚の経過時間 平均 (最大) | 経過時間の合計 / 5 秒 | display_p4 の CPU | 1 枚あたりの CPU |
|---|---:|---:|---:|---:|---:|
| (a) 待機 | 5 | 27 ms (30) | 3% | 1.5% | 15 ms |
| (b) エディタ (窓) | 38 | 29 ms (34) | 22% | 9.0% | 12 ms |
| (b') エディタ (全画面) | 36 | 33 ms (62) | 24% | 21.4% | **30 ms** |
| (c) アプリ 3 本 + Shell 打鍵 | 122 | 35 ms (50) | **85%** | **28.9%** | 12 ms |
| (d') 動画 30 fps | 116 | 30 ms (35) | 69% | 33.4% | 14 ms |
| (e0) Breakout.py | 137 | 29 ms (33) | 79% | 25.8% | 9 ms |
| (e1) + MJPEG 配信 | 117 | 37 ms (49) | 86% | 40.3% | 17 ms |

読み方:

- 経過時間の合計 (85% など) と CPU (29% など) の差が、PPA の完了待ちで眠っている時間 (PPA は
  `PPA_TRANS_MODE_BLOCKING` で呼んでいる、display_backend_ppa.cpp)。どこでどれだけ待っているかは測っていない。
- 全画面・高解像度 (b') だけは、経過時間のほとんどが CPU (計算で時間を使う形の合成)。
- 遠隔の配信中は、配信用の画面の写しを作る分、1 枚あたりの CPU が 9 → 16-17 ms に増える。

## 5. 測り方 (再現の手順)

- **試験用のビルドで**、FreeRTOS の実行時間の統計を有効にする。リポジトリの sdkconfig / sdkconfig.defaults* は
  変えない。追加の defaults ファイルを `SDKCONFIG_DEFAULTS` の後ろに足し、別の build ディレクトリと別の sdkconfig で作る:

  ```
  # stats.defaults (scratchpad などに置く)
  CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
  CONFIG_FREERTOS_USE_TRACE_FACILITY=y
  CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=y
  CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y
  CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64=y
  CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y
  ```

  `idf.py -B <dir>/build -DSDKCONFIG=<dir>/build/sdkconfig "-DSDKCONFIG_DEFAULTS=config/sdkconfig.defaults.naryav4;<dir>/stats.defaults" -DFMRB_HW_TARGET=NARYAv4 build`
- 計装は `main/drivers/led_status/cpu_stats.c`。既定のビルドでは空の関数で、統計が有効なビルドでだけ `cpustat` タスク
  (core 0、優先度 7) が 5 秒ごとにログを出す:
  - `cpu: win=<ms> c0=<%> c1=<%> gone=<%> n=<タスク数> probe=<µs>` (c0/c1 は 100% から IDLE を引いた値。gone は
    窓の途中で消えたタスクなどの分)
  - `cpu: t <名前>/<core> <%> ...` (0.1% 以上のタスクを大きい順。core は固定先、`-` は固定なし)
- 計装の負荷: `cpustat` 自身 0.3-0.6% (core 0)、取得 1 回 1.8-3.0 ms (その間 core 0 のスケジューラが止まる)、
  タスク切り替えごとの計上は見えない程度。静的な D/IRAM は計測版で +2,744 B (既定のビルドは増えない)。
- 割り込みの時間は、割り込まれたタスクに計上される。
- **注意: 計測版のビルドで、352 KB のファイルを `/fs/put` で `/home` に書くと、core 0 で Instruction access fault に
  なった (2 回)**。develop では同じ操作が通る。実行時間の統計と `CONFIG_SPI_FLASH_AUTO_SUSPEND` の組み合わせを疑って
  いるが、調べていない。計測版では大きなファイルの書き込みを避ける。
- 落とし穴: 別の build ディレクトリで idf.py を回すと、生成される `*_combined.rb` が更新され、`build/` の app-flash が
  勝手に作り直す (flash_write_flicker/report/f1.md 8 章)。

### 計測の場面 (2026-10-03、develop 08350af6 + 計装、ミュート)

- 各場面 30-45 秒 (5 秒の窓 6-11 個の平均)。打鍵は遠隔の入力で 100 ms ごとに 384 回。
- (b) `default/editor`、(b') 続けて F11 で全画面。(c) `/app/demo/mml.app.rb`・`default/monitor`・
  `/app/game/breakout/breakout.app.py`・`default/shell` (MML はミュートで演奏中、Breakout.py は操作なし)。
- (d) 動画の再生のアプリで `/usr/share/samples/slides/movies/demo.mjpg` (288x160、30 枚、繰り返し)。
- (e1) `curl` で `/stream` を 36 秒受ける (tab5_screenshot と同じ MJPEG の経路)、(e2) `/ws_video` に 36 秒つなぐ
  (ブラウザの遠隔デスクトップと同じ H.264 の経路)。

## 6. 関連

- タスクの優先度と core の割り振りの考え方: reference/task_priority.md、`components/fmrb_common/include/fmrb_task_config.h`
- 経緯と、表示を core 0 へ移したときの見込み (試験はしていない): archive/core_alloc/report/c1.md 5 章
- 打鍵の遅れの本当の原因 (GC の点検と二重解放): shell_input_lag/、表示の受信の詰まり: archive/p4_cursor_lag/
