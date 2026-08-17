# Phase 6: 絵 (日本語表示・画像・スプライト)

## 目的

Python アプリの見た目を Ruby アプリと同じ水準まで持ち上げる。
中心は**日本語表示**と**スプライト**の 2 つ。

**下ごしらえは既にできている**。描画命令の組み立ては共通 C 層
(components/fmrb_gfx/fmrb_gfx_cmd.h) に一本化済みで、フォント指定・画像・
スプライト・タイルのどれも構築子が揃っている。このフェーズの仕事は、
その構築子を `_fmrb` から呼べるようにし、Python 側にクラスを被せることである。
graphics-audio 側には手を入れない。

## やること

### 1. 文字 (日本語)

| 追加する `_fmrb` 関数 | 対応する共通 C 層 |
|---|---|
| `gfx_set_font(cid, family, size=None)` | `fmrb_gfx_cmd_set_font` |
| `gfx_set_text_size(cid, size)` | `fmrb_gfx_cmd_text_size` |
| `gfx_draw_text` に `mixed` 引数を追加 | `fmrb_gfx_cmd_text_n` |

Python 側 (`FmrbGfx`):

- `set_font(family, size=None)` / `set_text_size(size)`
- `draw_text(x, y, text, color, bg_color=None, mixed=False)`
- `text_width(text)` を**バイト単位の走査**に書き換える。今は
  `len(text) * 6` で、ASCII しか合っていない。混在描画では ASCII が 6 画素、
  全角が 8 画素、半角カタカナが 4 画素になるので、UTF-8 の先頭バイトを見て
  足し込む。Ruby 側の同等品と同じ規則にすること。

**Python の文字列の扱いを先に確かめる**。今の設定は ROM レベル CORE で、
文字列が Unicode 対応かどうかで `len()` と添字の意味が変わる。
「バイト列として走査したい」ので、実装前に `"あ".encode()` と `len("あ")` が
何を返すかを実機とシミュレーションの両方で確認し、結果を report に書く。
必要なら走査は `encode()` した bytes に対して行う。

言語切り替え (i18n) は、Ruby 版の仕組みを丸ごと写すのではなく、
**最小限**にする: 起動時に `_fmrb.language()` (新規、`FmrbConst` の値を返す)
で "ja" / "en" を得て、アプリが自分の辞書 (dict) から引く。
共有の文字列表は作らない。

### 2. 画像

| 追加する `_fmrb` 関数 | 用途 |
|---|---|
| `gfx_sync_file(src, dest)` | 端末側のファイルを描画側へ送る (違うときだけ) |
| `gfx_create_image(cid, path)` -> dict | 描画側のファイルから画像を作る |
| `gfx_draw_image(cid, id, x, y, sx, sy)` | 描く |
| `gfx_delete_image(cid, id)` | 捨てる |

Python 側は Ruby 版と同じ形の `sync_file` / `create_image` / `draw_image` /
`delete_image` を用意する。**ファイルの中身を Python が読む必要はない**。
経路指定だけで済み、実際の読み込みは C が行う。送信の要否判定は
`sync_file` が持っているので、送信済みを問い合わせる関数は入れない。

### 3. スプライト

これがこのフェーズの本命で、phase9 のゲームの土台になる。

| 追加する `_fmrb` 関数 | 対応する共通 C 層 |
|---|---|
| `gfx_create_sprite_image(w, h, transparent, use_transparent)` | 画像バッファの確保 |
| `gfx_load_sprite_image_bmp(id, path)` | `fmrb_gfx_cmd_load_sprite_image_bmp` |
| `gfx_set_sprite_image_target(id)` | 描画先を画像バッファに切り替える |
| `gfx_delete_sprite_image(id)` | |
| `gfx_create_sprite_instance(ids, x, y, z)` | 配置を作る (**引数が配列**なので phase5 の msgpack 拡張が要る箇所) |
| `gfx_sprite_move(id, x, y)` / `_visible` / `_frame` | 動かす・見せる・コマ替え |
| `gfx_delete_sprite_instance(id)` / `gfx_delete_all_sprites(cid)` | |

Python 側に `SpriteImage` / `SpriteInstance` クラスを置く。Ruby 版と同名・
同じ引数にする。Ruby にある「ブロックを渡して描く」形 (`draw { |g| ... }`)
は、Python では `with` 文か `set_target` / `reset_target` の対で書く。
どちらにするかは実装時に決める。

**性能上の要点**: スプライトの合成は描画側で走るので、位置を変えるだけなら
Python の費用は `sprite_move` 1 本と `present` だけで済む。phase5 の計測で
「毎フレーム全部描き直す」のが重いと分かった場合、動くものはスプライト、
動かない背景は 1 回だけ描く、という作り方に倒す。この判断は phase9 の
設計に直結するので、結論を report に残すこと。

### 4. タイル (最小限)

`gfx_draw_tile` (`fmrb_gfx_cmd_draw_tile`) だけを通す。1 枚の画像から
矩形を切り出して貼る命令で、ブロック崩しのブロック描画にも使える。

**タイルマップのクラスは作らない**。Ruby 版の地図クラスは大きく、
共通層に置くと**全部の Python アプリの起動時間**を押し上げる (共通層は
毎回そのまま解析される)。必要になったときに、アプリ側の Python コードと
して書けばよい (地図データは phase5 の `read_file` で読める)。

## やらないこと

- 描画のまとめ送り (GfxBlock)。phase5 の計測しだいで、後のフェーズで判断する。
- 合成領域 (composite region) と表示範囲 (viewport)。
- 画面の一部だけを取り出す `get_pixel`、半透明の矩形。
- 共有の文字列表としての i18n。

## 確定した事項 (実装で決めた結果)

- **文字列はバイト列**だった (ROM レベル CORE では Unicode 文字列が入らない)。
  `len("日本語")` は 9。`text_width` のバイト走査はそのまま書けるが、
  将来 ROM レベルを上げても壊れないよう `encode()` を通してから走査する。
- スプライト画像への描画は `set_target` / `reset_target` の**対**にした。
  `with` 文は使っていない。
- `text_width` は共通層 (Python) に置いた。毎フレーム呼ぶものではなく、
  文字を描き替えるときにだけ要るため。
- **画像とスプライト画像は経路が別**。BMP の素材は `load_bmp`、一枚絵の
  PNG は `create_image`。取り違えても失敗せず空の画像ができるので、
  制限事項に書いた。

## 完了条件

1. 日本語混じりの文字列が Python アプリで正しく描け、`text_width` の値が
   実際の描画幅と一致する (画面を撮って目視 + 値の突き合わせ)。
2. BMP を読み込んだスプライトが表示され、移動・コマ替え・表示切り替えが効く。
3. 画像の送信と描画が動く。
4. スプライトを n 個動かしたときの 1 フレームの費用が測ってある
   (phase9 のゲームで何個まで置けるかの根拠になる)。
5. デモアプリ (flash/app/python/) が新しい API を一通り見せる形に更新されている。
