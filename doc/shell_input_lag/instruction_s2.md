# 作業指示書 S2: mruby の割り当て経路の回収条件

対象: 実装担当のサブエージェント。前提: plan.md、report/s1.md (特に「何が起きていたか」1 と「案」1)。
report は `report/s2.md` へ。

## 背景

mruby の `mrb_obj_alloc_core` (src/gc.c) は、空きスロットが尽きたとき、`auto_step` なら
`live_after_mark + MRB_HEAP_PAGE_SIZE/2 < capacity` のときに `mrb_full_gc` を呼んでから、空かなければページを足す。
P4 では `MRB_HEAP_PAGE_SIZE` が 256 なので、余白の判定は 128 スロット分 (容量の約 4%) しかない。生存数が容量の
すぐ下に張り付く VM (Shell) では、約 120 割り当てごとに全体回収 (平均 130 ms) が走る (report/s1.md)。
この条件は上流の mruby (PR #6933 系、`c712a832e` / `0381f61c6`、のちに scheduler-driven GC `115de82c3`) にある。

## T1: 直し方を決める

- 上流の gc.c の該当部分と、その PR の意図 (ページを過渡的な最大値まで増やしたまま戻らない問題を避ける) を
  読み、意図を壊さずに「回収しても少ししか空かない」ときは回収しない条件を決める。案: 回収で戻る見込みの空き
  (`capacity - live_after_mark`) が容量の一定割合 (例 1/4) 以上か、ページ数枚分以上のときだけ回収する。
  複数の案を比べ、選んだ理由を report に書く。
- 当て方は lib/patch (submodule は直接編集しない)。mruby の src/gc.c をまだ lib/patch で上書きしていないので、
  既存の lib/patch/picoruby-mruby/lib/mruby/src/vm.c の当て方 (rakelib/setup.rake) にならう。差分は最小にし、
  上流に出せる形 (この条件の部分だけ) を意識する。ESP32 (rake の libmruby と CMake の両経路)、Linux、wasm の
  どれにも同じ gc.c が使われることを確かめる (lib/ を変えたら `rake clean`。memory の dual build の罠に注意)。

## T2: 測る (直す前と後、同じコミットのビルド同士)

- 実機 P4-Nano (NARYAv4)。S1 の計測方法 (report/s1.md「計測の方法」) を一時的に戻すか、同等の方法で:
  - Shell だけ: 打鍵から表示までの時間 (50 ms 以上の割合、最大)、GC の回数と 1 回の時間 (`FMRB_GC_PROFILE=1`
    の計測用ビルドでよい)。
  - **全 VM への影響**: デスクトップ、Services、Monitor、MML、FM-Editor (互換構成なら mruby)、ゲーム 1 本程度で、
    VM プールの使用量 (周期ダンプの `VM Pools`)、GC の停止時間、`hid_lat`。プールの使用量が増えすぎて
    回収器が空回りする (memory の「VM プールを使い切ると例外なしで黙って死ぬ」、7 割超) ことが無いか。
  - 数分動かしてプールが増え続けないか (ページが戻らない問題の再発が無いか)。
- sim (標準と互換) でも同じ確認を軽く。Retro の解像度の sim (S3 は `MRB_HEAP_PAGE_SIZE` が違うかもしれない。
  値を確かめて書く) も 1 回。

## T3: 判断の材料をまとめる

- 前後の表、全 VM の影響、上流に PR を出すならどう説明するか (上流の関心で書く: 回収で空く量が少ないときの
  回収の繰り返しという動作の問題として。組み込み・小さなメモリの動機は書かない) を report に。
- 効かない、または副作用が大きいときは、入れずに止まり、測った値と別の案を返す。

## 検証

- ビルド: TAB5 / NARYAv4 / S3 / Linux (標準・互換) / wasm。静的な D/IRAM が develop を超えない
  (NARYAv4 の develop は Shell のスタックの変更後の値を最初に測って基準にする)。`rake test`。
- sim の標準構成の検証にはエディタの起動と 1 打鍵を含める。

## 作業の決まり

- 作業ブランチ `feature/shell-gc` (親が develop から切る)。本体の checkout で作業する。自分が変えたファイルだけを
  パスで指定してコミット。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_shell_gc.diff と一致する。
- sdkconfig、graphics-audio は変えない。submodule は直接編集しない (lib/patch 経由)。
- 実機の書き込みはよい。ファイルを flash に繰り返し書く試験はしない。端末に置いた試験ファイルは消す。
- 親から「実機の操作を止めて」と伝えられたら (通話中など。アプリの起動や再起動で音が出る)、書き込み・再起動・
  アプリの起動と停止・遠隔の入力をすぐ止め、ビルドとコードの作業だけを続ける。
- 目視の確認は親がユーザに依頼する。ユーザとのやり取りはしない。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。
- 終わったら build/ は NARYAv4 のビルドのまま残してよい。

## 止まる条件

- 上流の意図を壊さずに条件を決められない、または副作用 (プールの増え方、他の VM の停止) が大きいとき。
- gc.c 以外 (VM 全体、ページの大きさの定義、表示のタスク) に手を入れる必要があると分かったとき。

## report に書くこと

選んだ条件と理由、前後の表 (Shell と全 VM)、プールの推移、上流への説明の下書き、見立てと違った点、残件。
