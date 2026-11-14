# T4 指示書: 描画の原点を「アプリが描いてよい区画」にする

> 親: plan.md の T4 と「決めたこと」(原点の決まり、枠なし・全画面・高解像度、守ること 4 つ)。材料は report/t0.md の T4 の節。

## やること

1. **原点を C の gfx に持つ**: VM の pool にある gfx の構造体に原点 (と区画の大きさ) を持たせ、`@gfx` の描画・文字・画像・
   スプライト・キャンバス・クリップが原点を足す。枠のある Window は user area、枠なし (`window_frame = "none"`) は Window の左上、
   全画面・高解像度は画面の左上。区画の形が変わる時 (枠の切り替え・全画面の出入り・`on_resize`・checkpoint の復元) に基底が持ち直す。
   mruby と Spinel と Python の区画の計算の重複 (t0 の一覧) は 1 か所にまとめる。
2. **`@window_gfx`**: 原点が Window の左上の 2 つ目の描き手。基底の枠描き (`draw_window_frame`)・desktop の `@_bg_canvas`・
   枠を自分で描くアプリが使う。
3. **イベントの座標**: マウス・タッチ・ドラッグの `ev[:x]`・`ev[:y]` を同じ原点にそろえ、Window 座標は `ev[:wx]`・`ev[:wy]` に残す。
   FmrbUI も同じ原点にし、全画面の出入りで原点が古いまま残る今の不具合も直す。
4. **全アプリを入れ替える**: `@user_area_x0 +` / `@user_area_y0 +` を消す (101 ファイル)。t0 の「絵が変わるもの」(desktop、
   robo_explorer の `BOARD_Y=13`、タイトルバーに重なっていた試験 8 本、閉じるボタンの当たりを真似ている stackchan・picoruby) を
   一つずつ扱う。`draw_window_frame` を呼んでいるアプリ 64 本は、基底に任せてよいものは消す。
5. **前後で全アプリを撮って比べる道具** (一回きり、`tools/regress/` に置いてよい): 変更前の木と変更後で、ランチャーから起動できる
   全アプリを sim (Retro・Modern) で起動して撮り、画素で比べる。違いが出たアプリは一覧にし、意図した違い (タイトルバーに重なって
   いた試験アプリが中に収まる、など) か不具合かを書く。
6. skill fmrb-app-new・sig・文書の座標の説明を直す。

## 確かめること (これだけ。過剰に流さない)

- `rake regress` (描画の記録が変わるなら、変わり方を report に書く。基準の更新は親)。
- 5 の前後の比べ (要点)。
- `rake regress:screens` (基準を**更新せずに**通ること)。
- `rake regress:checkpoint` (移った先での原点と `on_resize`)。
- `rake regress:run` を最後に 1 回 (VM プールが変更前と幅の中。C の構造体に足した分の増減を数字で)。
- `rake regress:size` を最後に 1 回 (内蔵 RAM の静的な増分 0)。
- `rake regress:spinel` (Spinel の基底を変えるので)。
- 標準構成でエディタを起動して 1 打鍵。
- 直しのやり直しでは、変えた所が影響する道具だけを流し直す。

## 触ってよい範囲

- fmruby-core (develop) と skill (`.claude` の SKILL.md は自分の直しだけをコミット)。graphics-audio は触らない (C の描画命令に
  原点を足す必要があると分かったら止まって返す)。作業ツリーの既存の未コミット (core の components/ サブモジュール 2 つ、
  wasm/vendor/、doc/wasm_apps/、doc/README.md の wasm_apps の分) には触らない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。
- 終わったら Linux のビルド (x86-64)、sim・web・ネイティブ版を片付け、**裏で走らせた処理を残さない** (`ps`・`docker ps` で確かめる)。

## 止まる条件

- 仕様が変わる選択 (アプリの見た目が意図せず変わる、API の意味が変わる)。VM プールが KB 単位で増える。内蔵 RAM の増分。
  graphics-audio の変更が要る。

## report (report/t4.md)

原点の持ち方、API ごとの原点の表、入れ替えた数、前後の比べの結果 (違いの一覧と判断)、確かめた結果と VM プール・内蔵 RAM の数字、
T5 以降への注意。
