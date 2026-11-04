# ESP-IDF v6 への移行 (sim をネイティブ版に置き換えて進める) 計画

> 状態: 構想 | 更新: 2026-11-04 | IDF 6 の Linux 版 (preview) の上流の不具合が移行の最大の壁だったので、ESP-IDF の Linux 版で作る sim をやめ、IDF を使わないネイティブ版を sim の代わりに位置づけてから、実機のビルドだけを IDF 6 へ移す。着手の時期は未定

前の調べと回避策は `doc/reference/idf6_migration_notes.md` (この計画はその「新しい方針」)。

## 目的

- ESP-IDF v6 へ移る道を開く。
- ホストでの確認 (今の sim が受け持っているもの) を失わない。

## 背景

- 2026-07 ごろ、P4 対応の初期に IDF 6.0.1 を試し、実機のビルドの課題 (picolibc、driver の分割、M5GFX など) には
  回避策を見つけた。止めたのは **IDF 6 の Linux 版 (preview) の上流の不具合**のためで、sim (fmruby-core と
  fmruby-graphics-audio の両方が `idf.py --preview set-target linux` で作る) が作れなかった
  (`idf6_migration_notes.md` §6)。それ以来、全部 IDF v5.5.4 にそろえている。
- その後、ネイティブ版 (`doc/linux_native/`) とブラウザ版の土台 (`hosted/`) ができた。これは **ESP-IDF を使わない**
  (自前の CMake と、同梱した FreeRTOS の協調 port)。IDF の版に左右されない。

## 方針 (2026-11-04 ユーザ)

- **sim (ESP-IDF の Linux 版で作るもの) をやめ、ネイティブ版を sim の代わりに位置づける。** そうすれば ESP-IDF を
  使うのは実機のビルドだけになり、Linux 版 (preview) の壁は無くなる。
- **シグナルで tick を割り込ませる方式は使わない** (シグナルと POSIX の API の相性が悪く、苦労した経緯がある)。
  FreeRTOS は今の協調 port のまま。
- 実機と同じ時間の進め方は、**普通のスレッドが tick を数えて VM の切り替えのフラグを書くだけの「非同期の tick」**で
  再現する (OS のスレッドの横取りはしない)。

## sim をやめると失われる確認と、その代わり

| sim が受け持っている確認 | 代わり |
|---|---|
| 実機と同じ mruby-task の時間の進め方 (VM の外が tick を数え、`vm_deliver_switch` が切り替えのフラグを書く) | ネイティブ版に「非同期の tick」の形を足す (S1)。VM が自分で tick を進める形 (`MRB_TASK_TICK_SELF_SUPPLY`) を切り、別のスレッドからフラグが書かれる経路を通す。切り替えの要求の数は書き手ごとに分けてあり (checkpoint の F0)、そのまま使える |
| Retro の 2 チップの構成 (core と graphics-audio (WROVER の役) を別のプロセスにした通信、音源 APU、映像の側) | ネイティブ版に、graphics-audio を別のプロセスとしてつなぐ形を足す (S2)。graphics-audio も IDF なしで作れるようにする |
| 回帰の道具 (`regress:run` の VM のプール、`regress:screens` の Retro・Modern、`regress:live`) | ネイティブ版で同じ場面を流せるようにする (S3)。`regress:screens` には既にネイティブ版の形がある |
| ESP-IDF の API に対してコンパイルが通ること (実機の platform の実装のヘッダ) | 実機のビルド (`regress:size` が作る) で拾う |
| FreeRTOS のタスクどうしの横取りでしか起きない不具合 (例: `doc/reference/sim_log_deadlock.md` のログの錠の優先度逆転) | ホストでは失う。実機の確認 (`rake regress:boot` など) で拾う。sim の POSIX port 自体がシグナルで動いていて実機と違う形だったので、sim で見つかったものの一部は sim だけの現象だった可能性もある |

参考: 2026-11-03 に見つけた Task が順番を得られないハング (`doc/checkpoint/report/hang_task_sleep.md`) は、VM が自分で
tick を進める形 (ネイティブ版・ブラウザ版) にしか無く、sim では出なかった。時間の進め方の 2 つの形は、どちらか
だけでは片方の不具合を見逃す。S1 はこの両方をホストで試すためのもの。

## 段

| 段 | 内容 | 状態 |
|---|---|---|
| S0 | sim だけが受け持っている確認の洗い出し (文書だけ。上の表を今のコードと回帰の道具で確かめ直す) | 未着手 |
| S1 | ネイティブ版に「非同期の tick」の形を足す (普通のスレッドが tick を数えてフラグを書く。シグナルなし) | 未着手 |
| S2 | ネイティブ版に Retro の 2 チップの形を足す (graphics-audio を別のプロセスで、IDF なしで作る) | 未着手 |
| S3 | 回帰の道具をネイティブ版へ移す (`regress:run`・`regress:screens` の Retro・`regress:live`) | 未着手 |
| S4 | sim を廃止する (両リポジトリの Linux 版のビルド、docker の 3 つのコンテナ、MCP の `sim_*`、文書の参照) | 未着手 |
| S5 | 実機のビルドを IDF 6 へ移す (`idf6_migration_notes.md` §1〜5 の回避策を使い、依存ライブラリの IDF 6 対応を確かめる) | 未着手 |

**着手の時期は未定** (2026-11-04 ユーザ)。

## 受け入れ条件 (案)

- S4 の前に、ネイティブ版で S0 の表の「代わり」が全部動いている (回帰の道具が sim と同じ場面を流せる)。
- S5 の後、全端末 (`regress:size` の `ALL=1`) が IDF 6 で作れ、内蔵 RAM が増えない。実機の起動の確認が通る。

## 未確定事項

- S2 の graphics-audio を、どの形でネイティブ版とつなぐか (今の sim の IPC をそのまま使うか)。
- `regress:live` (相手のいる通し) の場面のうち、sim に頼っているものの移し方。
- IDF 6 の版 (6.0.x のどれか) と、依存ライブラリ (M5GFX ほか) の IDF 6 対応の状況。
