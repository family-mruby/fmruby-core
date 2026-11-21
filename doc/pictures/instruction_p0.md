# P0 指示書: 絵の扱いの汎用化の調査と、API の案

> 2026-11-21 | 親セッションからサブ (Opus) への指示。**コードは書かない。文書だけ** (`doc/pictures/report/p0.md`)。
> ビルド・sim・web・端末は使わない (読むだけの調査。小さな確かめにホストの `ruby` や `rake ti:test` を使うのはよい)。
> 先に読むもの: doc/pictures/plan.md (全部)、doc/asterism_chat/report の c0 (3 章)・c3a・c3b・c3、
> 今のアプリの絵の処理 (`flash/app/modern/chat/stamps.rb`、`flash/app/tool/image_viewer.app.rb`、
> `flash/app/tool/sprite_editor.app.rb`、`main/ruby/default_app/appstore.app.rb` の絵の扱い)、
> `sig/fmrb_gfx.rbs`・`fmrb_sprite.rbs`・`fmrb_tile.rbs` と sig/README.md、`lib/gems/picoruby-fmrb-bmp332/`、
> `lib/gems/picoruby-fmrb-gfx/` と `picoruby-fmrb-app/` (mrblib と C、Spinel の基底の側も)、描画側の 2 か所
> (隣の `../fmruby-graphics-audio` の `main/graphics/` の絵の読み込み・スプライト・`draw_tile` の処理、fmruby-core の
> `display_local_task.cpp` とその周り)、線の形 (`components/fmrb_wire/include`、`rake wire:check`、版の扱い)、
> doc/reference/gfx 系の資料 (描画境界の課題と制約: doc を grep)、doc/reference/internal_ram_budget.md。

## 調べて書くこと

1. **今の経路の図**: 絵がファイルから画面に出るまでの道を、機械ごと (Retro・sim / P4・Tab5 / ブラウザ版 / ネイティブ版) に。
   どこで PNG を解くか、どこに画素を持つか、枚数の上限 (`create_image` 8・スプライトの絵 64) がどこで決まっているか。
2. **描画側の透明**: 2 か所の PNG の読み方 (使っているライブラリ・α と tRNS の扱い)、`png332.c` と同じ決まり (α < 128 を `0x01`)
   にする直しの形と大きさ。BMP は。今ある絵 (flash/ の PNG 全部) で、直に読んだ結果が変わるものの一覧。
3. **拡大して描く**: `draw_tile`・`draw_image`・スプライトの命令に整数倍を足す案。線の形に引数を足すのか、新しい命令か。
   プロトコルの版と、古い graphics-audio のファーム (Retro の子マイコン) との組み合わせで何が起きるか。足す場所の一覧。
   描画側の時間 (1 点ずつか、行ごとか)。
4. **画素から直接スプライトの絵を作る**: 今 `SpriteImage.new(width:, height:)` + `set_target` + 描く、でできること・できないこと。
   RGB332 のバイト列を渡す命令を足す案 (1 回の命令の大きさの上限、分けて送るか)。
5. **`BMP332.info`・`to_png`**: C の中で済ませる形、引数 (上限) と答え、誤りの扱い。VM プールの使用量が今の
   `parse` → `encode_png` と比べてどれだけ減るかの見積り。
6. **`FmrbGfx` の絵の置き場の管理**: API の案 (`picture`・`draw_picture`・離し方・上限の決め方・透明の市松)。mruby のアプリと
   Spinel の基底の両方にどう載せるか。絵を使わないアプリの VM プール・内蔵 RAM が増えない形。`create_image` と
   スプライトの絵のどちらに寄せるか。機械全体で共有の枚数を、複数のアプリが使うときの扱い。
7. **sig の案**: 足す・変えるメソッドを `.rbs` の形で (doc コメントの 1 行目つき)。**名前と引数で選ぶ所は、選択肢 (推奨つき) に
   して並べる** (利用者に見える所なので、親がユーザに聞く)。
8. **移す先と削る所の一覧**: 4 つのアプリの、どの関数・何行が、どの API に替わるか。替えた後に残るもの。
9. **段取りの見直し**: plan.md の P1〜P4 の分け方と順番でよいか。Retro の子マイコンの焼き直しが要るか。回帰の道具の基準
   (描画の記録・size・run) のどれが動くか。見積り (段ごとの大きさ)。
10. 確認していないこと・分からなかったこと。

## 範囲と止まる条件

- 触ってよい所 (fmruby-core): `doc/pictures/report/p0.md`、plan.md の未確定事項で決まったものの反映、`rake docs:index` の結果。
  ブランチ `asterism-chat` の上にコミット (文書だけ。push しない・マージしない・`git add -A` を使わない)。
- **触らない**: コード全部 (fmruby-core・graphics-audio・ほか)、ビルドの生成物、sim・web・端末・VPS。
- 止まる条件は特に無い (調査だけ)。分からない所は「分からなかった」と書く。推定と確かめた事実を分ける。
- 呼び名: 「端末」「Window」「描画側」。
