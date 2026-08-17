# Phase 6 実装レポート (絵)

## 完了条件の判定

| 条件 | 判定 | 確認方法 |
|---|---|---|
| 1. 日本語が描け、text_width が実際の描画幅と一致する | OK | 混在描画の下に測った幅の線を引き、線の端と文字の端が揃うことを画面で確認 |
| 2. BMP のスプライトが表示され、移動・コマ替え・表示切替が効く | OK | Ruby の RPG デモの player_*.bmp を Python から読み、矢印で移動・SPACE でコマ替え |
| 3. 画像の送信と描画が動く | OK | 壁紙の PNG を create_image して 0.5 倍で描画 |
| 4. スプライト n 個の 1 フレームの費用が測ってある | OK | 8 個で 333 us/frame (下記) |
| 5. デモアプリが新しい API を一通り見せる | OK | flash/app/python/pygfx.app.py |

## 入れたもの

### C 側 (`_fmrb`)

- 文字: `gfx_set_font` / `gfx_set_text_size`、`gfx_draw_text` に混在描画の引数。
- 画像: `gfx_sync_file` / `gfx_create_image` / `gfx_draw_image` /
  `gfx_delete_image` / `gfx_draw_tile`。
- スプライト: `gfx_create_sprite_image` / `gfx_load_sprite_image_bmp` /
  `gfx_delete_sprite_image` / `gfx_set_sprite_target` /
  `gfx_create_sprite_instance` / `gfx_sprite_move` / `gfx_sprite_visible` /
  `gfx_sprite_frame` / `gfx_delete_sprite_instance` / `gfx_delete_all_sprites`。
- `language()` (system_conf.toml の言語)。

命令を組み立てる部分はすべて共通 C 層 (fmrb_gfx_cmd.h) のもので、
graphics-audio 側には一切手を入れていない。**計画どおり「繋ぐだけ」で済んだ**。

### Python 側 (共通層)

- `FmrbGfx`: `set_font` / `set_text_size` / `draw_text(mixed=)` /
  `text_width` (バイト走査) / `sync_file` / `create_image` / `draw_image` /
  `delete_image` / `draw_tile` / `delete_all_sprites`。
- `SpriteImage` (`set_target` / `reset_target` / `load_bmp` / `destroy`)、
  `SpriteInstance` (`move` / `set_visible` / `set_frame` / `destroy`)。
- `language()`。

### 検証用アプリ

`flash/app/python/pygfx.app.py` + toml。Ruby の RPG デモの素材をそのまま
借りている (同じファイルがどちらの言語からでも使える、という確認も兼ねる)。

## 分かったこと

### 1. 文字列はバイト列だった

`MICROPY_PY_BUILTINS_STR_UNICODE` は ROM レベル EXTRA 以上でしか入らず、
この構成 (CORE) では**入っていない**。つまり `len("日本語")` は 9 を返し、
添字もバイト単位になる。`text_width` のバイト走査はこれで素直に書ける。
`encode()` を通してから走査しているのは、将来 ROM レベルを上げたときに
壊れないようにするため。

画面では `bytes 29` (「こんにちは ABC 日本語」= 29 バイト) と出ており、
Ruby 側の `String#length` が文字数を返すのとは**逆**である点は覚えておく
(Ruby では bytesize が要るところで、Python ではそのまま len が使える)。

### 2. 画像とスプライト画像は別物 (詰まった点)

`create_image` は**画像ファイルを復号する経路**で、デスクトップの壁紙
(PNG) のためのものである。ここに 64x32 の BMP を渡したところ、
`id=3 320x240` が返り、描いても何も出なかった。エラーにはならない。

正しい使い分けはこう:

| 用意したいもの | 使うもの |
|---|---|
| スプライト・タイル素材 (BMP) | `SpriteImage` + `load_bmp` |
| 一枚絵 (PNG) | `create_image` + `draw_image` |

Python 固有の話ではなく Ruby でも同じだが、**間違えても黙って空の画像が
できる**ので、known_limitations に書いた。

### 3. スプライトは 1 フレームあたりほぼ無料

シミュレーションでの実測:

| 項目 | 値 |
|---|---|
| スプライト 8 個を動かして present 1 回 | **333 us/frame** |
| 内訳 (present 1 回 = 33 us) を引いた 1 個あたり | 約 37 us |

30 フレーム/秒 (33ms) の予算に対して **1% 程度**しか使わない。phase5 で
測った `fill_rect` 1 本 45us と比べると、**動くものを毎フレーム描き直すより
スプライトに任せるほうが桁で安い**。phase9 のブロック崩しを
「板・球・道具はスプライト、ブロックと背景は 1 回だけ描く」で設計する
根拠がこれで揃った。

### 4. タイルマップのクラスを作らなくても足りる

`draw_tile` を通しただけで、タイル並べは Python 側で `for` を回せば書ける
(デモの上端のタイル列がそれ)。地図データが要るなら phase5 の `read_file`
で読める。共通層に大きなクラスを持ち込まない方針は保てている。

## 退行の確認

- Ruby の RPG デモ (スプライト・タイル・BGM を全部使う) が従来どおり動く。
- ホストのテスト一式 (`rake test`) 通過。
- 既存の Python アプリ (python.app.py / pysub / pybench) も動作。

## 次への申し送り

- `create_image` の 10 秒待ちは PNG の復号を待つためのもので、その間
  アプリは止まる。ゲームの最中に呼ぶものではない (起動時に済ませる)。
- スプライト画像への描画 (`set_target` / `reset_target`) は対で使う。
  Python の `with` 文にはしていない。対を崩すと以後の描画が画像側に
  流れ込むので、phase9 で使うときは 1 か所にまとめる。
- 実機での描画命令の費用は未測定。phase5 の計測と合わせて、実機で
  一度に測る。
