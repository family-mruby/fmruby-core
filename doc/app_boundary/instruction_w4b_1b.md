# W4b-1b 指示書: W4b-1 の後に決めた 3 つを入れる

> 親: plan.md の「W4b-1 の後に決めたこと」(2026-11-15 ユーザ)。前段は report/w4b_1.md。時間の目安は
> doc/regression/usage.md 0 章。

## やること

1. **mruby の `present` を C に戻す**: mruby の FmrbGfx の `present` を C のメソッドにし、中で `present_raw` の実体を
   呼ぶ。引数の形 (省略・`x, y` を渡す形) は今の包みと同じ。手書きの包みの例外として、生成の道具・sig・
   report/w4b_1.md の計測の表の扱いと矛盾しないようにする (道具が「包みは Ruby」と前提していれば例外の書き方を
   決めて書く)。命令数を W4b-1 と同じ測り方 (scratchpad の bench、report/w4b_1.md の計測の節) で測り、620 前後に
   戻ることを確かめる。
2. **描き手の上限を全エンジンで例外に**: 9 個目の描き手を作ろうとしたとき、Spinel も mruby・MicroPython と同じく
   例外にする (黙って描かないのをやめる)。Lua も同じ扱いか確かめ、違えば揃える。例外の文言は揃える。
3. **版をまたぐ checkpoint の穴を mruby-checkpoint の側で塞ぐ**: 止めた瞬間に Task が gem の mrblib の包み
   (版で中身が変わるもの) の中にいると、復元後に NoMethodError になる件 (report/w4b_1.md の checkpoint の節)。
   運ぶ前 (または復元の前) に、送り手と受け手で中身の違う mrblib のフレームを見つけて、分かる言葉で断る。
   - gem は `/home/kishima/fmrb/family-mruby/tmp/mruby-checkpoint` (公開リポジトリ family-mruby/mruby-checkpoint の
     checkout)。fmruby-core は `vendor/mruby-checkpoint` の固定した版を使う。開発中は `MRUBY_CHECKPOINT_DIR` で
     その checkout を指して試す (Rakefile の説明)。
   - gem の変更は tmp/mruby-checkpoint にコミットまで (試験つき、gem の README・文書も)。**push とピンの更新は
     しない** (ユーザの指示の後に親がする)。gem の側の試験 (tmp/mruby-checkpoint の rake など) を流す。
   - どの「中身の違い」で断るかの判定 (mrblib のハッシュ・版の番号など) は、今ある互換の判定 (SND0 など) の作りに
     合わせて決め、report に書く。黙って通る経路を残さない。

## 確かめること (これだけ)

- `rake regress` (35 秒)、`rake regress:spinel` (20 秒)。
- 1: 命令数の前後。2: 9 個目で例外になることを各エンジンで (ホストの試験か sim)。
- 3: `MRUBY_CHECKPOINT_DIR` で gem の checkout を指してビルドし、`rake regress:checkpoint` (7 分) が通ること。
  断る経路は、版の違う mrblib のフレームを作る試験 (gem の側か regress の場面) で確かめる。
- 最後に 1 回: `rake regress:screens ONLY=...` は `present` を使う画面に絞ってよい。`regress:run` は mruby の
  アプリの VM プールが揺れの幅の中か。
- 終わったら `MRUBY_CHECKPOINT_DIR` を外した (固定の版の) Linux のビルドに戻す。

## 触ってよい範囲

- fmruby-core (develop) と tmp/mruby-checkpoint。既存の未コミット (components/ のサブモジュール 2 つ、wasm/vendor/) には
  触らない。ピン (`vendor/` の版を決めるファイル) は変えない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。
- 終わったら Linux のビルド (x86-64)、sim を片付け、裏の処理を残さない。

## 止まる条件

- 仕様が変わる選択 (決めたこと以外で、アプリの書き方・運べるアプリの範囲が変わる)。gem の公開の API を変える必要。
  内蔵 RAM の増分。ユーザの作業が要るとき。

## report (report/w4b_1b.md)

3 つの直し、命令数の前後、断る判定の作りと試験、gem のコミット (ピンを上げるときに要る情報)、確かめた結果。
開始と終了の時刻は `date` で書く。
