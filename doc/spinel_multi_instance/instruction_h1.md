# 作業指示書 H1: 開き直したときに消えたメモリを指す 2 つの領域 (sp_brk_stack / sp_fstr_tab)

対象: 実装担当のサブエージェント。前提: plan.md、report/i1.md の 6 章 1。report は `report/h1.md` へ。

## 問題 (I1 で見つけた、ユーザが対応を決めた 2026-10-05)

`spinel_rt.h` (フォーク kishima/spinel fmrb-ext、clone `/home/kishima/fmrb/wt/spinel-rebase`) の次の 2 つは、
生成プログラムの中で初めて使うときに、そのインスタンスの領域から確保され、インスタンスの開始の戻し
(`sp_reset_tu_statics`) でも NULL に戻されない。インスタンスが終わると領域ごと消えるので、**同じプログラムを 1 つずつ
開き直すだけで**、2 回目は消えたメモリを指す。

- `sp_brk_stack` (と同時に確保される `sp_brk_val` / `sp_brk_serial` / `sp_brk_exc_top` などの仲間): ブロックからの `break`
  (spinel_rt.h 11102 行付近)。
- `sp_fstr_tab` (と `sp_fstr_cap` / `sp_fstr_len`): `-"str"` の凍結文字列の表 (1121 行付近)。

今の Family mruby の生成プログラムではどちらもリンクされていない (I1 で nm により確認) が、エディタやアプリの Ruby に
ブロックからの `break` か `-"..."` が入った時点で表に出る。

## T1: 再現

- まず再現を作る。小さな Spinel の試しのプログラム (または gem の試しのコード) で、ブロックからの `break` と
  `-"str"` を使い、同じプログラムを**閉じて開き直す**のを数回くり返して壊れることを、sim (と実機 P4-Nano) で確かめる。
  フォークの試験 (`make test` / multi_ctx の試験) に足せる形なら、そちらでも再現する。
- ほかに同じ形 (遅れて確保し、戻しで NULL にしない) の領域が spinel_rt.h やライブラリにあれば洗い出す。

## T2: 直す (フォーク)

- インスタンスの開始の戻しで、これらを NULL / 0 に戻す (遅れて確保する形はそのまま)。または、インスタンスごとの
  文脈 (`sp_ctx`) の管理に入れる。どちらが自然かを、上流の SP_MULTI_CTX の作り (docs/internals/multi-instance.md、
  `sp_reset_tu_statics` の生成) に合わせて選び、理由を report に書く。
- フォークの `fmrb-ext` にコミットする (push はしない。SPINEL_PIN の更新と push は親の判断)。フォークの試験
  (`make test`、multi_ctx の試験) を通す。
- fmruby-core 側は `components/fmrb_spinel_rt/import_from_fork.rb` で実行時のスナップショットを取り込み直す (clone の
  パスから取り込むと IMPORT_INFO の fork_dir がそのパスになる。最後に親が vendor/spinel から取り込み直すので、
  その旨を report に書く)。
- **上流 PR の候補として整理する**: 上流の SP_MULTI_CTX でも、同じプログラムを順に開き直すことは想定内のはず
  (「同時に 2 つ」は対象外だが「終わってから次」は違う)。上流の main で同じ問題が起きるかを確かめ、起きるなら
  `doc/spinel_upstream_ext/pr_candidates.md` に候補を足す (理由は上流の関心: 正しさ。小さなメモリなどの動機は書かない)。

## 検証

- T1 の再現が、直した版で起きないこと (sim と実機)。
- 生成プログラム (カーネル・エディタ・gem) の動きが変わらないこと: sim (標準・互換) でエディタの起動と打鍵、
  アプリの起動と終了を数周。`rake test`。ビルド (TAB5 / NARYAv4 / S3 / Linux / wasm)。静的な D/IRAM が develop
  (NARYAv4 125,612 B) を超えない。

## 作業の決まり

- 作業ブランチ `fix/spinel-lazy-slots` (fmruby-core、親が develop から切った)。本体の checkout で作業する。
  `.env` は書き換えずコミットしない (scratchpad の env_before_lazy.diff と一致)。`rake clean_all` は使ってよい。
- sdkconfig、graphics-audio、submodule は変えない。
- 自分が変えたファイルだけをパスで指定してコミット。push とマージはしない (フォークも)。最後にこの版を実機に焼く。
- 実機 (P4-Nano、/dev/ttyACM0、192.168.10.15) はミュートのまま。動いているアプリは止めてよい。シリアルは開き直さない。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 直し方に、利用者から見た動きの変更が要るとき。
- フォークの変更が大きくなる (生成器の広い範囲) とき。
- 再現しない、または原因が見立てと違うとき。
