# F0・F1 指示書: storage から外すものを外し、試験のアプリは WiFi で送る

> 親: plan.md の段 F0・F1 と未確定事項 1 (決まった)。WiFi の道具は doc/remote_guard (G0、report/g0.md)。時間の目安は
> doc/regression/usage.md 0 章。

## やること

1. **F0 (A)**: 歩数アプリ (`app/demo/walk_scroller*` と `usr/share/sprites/walk_scroller`) をふだんの storage に入れない
   (全端末。今 Retro だけ外している設定を全端末に)。使うときに入れる道を作る: App Store に載せられるならそれ
   (App Store の仕組みを読んで決める)、無理なら `tools/fmrb_rd_fs.rb push` で入れる手順を文書に。sim・ブラウザ版の扱い
   (回帰の場面で使っていないか) を確かめる。
2. **F0 (B)**: 端末の画面に合わない大きさの背景を、端末ごとの除外の設定 (`config/storage_exclude_*.txt` か、より良い
   仕組みがあればそれ) で外す。どの端末がどの大きさを使うかはコード (背景を選ぶ所) を読んで決める。参照の無い
   `BG_sample.png` は消す (本当に参照が無いことを grep と sim で確かめる)。
3. **F0 (C)**: スライドの見本 (`usr/share/samples/slides`) の動画と専用の画像をやめる。スライドの見本は文字だけで成り立つ
   ように直し、画像の読み込みの見本は既にある背景の画像 (`usr/share/backgrounds/` の端末に合うもの) を使う。
4. **F1**: 試験用のアプリ (`app/test`) と試験用の音 (`usr/share/sounds/sine440_16k.wav` など試験だけのもの) を、実機の
   storage の像から外す。sim・ネイティブ版・ブラウザ版で回帰の道具が使う所は今のまま動くこと (sim は flash/ を
   直接見るか、像を作るかを確かめる)。実機で試験のアプリが要る道具 (`regress:boot`、MCP の tab5_* の手順、
   checkpoint の実機の場面など) は、使う前に WiFi で送り (G0 の `push`/`sync`)、試験のまとまりの終わりに消す。空きが
   一定より少なければ送る前に古いものから消す (未確定事項 1 の決定)。
5. 公開のブラウザ版の束 (`wasm:dist`・`wasm:site`) も同じ考えで外れること。
6. 減った量を表に: 端末ごとの storage の像のファイル数・データ量・littlefs での使用 (前後)。

## 確かめること

- `rake regress`・`rake regress:screens` (全部。外したことで基準が変わる場面は理由を書く。基準の更新は親)。
- `rake regress:run`・`rake regress:checkpoint`・`rake regress:live` (試験のアプリを使う道具が通ること)。
- 実機 (P4-Nano) で、試験のアプリを送って使い、消すところまでの通しを 1 つ (親が焼くか、サブが WiFi の道具だけで行う。
  シリアルと flash は親)。
- 速さの確認は Ruby のエンジンだけでよい (ユーザ)。小さな差は理由を書けば止まらない (ユーザ)。

## 触ってよい範囲

- fmruby-core (develop) と family-mruby の tools/。graphics-audio は変えない。既存の未コミット (components/ の
  サブモジュール 2 つ、wasm/vendor/) には触らない。シリアル・flash・/dev/tty* は使わない (P4-Nano の WiFi の口は使ってよい。
  端末の /home には触らない)。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。

## 止まる条件

- 仕様が変わる選択 (上の決定以外で、アプリの見た目・中身が変わる)。ユーザの作業が要るとき。

## report (report/f01.md)

外したもの、入れる道、試験の道具の変化、減った量の表、確かめた結果。開始と終了の時刻は `date` で書く。
