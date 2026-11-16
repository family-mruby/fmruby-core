# F2b 報告: スプライトエディタで PNG を読み書きする

> 開始 2026-11-16 13:40 (JST) / 終了 2026-11-16 15:50 | 親: ../plan.md の F2b (2026-11-16 ユーザの決定) | 前段: f2.md の 5 章 決めること 1

## 0. 要約

- 端末のスプライトエディタ (`flash/app/tool/sprite_editor.app.rb`) が **PNG を開いて保存できる**ようにした。開いた形式のまま
  保存する (`.png` はパレットの PNG、それ以外は今までの 8 bit BMP)。
- 読み書きは **app 側 (core) の C で、どの端末でも同じコード**: `picoruby-fmrb-bmp332` に `ports/esp32/png332.c` を足し、
  `BMP332.parse` が先頭の 8 バイトで PNG と BMP を見分け、`BMP332.encode_png` が F2 と同じ形のパレットの PNG を作る。inflate・
  deflate とも自前 (約 10 KB のコード)。**静的・大域の変数は 0** (`.data`・`.bss` とも 0 B)、作業の領域はアプリの VM プールから
  取って返すので、内蔵 RAM の静的な増分は 0。
- PicoRabbit の 8 枚を `tools/rgb332_png.rb` でパレットの PNG にした (1,334 B × 8 → 153〜175 B、どれも littlefs の inline に入る)。
  renderer のファイル名と `tools/gen_picorabbit_sprites.rb` (PNG を書く) を直した。
- 確認: 新しいホストの試験 `rake png332:test` (`rake test` に入れた)、`rake regress` (描画の記録は PicoRabbit の 4 つがファイル名だけ
  変わり、基準を更新)、sim (Retro) でエディタの PNG・BMP の開く→1 画素を直す→保存→開き直し、PicoRabbit の絵が画素で同じ、
  `regress:run`・`regress:screens`・`regress:size` (2 章)。

## 1. 調べた結果と選んだ作り

### 1.1 BMP を読み書きする端末の道具

| 所 | 読み書き | この段で |
|---|---|---|
| スプライトエディタ `sprite_editor.app.rb` (mruby のアプリ) | `BMP332.load` (File で全体を読み C の `BMP332.parse`) → `@sheet_pixels`、保存は `BMP332.save` (Ruby で BMP を組む) | PNG も開き、開いた形式で保存 |
| PicoRabbit の絵 (`picorabbit_renderer.rb`) | `sync_file` → `SpriteImage#load_bmp` (表示側が読む。PNG は F2 で済) | 名前を `.png` に |
| PicoRabbit の Export | 表示側の画面の書き出し (実機は JPEG、sim は BMP)。スプライトではない | 変えない |
| shooter・rpg_demo などの絵 | F2 で PNG。エディタで開けるようになった (道具で作り直す絵なのは同じ) | — |
| ホストの道具 `tools/web/js/sprite-editor.js`・`bmp332.js`、family-mruby 直下の `tools/fm_asset_editor` | BMP を書く | 端末の外なので対象外。読む側は両方受けるので、出来た BMP はそのまま使える |

- `BMP332` を使うのはエディタだけ (Spinel 側の `main/ruby/spinel` には無い)。エディタは mruby の VM で動く。

### 1.2 PNG を読む所をどこに置くか

| 案 | 内容 | 判断 |
|---|---|---|
| (A) core の C に小さな読み書き (採用) | パレット (RGB332) の PNG を主に、ふつうの 8 bit の PNG も読む。gem の C 関数として Ruby に出す | どの端末 (Retro・P4・sim・ネイティブ・ブラウザ) でも同じコードで、表示側とのやり取りが要らない。エディタは画素の値 (RGB332 のバイト) が手元に要るので、読んだ結果を app 側に持ってこられることが前提 |
| (B) 表示側で復号して画素を返してもらう | P4・ネイティブ・ブラウザは pngle がある | Retro は表示が WROVER で、画素を送り返す線の形が無い (新しいコマンドと UART の往復)。端末で道が分かれる |
| (C) LovyanGFX の miniz (`lgfx_miniz.c`) を core から呼ぶ | S3・P4 では m5gfx の部品が組まれている | 内部の記号に依存する。deflate の `tdefl_compressor` は 300 KB 前後の作業領域で VM プールに重い。sim の core (IDF の linux) で組まれているかも別に確かめが要る |
| ROM の miniz (S3) | inflate だけ | P4・ホストで使えず、書く側が無い |

(A) にした。復号は PNG の全体を一度に持つので、inflate は 32 KB の辞書を持たず出力の領域を辞書に使う (pngle の 44 KB に対し
`inflate_t` 1.6 KB + IDAT の写し + 生の行 + 画素)。

### 1.3 読み書きの中身 (`include/fmrb_png332.h`、`ports/esp32/png332.c`)

- **読む**: パレット 1/2/4/8 bit、グレー 1/2/4/8 bit、RGB・RGBA・グレー+α の 8 bit、5 種のフィルタ全部。インタレースと 16 bit は
  分かる言葉で断る。色は上位ビットを残して RGB332 (`(r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)`、表示側の LovyanGFX・
  `rgb332_png.rb` と同じ)。透明 (tRNS・α) は見ない (スプライトは色で抜く。0x00・0xE3 など)。zlib の頭と Adler-32 を確かめる。
  幅・高さは 2048 まで (`FMRB_BMP332_MAX_DIM` と同じ)。
- **書く**: `tools/rgb332_png.rb` の `encode` と同じ形 — 使う色だけのパレット (値の昇順、RGB888 への広げ方も同じ)、16 K 画素未満は
  1/2/4 bit、以上か 17 色以上は 8 bit、フィルタなし。IHDR・PLTE はバイト単位で Ruby の道具と同じ (試験で確かめる)。
  圧縮は LZ77 (ハッシュの鎖、1 歩の遅延一致、窓は最大 32 KB だがデータより大きくしない) と、ブロックごとに固定の符号と
  そのブロックの Huffman 符号の短い方。圧縮が生より大きいときは stored のブロックにする。
- **大きさ** (Ruby の zlib 最良と比べる。`flash/` の PNG 107 枚 (PicoRabbit の 8 枚を含む) を C で書き直した):

| 絵 | zlib 最良 (今のファイルの作り) | C の書き出し |
|---|---|---|
| 16 K 画素未満 (スプライト・アイコン・タイル、72 枚) | 11,358 B | **11,313 B** |
| 全 107 枚 | 114,374 B | 140,115 B |

  スプライトの大きさでは zlib の最良と同じ。差が出るのは大きな絵 (1280x720 の壁紙が 2 倍など、鎖の長さと窓の差) で、エディタの
  画面 (280x180 の Window に 16 px のタイルを並べる) で直す絵ではない。PicoRabbit の 8 枚は C で書き直しても同じ大きさ帯
  (エディタで保存した `usagi_sleep` が 172 B、道具の 172 B と同じ)。
- **作業の領域** (アプリの VM プール、`mrb_malloc_simple`): 読むとき IDAT の写し + 生の行 ((幅のバイト + 1) × 高さ) + 画素 (幅 × 高さ)
  + 1.6 KB。書くとき 生の行 + 出力 (stored の大きさまで) + 約 9 KB の表 + ハッシュ (256〜8,192 個 × 4 B) + 窓 (データの大きさまで、
  最大 32 K × 2 B) + 字句 (最大 4,096 × 4 B)。16x16 のスプライトで約 10 KB、どれも処理の後に返す。スタックは数百 B。
- **コードの大きさ**: ホストの `-Os` で text 9,964 B、data 0・bss 0。ESP32 での増分は 2 章の regress:size。

### 1.4 Ruby の側

- `BMP332.parse(data)`: PNG なら `parse_png` (C)、そうでなければ今までの BMP。返す Hash は同じ (`width`・`height`・`pixels`)。
- `BMP332.save(path, w, h, pixels)`: 末尾が `.png` (大文字も) なら `encode_png` で書き、それ以外は `save_bmp` (今までの BMP の書き出し、
  名前だけ変えた)。
- `BMP332.encode_png(w, h, pixels)`: C。
- エディタ: 待ちの文を `Select a PNG or BMP file...` に、開けないファイル (テキストなど) は `Log.error` に理由を出してファイルの
  選択に戻る (前は例外でアプリが落ちていた)。保存の失敗も `Log.error`。

## 2. 確認

| 道具・場面 | 結果 |
|---|---|
| `rake png332:test` (新設、`rake test` に入れた) | `flash/` の PNG 107 枚 (パレット 1/2/4/8 bit 103 枚、RGB 1、RGBA 3) を C で読み、`rgb332_png.rb` と画素が全部同じ。C で書き直したものを C と Ruby の両方で読み戻して同じ、IHDR・PLTE は Ruby の `encode` とバイトで同じ。合成の場面: 160x120 の雑音 (stored に落ちる)、市松 (圧縮)、1x1、端数の幅 (13・7・3 px で 1/2/4 bit)、壊れた入力・BMP を PNG と取り違えない、解放の漏れ 0。ASan・UBSan 付きでも通した |
| `rake regress` | host 31 組通過。描画の記録は PicoRabbit の `present` 4 つ (retro・modern・mini 2 つ) が落ち、差は `load_bmp` のパスの `.bmp` → `.png` だけ (消えた行と足した行が拡張子を除いて対になることを確かめた)。基準を更新 |
| sim (Retro) エディタ・PNG | `/home` に置いた `usagi_sleep.png` の写しを開く → 左上の 1 画素を赤 (0xE0) に → S で保存 → 開き直すと赤が出る。保存したファイルは元と比べて **(0,0) の 0x00 → 0xE0 だけが違い**、4 bit・8 色のパレットの PNG、172 B (Ruby の zlib 最良と同じ大きさ)、Pillow でも開ける |
| sim (Retro) エディタ・BMP | 前の `usagi_run1.bmp` の写しで同じ操作 → BMP (`BM`、1,334 B) のまま保存され、違いは (0,0) だけ |
| sim (Retro) エディタ・絵でないファイル | `mahjong.toml` を選ぶと `SpriteEd: cannot open /home/mahjong.toml: Failed to parse BMP332` を出してファイルの選択に戻る |
| sim (Retro) PicoRabbit | demo.md の開始で 8 枚が `LOAD_SPRITE_BMP: loaded flash/cache/app/picorabbit/*.png (PNG)`、`8 sprite frames loaded`。2 ページ目の画面で、うさぎ (`usagi_run1`) の 16x16 の不透明な画素が PNG の画素と全部一致 (位置 0,224) |
| `rake regress:run` | 1 回目は Retro の FM-Editor (Spinel のエディタ、BMP332 を使わない) が +592 B で落ち、流し直しで通過 (揺れ)。mruby のアプリの VM は -32〜+72 B (gem の mrblib の増分は揺れの中) |
| `rake regress:screens` | **33/33 通過、基準の変化なし** (retro 9・modern 11・web 6・native 3・native-keys 4。ブラウザ版・ネイティブ版も png332.c を組んで通った) |
| `rake regress:size` | 通過。DIRAM は NARYAv3 125,395 B・TAB5 129,394 B で **F2 の後と同じ値 (内蔵 RAM の静的な増分 0)**。`libpicoruby-esp32.a` の flash は基準 (4e7e7e61) から +15,305 B (この段の C と、基準より後の他の段の分) |

## 3. PicoRabbit の絵

| 絵 | BMP → PNG | 色 |
|---|---|---|
| kame_walk1 / walk2 / banzai | 1,334 B → 155 / 156 / 166 B | 5 |
| usagi_run1 / run2 / jump / sleep / hurry | 1,334 B → 160 / 153 / 163 / 168 / 175 B | 6 / 5 / 6 / 7 / 7 |

- 計 10,672 B → 1,296 B。8 区画 → 0 区画 (inline)。`rgb332_png.rb` で読み直して画素が BMP と同じことを確かめた。
- `tools/gen_picorabbit_sprites.rb` は PNG を書くようにした。下書きから書き直すと 5 枚は今のファイルとバイトで同じ、`usagi_run1`・
  `run2`・`sleep` は違う (エディタで手直しされた絵で、道具の注釈どおりファイルの方が正。道具は既にあるファイルを上書きしない)。
- 作業ツリーの `flash/cache/app/picorabbit/` (gitignore) には前の `.bmp` の写しが残る。害は無い (P4 の像には作業ツリーの `cache/` が
  入るので、気になれば消してよい)。

## 4. 実機 (親が確かめること)

`build/` に NARYAv4 の ESP32 のビルドを残した (5 章)。焼いて (/home は keep_home):

1. PicoRabbit でデッキを開始し、うさぎと亀が今までどおり出る (ログ `PicoRabbit: 8 sprite frames loaded`、表示側の
   `LOAD_SPRITE_BMP: loaded .../cache/app/picorabbit/*.png`)。
2. スプライトエディタ: `/usr/share/picorabbit/usagi_sleep.png` (または /home に写したもの) を開く → 1 画素を直して S → ログ
   `SpriteEd: saved ...` → 開き直して直した画素が出る。保存したファイルを `tab5_fs` で取り出せば `ruby -r./tools/rgb332_png` で
   読める。BMP のファイルも同じ操作で BMP のまま保存される。
3. 内蔵 RAM: 10 秒周期の `IRAM free:` が前のファームと同じ (静的な増分は regress:size で 0)。

## 5. ESP32 のビルド

`rake clean_all` → `FMRB_HW_TARGET=NARYAv4 rake build:esp32` (set-target・littlefs の venv は rake に任せて通った)。**本体の checkout の
`build/` に残した** (焼くのは親)。`.env` は触っていない。コミットは c9602115 (この report の前)。

| 項目 | 値 |
|---|---|
| `file build/fmruby-core.elf` | ELF 32-bit RISC-V |
| DIRAM (内蔵 RAM の静的な使用) | **124,876 B** (F2 と同じ。増分 0) |
| HP core RAM・LP RAM | 2,996 B・56 B (同じ) |
| `fmruby-core.bin` | 6,584,528 B (0x6478d0)。app の区画 0x7f0000 の 21% が空き |
| storage の像の PicoRabbit | `usr/share/picorabbit/*.png` 8 枚 (BMP は無い) |
| 焼き方 | 区画の表は F4 から変わっていないので app_sync (ファームと storage) でよい。font_ja の区画は同じ |

## 6. 決めること (ユーザ)

- なし。気づいた点: エディタは開いた形式で保存するので、BMP を PNG にしたいときは今は道具 (`rgb332_png.rb`) で変える。エディタに
  「PNG で保存し直す」を足すかは必要になったら。
