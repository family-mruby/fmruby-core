# 実装指示書 P7: スライドに画像を載せる

対象: 実装担当セッション。作業リポジトリ: fmruby-core (renderer とサンプル。
GFX 側の変更は不要の見込み)。先に読むもの: plan.md の P7、
instruction_p0_p1.md の「進め方」、report/p6.md (フォント高さ基準の寸法)。
報告は report/p7.md。

## 確かめてある事実

- parser は `![alt](path)` を `Element.new(:image, path)` にしている
  (picorabbit_parser.rb 118 行付近)。renderer の `when :image` は
  `create_image` → `draw_image` → `delete_image` まで書いてあるが、
  **ファイルを描画側へ渡していない** (`sync_file` 無し) ので、Retro
  (WROVER) と sim (graphics-audio の別ストレージ) では見つからず、
  `[img: path]` の代替表示に落ちる。Tab5 は core と display が同じ VFS
  なので**たまたま**読める。
- 描画側が読めるのは **PNG** (`drawPng`。graphics_handler.cpp 1918 行付近で
  生データを保持し描画時に復号)。BMP は sprite (`load_bmp`) 用で、
  `create_image` の対象ではない (renderer にある `.bmp` → `.png` の書き換えは
  消す)。JPEG は無い。
- `draw_image(id, x:, y:, scale_x:, scale_y:)` に拡縮がある (1.0 = 等倍、
  `scale_y` 0.0 で等比)。`create_image` の戻り値に幅と高さがある。
- スプライト (flappy) と同じく `sync_file(src, dest: "/cache/app/picorabbit/...")`
  で描画側へ写す。同じ内容なら転送しない。Retro の UART は 921600bps
  (実効 ~90KB/s) なので、**PNG 1 枚 100KB なら 1 秒強**。

## 仕様

### 書き方

```
![alt](images/photo.png)            # デッキのあるディレクトリからの相対
![alt](/mnt/sd/slides/photo.png)    # 絶対パス
![w=200](images/photo.png)          # 幅を px で指定
![60%](images/photo.png)            # 本文幅に対する割合で指定
{:.center}                          # 中央寄せは従来の指示で (右寄せも)
```

- 相対パスは **.md のあるディレクトリ**基準 (`/home/slides/intro_ja.md` の
  `images/x.png` → `/home/slides/images/x.png`)。
- alt は表示しない。`w=NNN` か `NN%` の形なら大きさの指定として読む。
  それ以外の alt は無視。
- 形式は PNG のみ。それ以外の拡張子は `[img: path]` の代替表示。

### 大きさと置き方

- 指定が無ければ**本文幅と残りの高さ (`max_y - y`) に収まるように等比で縮小**。
  **拡大はしない** (等倍以下)。指定があればその幅 (高さは等比)。ただし
  残り高さに収まらなければそこまで縮める。
- 画像の下に `@line_h / 2` の余白。
- 横位置は `calc_align_x_px` (既存。`{:.center}` / `{:.right}`)。
- `draw_image` の `scale_x` に縮小率を渡す (`scale_y` は 0.0)。

### 転送と寿命

- 描画のたびに `sync_file(src, dest: "/cache/app/picorabbit/<デッキ名>_<ファイル名>")`
  → `create_image(dest)` → `draw_image` → `delete_image`。sync は同内容なら
  転送しないので、2 回目以降は速い。
- 読めなかったとき (ファイル無し / PNG でない / 復号失敗) は今の代替表示
  `[img: path]` のまま。例外で落とさない。
- Export (P4) は描いた絵をそのまま書くので追加作業なし。

### 性能の確認

- 1 枚の画像の「sync + create + draw」の時間を sim (Modern / Retro 向け) と
  Tab5 で実測し report に書く。**Retro 向け sim** では UART 転送の遅さは
  出ないので、Retro 実機の数字はユーザ確認に残す (100KB で 1 秒強の見込み
  を report に書いておく)。
- 画像の推奨上限を report と plan に書く: **426x240 以下、100KB 以下**
  (それ以上は Retro の転送とメモリで苦しい)。

## サンプル

- `flash/home/slides/images/` を作り、小さな PNG を 1 つ置く
  (例: デスクトップのスクリーンショットを 213x120 に縮めたもの、
  30KB 以下。`python3 tools/fmrb_screenshot.py` で撮って Pillow で縮める。
  生成は Python で可)。
- `intro_ja.md` の「何が作れるか」の枚に `![60%](images/desktop.png)` と
  `{:.center}` を足す。demo.md には `![alt](images/desktop.png)` を 1 か所
  (相対パス・既定の大きさの例)。

## 検収

### sim (Modern 向け 426x240、Retro 向け 320x240 の両方)

- intro_ja.md の画像の枚: 画像が本文幅の 60% で中央に出て、下の箇条書きが
  続く。pngscan で画像の左右端の x を読み、幅が `content_w * 0.6` に一致。
- demo.md の画像: 既定の大きさ (等倍か、収まらなければ縮小) で左寄せ。
- 存在しないパスを書いた一時 .md で `[img: ...]` が出て落ちない。
- 同じ枚を 2 回開いて、2 回目の sync が転送をしていない (ログ)。
- 放置中の present 1/s、E/Exception 0。

### Tab5

- 同じ 2 枚の実機写真、Export した JPEG に画像が入っている
  (`fmrb_rd_fs.rb pull` で回収)。sync + create + draw の時間。

### Retro

- 実機はユーザ確認 (WROVER を 8ecc108 以降で焼いた状態で intro_ja.md)。

## 受け入れ条件

- 上の検収が report/p7.md に揃う。
- コミット 2 本: (1) renderer、(2) サンプル (PNG + .md 2 本)。本書は (1) に。
  英語、ユーザ確認のうえ。
- plan.md の P7 に推奨上限を 1 行書く。
- `.env` が作業前の値に戻っている。

## やらないこと

- JPEG / BMP の表示、画像のキャッシュ保持 (描画ごとに復号でよい)、
  画像の隣に文字を回り込ませる配置、画像だけの枚の全面表示。
