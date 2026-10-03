# 作業指示書 C1: 同じ Spinel プログラムの 2 つ目の起動を断る

対象: 実装担当のサブエージェント。前提: plan.md (「C で対応」)、report/i1.md (5.3)、doc/app_exit/report/e1.md (E2)・e3.md。
report は `report/c1.md` へ。

## 決定 (ユーザ、2026-10-03)

C で対応する。同じ Spinel のプログラム (生成 C の `Init_<program>` が同じもの) が動いているとき、2 つ目の起動を断り、
「すでに起動しています / This app is already running.」の窓を出す。エディタは同時に 1 つ (`default/editor` と
`default/editor_fs` で 1 つ)。カーネル、ほかの Spinel のプログラム、mruby・Lua・BASIC・MicroPython のアプリとは
同時に動く。

## T1: 戻す

- E2 の a1b2ee50 の claim の部分 (`fmrb_app_spinel_claim` / `release`、`ctx->spinel_program`、spawner の断りと窓、
  `destroy_vm` での解放、強制終了での解放) を、今の develop に合わせて戻す。E3 で外した差分 (e43bcf02) の逆。
- **E3 で直した文書の枠の扱い (エディタごとに自分の枠、`EditorCore.slot` を C に聞く) はそのまま残す**。1 つだけの
  前提に戻さない。
- これ以外の動きの変更は入れない。**Spinel の gem (raycast など) を断る形にはしない** (ユーザの判断待ち)。
  `sp_brk_stack` / `sp_fstr_tab` の穴も、この段階では直さない。

## 検証

- 実機 P4-Nano: エディタを開いた状態で、もう 1 つ開こうとすると断る窓が出る (ランチャー・遠隔の `/app/launch`・
  全画面の起動のどれでも)。断られた後も 1 つ目は普通に動く。閉じれば次が開ける。kill の後も開ける。
  エディタの起動と終了を 10 回以上くり返して「Doc full」が出ない。全アプリの起動と終了を数周。ミュートのまま。
- sim (標準・互換)、ブラウザ版、`rake test`、ビルド (TAB5 / NARYAv4 / S3 / Linux / wasm)。静的な D/IRAM が develop
  (NARYAv4 125,612 B) を超えない。

## 作業の決まり

- 作業ブランチ `feature/spinel-multi-instance` (本体の checkout)。コードは 1 つのコミット。自分が変えたファイルだけを
  パスで指定。push とマージはしない。`.env` は書き換えずコミットしない (scratchpad の env_before_multi.diff と一致)。
- sdkconfig、graphics-audio、submodule、Spinel のフォークは変えない。
- 実機 (/dev/ttyACM1、192.168.10.15) はミュートのまま。動いているアプリは止めてよい。最後にこの版を焼いて残す。
- 親から「実機の操作を止めて」と伝えられたら、すぐ止める。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 上に書いた以外に、利用者から見た動きを変える必要が出たとき (案として返す)。
