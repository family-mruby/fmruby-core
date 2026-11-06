# 移した窓の枠が書いた側のテーマのまま (F3 の直し)

> 状態: 実装済・検収待ち | 更新: 2026-11-06 | テーマの違う端末へ窓のアプリを移す (または復元する) と、窓の中身は着いた側のテーマで描き直されるのに、タイトルバー・閉じるボタン・縁だけが書いた側のテーマのまま残った。原因は、基底クラスが窓の枠を `GfxBlock` にまとめるときテーマの色が手順に焼き込まれ、復元ではその手順をそのまま定義し直していたこと。復元のとき枠の `GfxBlock` は運んできたものを捨て、着いた側のテーマで作り直すようにした。`rake regress:checkpoint` にテーマの違うネイティブ版へ移す場面 (window theme) を足し、直す前に落ち、直した後に通ることを確認した

## 1. 症状

デモの撮影で、ネイティブ版 (cyberpunk) から ESP32 (明るいテーマ) へ窓のアプリを移すと、着いた側のタイトルバーが cyberpunk の青のままだった。窓の中身 (`clear_user_area` の地の色、`theme_fg` の文字) は着いた側の色で描き直されていた。

## 2. 原因

- `FmrbApp#_build_frame_block` は窓の枠 (タイトルバー・メニューの印・題・閉じるボタン・縁) を 1 つの `GfxBlock` にする。`GfxBlock` はブロックを 2 回評価し、値が変わる引数 (`w:`・`h:`) だけをレジスタにし、ほかは手順 (bytecode) の定数にする。`FmrbConst::THEME_MENU_BG`・`THEME_TEXT_LIGHT`・`THEME_BORDER` の値と題の文字は、作った時点の値で手順に焼き込まれる。
- 復元 (`_checkpoint_restored`) は `ObjectSpace` の `GfxBlock` を全部 `_checkpoint_redefine` し、運んできた手順 (`@_bc`・`@_st`) をそのまま表示側に定義し直していた。枠の `GfxBlock` も同じ扱いだったので、書いた側の色で描かれた。`_build_corner_clear_block` (角の外側を色キーで塗り直す) も同じ形 (焼き込むのはテーマでなく色キー 0x01)。
- `FmrbConst::THEME_*` そのものは着いた側の値になる。定数は VM を開いた時点の基準に入っており、イメージの変更表に出ない (gem の format_v1.md の 9 章) ので、着いた側の VM が自分の設定から定義した値のまま。描くときに定数を読む部品 (リサイズの三角 `draw_resize_mark`、閉じるボタンを押して外で離したときの描き直し、`clear_user_area`、アプリの `theme_*`) はもともと着いた側の色になる。手順に値を焼き込む `GfxBlock` だけが外れていた。親の見立てと同じ。

## 3. 直し方

- `GfxBlock#_checkpoint_drop` を足した。プログラムの ID を忘れて使えない印を付けるだけで、表示側には何も送らない (古い ID は書いた側のもので、着いた側ではほかのアプリのプログラムを指しうるので消さない。`_checkpoint_redefine` が定義し直せなかったときと同じ扱い)。
- `_checkpoint_restored` は、`GfxBlock` を定義し直す前に基底クラスの 2 つ (`@_frame_block`・`@_corner_clear_block`) を `_checkpoint_drop` して nil にする。続く `ObjectSpace` の定義し直しは、印の付いたものを飛ばす (アプリが作った `GfxBlock` は今までどおり定義し直す)。合成の範囲などを戻す所で、新しく起動した窓と同じく全画面でなければ `_build_frame_block` を呼び、着いた側のテーマと大きさで枠と角の 2 つを作る。全画面で着いたアプリは、窓に戻ったときの `draw_window_frame` がその時に作る (起動したときと同じ)。
- 基底クラスがテーマの色を焼き込む `GfxBlock` はこの 2 つだけ (`lib/add/picoruby-fmrb-app/mrblib` の `GfxBlock.new` はこの 2 箇所)。
- アプリから見える振る舞いとイメージの形式は変わらない。

## 4. 試験

`tools/regress/checkpoint.rb` に window theme の場面を足した (ネイティブ版の場面、window move の後)。

- 3 つ目のネイティブ版 C (同じビルド、run dir・debug port 5587・利用者の場所は別) を起こす。C の利用者の場所の `/etc/system_conf.toml` は、ビルドの最初の起動用の etc (`native/build/root/etc`) の写しの `[theme]` だけを cyberpunk (`config_dialog.rb` の `CFG_THEME_PRESETS` と同じ値) に書き換えたもの。
- C で `ckpt_window` を新しく起動し、window の場面と同じクリック 2 つとキー 2 つを与えて窓を撮り、閉じる (着いた側で新しく起動した窓の見本)。見本のタイトルバーと縁が C のテーマの `menu_bg`・`border` であることを画素で確認する。
- A で `ckpt_window` を起動して同じ操作をし、タイトルバーと縁が明るいテーマであることを確認してから C へ移す。C で `on_restore` と Task の次の 1 歩を待ち、窓を撮る。
- 移した窓のタイトルバー (題と閉じるボタンを避けた所) と左の縁の画素が C の `menu_bg`・`border` であること、窓の範囲が見本と画素で同じこと (中身も着いた側のテーマで描き直されていること) を確認する。C のログに強制終了が無いことも見る。
- 画素の色は、RGB332 の値を 8 bit に伸ばしたものと各成分 ±8 で比べる (ネイティブ版の撮影は RGB565 を通るので少しずれる)。

直す前のコード (fmrb-app.rb・gfx_block.rb を c7242546 のまま) で流すと、この場面だけが落ちた:

```
standard: FAILED window theme: the frame kept the writing side's theme (title bar [222, 36, 82] for 0x22, border [107, 0, 0] for 0x0E; window_theme.png)
```

タイトルバーは明るいテーマの赤 (0xC5)、縁は 0x60 のまま、中身は cyberpunk の黒地で描かれていた。直した後は両方の構成で通った (§5)。

## 5. 確認

| 確認 | 結果 |
|---|---|
| `rake regress:checkpoint` (標準・全部 mruby) | 標準・全部 mruby とも ok (window theme: 標準でタイトルバー [33, 0, 173]・縁 [0, 109, 173]、見本と同じ)。1 回目は全部 mruby が `move no answer: held 2930 ms for a hold of 1500` で落ちた。足した場面より前の場面 (誰も答えない相手へ止めて待つ) の時間の幅で、この直しの経路 (復元) を通らない。`ONLY=mruby NO_BUILD=1` で流し直して通った |
| `rake regress` | ok (ホスト 21 本、描画の記録 205 回) |
| `rake regress:screens ONLY=retro,modern,native` | native・native-keys は 7/7 ok。retro・modern (sim) は流していない: sim の道具 (`sim_run.rb`) は docker compose を worktree の 1 つ上 (`/home/kishima/fmrb/wt`) で探し、`FMRB_ROOT` を見ないので worktree からは流せない (共有 checkout を使うことになる)。変えたのは復元の経路 (`_checkpoint_restored`) と復元でだけ呼ぶ `GfxBlock#_checkpoint_drop` で、普段の窓を描く経路は変えていない。なお `regress:screens` のネイティブ版も `screens/drivers.rb` が `FMRB_ROOT` を見ないので、そこだけ一時的に `FMRB_ROOT` を見るよう書き換えて流した (コミットしていない) |
