# 作業指示書 E1 / E2: mrb_close と、同じ Spinel アプリの同時起動

対象: 実装担当のサブエージェント。前提: plan.md、doc/shell_input_lag/report/s2c.md (「`mrb_close` を試した結果」
と「残件」)。scratchpad の `s2c/` に試験の道具 (`cycle.sh`、`dbl_editor.sh`、試験用アプリ、`T2meas.elf`) がある。
report は `report/e1.md` (E1 と E2 を 1 つに。節を分ける) へ。

## E1: mruby アプリの正常終了で mrb_close を呼ぶ

- `main/app/fmrb_app.c` の `destroy_vm` (正常終了の後始末) で、上流と同じ順番 (`mrb_close` → `mrc_ccontext_free`)
  にする。強制終了の経路では呼ばない (理由をコメントに)。上流の r2p2 / picoruby の終了の順番を読んで合わせる。
- 測る: 終了にかかる時間 (Shell、MML、BlockGame、Monitor、Spinel の gem を使う mruby アプリ、eval / sandbox の
  試験アプリ)、プールの戻り (起動・終了を数周)、不正な解放 (S2b の数え方を一時的に当てて 0 回)。
- 先に E1 をコミットする。

## E2: エディタ (Spinel) の同時起動の abort

- 再現の条件を固める (長く動かした後、ExcHW の残り方、sim でも出るか)。`T2meas.elf` で abort の場所が解ける。
- 何が 2 つのインスタンスで共有されているかを、生成 C (エディタの combined の C) と Spinel の実行時
  (`SP_MULTI_CTX`、`sp_reset_tu_statics`、`SP_TU_BSS`) を読んで確かめる。`ESTALLOC_DEBUG` を外して
  (S2c の T2) から出やすくなった理由も確かめる。
- 直し方を plan.md の候補から選び、理由を report に書いてから直す。フォーク (kishima/spinel の fmrb-ext、
  clone `/home/kishima/fmrb/wt/spinel-rebase`) の変更が要るなら、そこにコミットして (push しない)
  `import_from_fork.rb` で取り込み直す。SPINEL_PIN の更新は親の判断。変更が大きければ、(1) 同時起動を止める
  手当てを先に入れて、根本の案を返す。
- 同じ形の問題が、ほかの Spinel アプリ (カーネルは 1 つなので対象外、エディタ、Spinel の gem) で起きうるかも書く。

## 検証

- 実機 P4-Nano (NARYAv4): E1 の測定、E2 の「2 つ開く → 閉じる → 3 つ目」を長く動かした後も含めて 10 回以上。
  Guru 0。
- sim (標準・互換): 起動、エディタの起動と 1 打鍵、アプリの起動と終了を数周、E2 の手順。ブラウザ版: 起動と
  アプリの起動・終了。`rake test`。
- ビルド: TAB5 / NARYAv4 / S3 / Linux / wasm。静的な D/IRAM が develop を超えない。

## 作業の決まり

- 作業ブランチ `feature/app-exit` (親が develop から切った)。本体の checkout で作業する。E1 と E2 は別のコミット。
  自分が変えたファイルだけをパスで指定。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_app_exit.diff と一致する。
- sdkconfig、graphics-audio は変えない。submodule は直接編集しない。
- 実機は**ミュートのまま** (`curl -X POST "http://192.168.10.15/audio/mute?on=1"`)。USB が切れたら `rake attach`。
  ファイルを flash に繰り返し書く試験はしない。試験のファイルは消す。最後に E2 まで入った版を焼いて残す。
- 親から「実機の操作を止めて」と伝えられたら、すぐ止める。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- `mrb_close` で落ちる・不正な解放が出る (別の二重解放がある) とき。場所を記録して返す。
- E2 の原因が Spinel の作りの大きな変更を要するとき (手当てを入れて案を返す)。
