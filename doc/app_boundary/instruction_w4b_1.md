# W4b-1 指示書: 名前の決定を入れ、FmrbGfx の状態を境界の下へ移す (worktree で並行)

> 親: plan.md の作るもの 2・段 W4b、決定 17、「W4a で決めたこと」、未確定事項。前段は report/w4a.md (3〜7 章、
> 特に 7 章「W4b への注意」)。時間の目安は doc/regression/usage.md 0 章。

## 作業場所 (並行のため、いつもと違う)

本体の checkout では別のサブ (doc/boot_splash の B2、sim を使う) が動いている。**この段は worktree で行う**:

```
git -C /home/kishima/fmrb/family-mruby/fmruby-core worktree add /home/kishima/fmrb/wt/w4b1-core -b wt/w4b1 develop
```

- 作業・ビルド・コミットはすべて worktree の中 (`wt/w4b1` ブランチ)。本体の checkout・build/・sim・docker の
  コンテナ (fmruby_core など) には触らない。**sim・ネイティブ版・ブラウザ版の MCP ツールは使わない** (1 組しか無く、
  マウント先は本体の checkout)。
- 使えるのは worktree の中の `rake build:linux`、`rake regress` (ホストの試験と描画の記録)、`rake regress:spinel`、
  `ruby tools/wamr_poc/run.rb boundary`。sim で見る確認 (screens・run・checkpoint・エディタの打鍵) は親が develop に
  取り込んでから流す。
- submodule (components/ の下) が worktree で空なら、本体の checkout の中身を参照する形で用意してよい
  (submodule を書き換えない)。用意の仕方は report に。

## やること

1. **checkpoint への影響を調べる** (コードを読む。止める理由になるかの判断材料): W4a で mruby の gem の C のメソッドの
   名前・数が変わった。movable のアプリが呼ぶメソッドの表 (`lib/upstream/family_mruby_ckpt_table.rb`)、メソッド名の
   シンボルの扱い、端末間の互換の判定を読み、W4a の前後の版の間でアプリを運べるか、運べないならどこで弾かれるか
   (黙って壊れないか) を report に。直しが要るなら、仕様が変わらない直しだけ入れる。
2. **C の名前の決定を入れる** (2026-11-15 ユーザ): 本体の中の実体と生成するつなぎは `fmrb_bnd_<クラス>_<メソッド>`。
   C のアプリから見える名前 (SDK のヘッダ・wasm の import の名前) は `fmrb_<クラス>_<メソッド>` のまま。W4a で
   避けるために改名した内部の 2 つ (`fmrb_gfx_set_canvas_sprite_clip`・`fmrb_gfx_canvas_region_t`) を元の名前に戻す。
   ABI の確認 (wamr_poc の引数のハッシュ) が変わらないこと。
3. **FmrbGfx の状態を境界の下へ** (決定 17、report/w4a.md 7 章): 描画の原点を Spinel と MicroPython でも C の側に
   移し、包みが足し算を持たない形にする (mruby・Lua と揃える)。W4a で「仮の形」とした原点の渡し方を決まった形に。
   sprite に描いている間の `set_sprite_clip` の原点の扱いも決めて書く。canvas_id・大きさ・現在のフォントなどの
   状態を、エンジンのオブジェクトと C のどちらが持つかを整理し、Spinel と mruby の包みを揃える。
4. **計測 (最初のまとまりだけ)**: 境界の関数を直接呼ぶことで mruby がどれだけ変わるか。代表の関数 (fill_rect・
   draw_text・present) の呼び出しの命令数 (doc/reference/vm_speed_measurement.md の測り方)、Hash の確保の回数。
   W4a で present が 1 段深くなった分も。デスクトップと標準アプリの呼び出しを置き換えるかは、この数字を見て親と
   ユーザが決める (この段では置き換えない)。

ほかのクラス (アプリと Window・時刻とログとクリップボード・ファイル・音) は次の段。

## 確かめること (worktree の中で、これだけ)

- `rake regress` (35 秒)。sig を直したら `rake clean` してからビルド。
- `rake regress:spinel` (20 秒)。
- `ruby tools/wamr_poc/run.rb boundary`。
- `rake build:linux` が通り、`file build/fmruby-core.elf` が x86-64。
- sim で見るべき所 (原点が変わった描画・エディタ・Lua・MicroPython のアプリ) を一覧にして report に書く
  (親が取り込み後に流す)。

## 触ってよい範囲

- worktree の fmruby-core だけ。graphics-audio は変えない。
- コミットは `wt/w4b1` に。件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。
  基準の更新はしない。
- 終わったら worktree の中で `rake clean_all` して build/ を消す (worktree 自体は親が取り込んだ後に消す)。
  裏の処理を残さない。

## 止まる条件

- 仕様が変わる選択 (アプリの書き方・名前・既定値)。checkpoint で黙って壊れる経路があり、直すと仕様が変わる場合。
  内蔵 RAM の増分。ユーザの作業が要るとき。本体の checkout や sim を使う必要が出たとき。

## report (report/w4b_1.md)

checkpoint の調べ、名前の入れ替え、状態の置き場の表 (前と後、エンジンごと)、計測の数字、sim で親が見る所の一覧、
W4b の次の段への注意。開始と終了の時刻は `date` で書く。
