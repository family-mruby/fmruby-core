# 報告: FmrbGfx の長文ヘルプ

> 状態: 完了 | 更新: 2026-09-01 | FmrbGfx の F1 ヘルプを 5 -> 32 に。
> 書きながら見つけた sig と実装の食い違い 3 件

指示書は doc/editor_ti/instruction_help.md。書いたのは
`sig/fmrb_gfx.rbs` の doc コメント 2 行目以降で、シグネチャと 1 行要約は
触っていない。`flash/help` は生成物 (gitignore) なので、追跡される変更は
sig の 1 ファイルだけ。

## sig と実装が食い違っていた 3 件 (直していない)

指示書のとおりシグネチャは直さず、ここに残す。

1. **`clear_viewport` は実装が無い**。`sig/fmrb_gfx.rbs` は
   `def clear_viewport: () -> void` を宣言しているが、mrblib にも C にも
   実体が無い。06d8d1f4 (gfx: give each canvas a sprite clip rect) で
   Ruby の wrapper が消え、gfx.c にはコメントの中に名前が残っているだけ。
   **エディタは今これを補完候補に出す**ので、書いた人は NoMethodError を
   受け取る。sig から消すか、実装を戻すかの判断が要る。
2. **`blend_rect` の第 6 引数名が `alpha`**。実体は混ぜ方
   (`FmrbGfx::BLEND_ADD` / `BLEND_XOR`) で、透明度ではない
   (`mrb_gfx_blend_rect` は `mode` として読む)。名前だけの問題だが、
   ホバーに出る文字列なので誤解を招く。長文の方には「透明度ではない」と
   明記した。
3. **キーワード引数が本質のものは、sig では引数ごと落ちている**。
   `draw_image` の `x:` `y:`、`draw_tile` の `dst_x:` `dst_y:`、
   `draw_image_masked` の `x:` `y:`、`sync_file` の `dest:`。
   sig/README.md の「必須引数だけの形で書く」に従った結果で、規約どおりでは
   ある。ただし**シグネチャだけ見て呼ぶと動かない** (置き場所が全部 0 になる)
   ので、長文の方に実際の呼び方を書いた。この 4 つは長文が無いと使えない。

## つまずきの見つけ方 (次に書く人へ)

「実装を読んで、コメントが弁解しているところを探す」のが速かった。
mrblib のコメントは、なぜそう書いたかが書いてあるところほど、利用者が
つまずく場所と一致している。実例:

- `sync_file` のコメントに「`file_status[:exists]` で判定すると、書き換えた
  素材が古いまま残る」とある。これは踏んだ人がいるから書かれている。
- `create_image` は PNG を生のまま `drawPng` に渡す実装で、BMP を渡しても
  例外にならない (画面と同じ大きさの空の画像ができる)。**失敗が静か**な
  ものは全部つまずきどころ。
- `draw_image` の `scale_y` の既定値が `0.0` で、意味は「`scale_x` と同じ」。
  既定値が値として不自然なものは、たいてい説明が要る。

逆に、`draw_circle` のように「引数のとおりに描く」だけのものには何も書いて
いない。指示書の「書くことが無いメソッドには書かない」に従った。

## 書いた範囲

52 メソッド中 27 に足して、既存と合わせて 32。足したのは次の系統。

- 出ない・消える: `present` (描画は present まで出ない、スプライトの合成も
  ここ)、`clear` (窓のアプリでは枠まで消える。`clear_user_area` を使う)
- 座標と大きさ: `canvas_width` (窓のアプリが描いてよいのは `@user_area_*`)
- 測る: `text_width` `font_height` `set_text_size` (いまの字で測る)
- 画像: `create_image` `load_image` `draw_image` `delete_image` `draw_tile`
  (PNG と BMP の入口の違い、先に `sync_file`、位置はキーワード)
- 送る: `sync_file` `transfer_file` `file_status` (描画側は自分の
  ファイルシステムしか読めない)
- マスク: `create_mask` `draw_image_masked` (1bpp、上位ビットが左)
- 機種差: `set_viewport` (Modern のみ)、`set_output_level`
  `set_chroma_level` (Retro のみ)
- 色: `rgb_to_332` (3-3-2 に落ちる。**0x01 は避ける**)、`hsv_to_rgb`
  (戻りは配列で、そのままでは色にならない)
- その他: `get_pixel` (同期の往復)、`blend_rect` (混ぜ方)、`draw_arc`
  `fill_arc` (角度は度、r0 が内側)、`draw_thick_line` (1 画素の線の重ね)、
  `set_composite_regions` (ふつうは .app.toml が設定する)

## ヘルプ画面の幅は約 39 桁

F1 のヘルプは文章を折り返して出す。散文はどこで折れても読めるが、**コード
ブロックは語の途中で折れる**。`/usr/share/sprites/flappy/bird_body.bmp` の
ような長いパスを例に書くと、画面では `.../flapp` `y/bird_body.bmp` と割れて
読みにくい。

実測で 1 行およそ 39 桁 (半角)。**例のコードは 40 桁以内**に収める。長い
パスは短いもの (`/home/pic.png` など) に置き換えるか、行を分ける。既存の
`set_sprite_clip` の例も 55-62 桁あって折り返している (今回は触っていない)。

## 確かめたこと


- `rake ti:help` -> `flash/help` 全体で 57 ファイル (30 から)。
  `flash/help/FmrbGfx` は 5 -> 32。
- `rake ti:test` 通過 (test_builtin / test_eval / test_suggest /
  test_call_context)。
- `<<en>>` の切り分けは生成物で確認済み。長文は「`<<en>>` だけの行」で
  区切る形になっている。
- **ブラウザ版で実際に開いた**。`rake wasm:web` -> `web_reload` ->
  エディタで次のように書いて Tab / F1。日本語側だけが出て、英語側は出ない。

  ```ruby
  class A < FmrbApp
    def on_create
      g = create_canvas_gfx(100, 100)
      g.present
  ```

  受け手の型が決まる書き方が要る。`@gfx` は効かない (picoruby-ti は ivar を
  追わない) が、`create_canvas_gfx` の戻り値を局所変数で受ければ効く。

## 残り

指示書の順番でいうと次は FmrbApp (73 中 2)。ライフサイクルと窓まわり。
FmrbAudio / スプライト / タイルはその次。
