# 作業指示書 S2c: アプリ終了時の二重解放を直し、estalloc の統計だけを残す

対象: 実装担当のサブエージェント。前提: plan.md、report/s2.md、**report/s2b.md** (分け方、計測、止まった理由)。
scratchpad の `s2b/` に S2b の変更 (`change.diff`、`estalloc.patch.c`)、計装 (`instrumentation.diff`)、
不正な解放の数え方 (`../s2b_est_rejects.diff`)、計測の道具がある。report は `report/s2c.md` へ。

## 決定 (ユーザ、2026-10-02): この順で進める

1. 二重解放を直す。
2. そのうえで S2b の変更 (統計を `ESTALLOC_DEBUG` の外へ、`ESTALLOC_DEBUG` を全ビルドで外す) を入れる。
3. `mrb_close` を呼ばない件は、1 の結果を見てから別に扱う (この段階では変えない。分かったことを書く)。

## T1: 二重解放を直す

- `main/app/fmrb_app.c` の `mrc_create_task` の後の `mrc_irep_free(cc, irep_obj)` (S2b で特定) をやめる。
  irep は `mrb_proc_new` 経由で VM の GC の持ち物になる。上流の picoruby の同じ経路 (タスクを作った後に irep を
  どう扱うか) を読んで、合わせる。`mrc_ccontext_free` など前後の後始末は、持ち主を確かめてから残す・外すを決める。
- 同じ形の解放がほかの経路 (Spinel のアプリ、BASIC・Lua・MicroPython の起動、eval、irb、sandbox) に無いか
  洗い出す (`mrc_irep_free`、`mrb_irep_decref` の呼び出しを grep)。
- **確かめ方**: 点検を残した今の作り (`ESTALLOC_DEBUG` あり) で、S2b の数え方 (`s2b_est_rejects.diff` を一時的に
  当てる) を使い、アプリを閉じたときの不正な解放が 0 回になること。対象: Shell、MML、BlockGame (ゲーム)、
  Monitor、FM-Editor (Spinel)、Spinel のアプリ 1 本、ユーザの mruby のアプリ 1 本、irb、sandbox。
  起動と終了を数回くり返して、プールの使用量が戻ること (漏れていないこと) も見る。
- `mrb_close` の注記 (「`mrc_irep_free` の後に呼ぶと落ちる」) について、直した後に呼んだらどうなるかを試すのは
  よい (コミットはしない)。結果を report に書く。

## T2: 統計だけを残す

- T1 をコミットしてから、S2b の `change.diff` を当てる (lib/patch の estalloc.c、mrbgem.rake、alloc.c、
  family_mruby_linux.rb / wasm.rb)。
- 実機 P4-Nano で、アプリの起動と終了 (T1 と同じ対象) をくり返してクラッシュしないこと、Shell の打鍵と GC の時間が
  S2b の B2 と同じ程度であること、統計 (VM Pools、`FmrbApp.pool_usage`) が出ること。

## 検証

- ビルド: TAB5 / NARYAv4 / S3 / Linux (標準・互換) / wasm。静的な D/IRAM が develop を超えない。`rake test`。
- sim の標準構成と互換構成: 起動、エディタの起動と 1 打鍵、Shell の打鍵とコマンド、アプリの起動と終了を数回。
  ブラウザ版: 起動とアプリの起動・終了。
- Retro (S3) は実機が無ければビルドだけ (report に「未」)。

## 作業の決まり

- 作業ブランチ `feature/shell-gc` (本体の checkout)。T1 と T2 を別のコミットにする。自分が変えたファイルだけを
  パスで指定。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_shell_gc2.diff と一致する。
- sdkconfig、graphics-audio は変えない。submodule は直接編集しない (lib/patch)。lib/ を変えたら `rake clean`。
- 実機は**ミュートのまま** (`curl -X POST "http://192.0.2.15/audio/mute?on=1"`)。USB が切れたら `rake attach`。
  ファイルを flash に繰り返し書く試験はしない。終わったら develop 相当でなく、T2 まで入った版を焼いて残す
  (親が目視の確認に使う)。
- 親から「実機の操作を止めて」と伝えられたら、すぐ止める。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- `mrc_irep_free` をやめても不正な解放が残り、原因が別の経路 (VM の作り、mruby の内部) にあるとき。
- irep の持ち主の扱いを変えるのに、mruby / picoruby の submodule の変更が要るとき (lib/patch の範囲を越えるなら案を返す)。

## report に書くこと

二重解放の原因と直し方、同じ形の箇所の洗い出し、不正な解放の数の前後、プールの戻り、`mrb_close` を試した結果、
T2 の前後の表、回帰の確認、残件。
