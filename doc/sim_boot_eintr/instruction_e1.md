# 作業指示書 E1: sim の起動の EINTR

対象: 実装担当のサブエージェント。前提: plan.md、ルートの CLAUDE.md (sim の仕組み、3 ホップの入力、gdb の当て方)、
memory の「sim の graphics-audio が EINTR で起動失敗」。report は `report/e1.md` へ。

## T1: 確かめる

- sim を何度も起動し直して (sim_down → sim_up、または docker compose を直接)、失敗の頻度と、失敗する呼び出し
  (graphics-audio の bind / listen / accept、core の connect、debugd の transport) を数える。
- 割り込んでいる信号を確かめる (FreeRTOS の POSIX ポートの SIGALRM の見込み。strace が使えるなら、または
  一時的なログで)。3 つのプロセス (sdl2-display、graphics-audio、core) のどれで起きるかも。
- ソケットや系統呼び出しで、EINTR を扱っていない所を graphics-audio と core (Linux の経路) で洗い出す
  (bind、listen、accept、connect、send、recv、read、write、nanosleep / usleep、select / poll など)。

## T2: 直す

- plan.md の候補から、既存の手当て (graphics-audio の display_shm.cpp の EINTR の再試行) にそろえて選び、
  理由を report に書く。洗い出した所を同じ形で直す (共通の小さな関数にまとめてよい)。
- graphics-audio の変更は許可する (作業ブランチ `fix/sim-boot-eintr`、親が切った)。fmruby-core も同名のブランチ。
- 実機のファーム (ESP32) の経路には影響させない (Linux の経路のファイルだけ、または `#ifdef` の内側)。

## 検証

- sim を**連続 30 回以上**起動し直して、毎回起動する (直す前の失敗の頻度と比べる)。標準構成と互換構成の両方。
  起動のたびに、エディタの起動と 1 打鍵まで (少なくとも数回)。
- `tools/fmrb_audio_probe.rb` で音の経路が生きていること (1 回)、sim_input の入力が届くこと。
- `rake test`。fmruby-core と graphics-audio の Linux のビルド。ESP32 のビルドが変わらないこと (NARYAv4 の静的な D/IRAM
  125,612 B、graphics-audio の WROVER のビルドが通る)。

## 作業の決まり

- `.env` は書き換えずコミットしない (scratchpad の env_before_eintr.diff と一致)。`rake clean_all` は使ってよい。
- sdkconfig、submodule は変えない。
- 自分が変えたファイルだけをパスで指定してコミット (両リポジトリ)。push とマージはしない。
- sdl2-display (親のリポジトリの docker の部分) を変える必要が出たら、止まって案を返す。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 原因が見立てと違い、FreeRTOS の POSIX ポートや IDF の Linux の部分を変える必要があるとき。
- 直しても失敗が残るとき (測った値と案を返す)。
