# P2・P3 指示書: 絵の gem の `info`・`to_png` と、FmrbGfx の絵の置き場の管理

> 2026-11-21 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/pictures/plan.md (**7 章が決定**)、
> report/p0.md (5・6・7 章の案。**sig の案の正**)・**report/p1.md** (入ったもの: `draw_tile(..., scale:)`・
> `draw_tile_scaled_raw`・`SpriteImage#write_pixels`・描画側の透明。p0 から変えた所)、`lib/gems/picoruby-fmrb-bmp332/`、
> `lib/gems/picoruby-fmrb-app/` (FmrbGfx の mrblib と C、Spinel の基底の側)、`sig/`・sig/README.md、4 つのアプリの絵の処理
> (移すのは P4。ここでは読むだけ: `flash/app/modern/chat/stamps.rb`、`flash/app/tool/image_viewer.app.rb`、
> `flash/app/tool/sprite_editor.app.rb`、`main/ruby/default_app/appstore.app.rb`)、doc/reference/internal_ram_budget.md、
> アプリの書き方の skill (直下の `.claude/skills/fmrb-app-new/SKILL.md`)。

P1 の後の仮決め (親。推奨のまま): 透明な点に書く値はいつも `0x01`。境界の `picture_info` はここ (P2) で `BMP332.info` と
同じ C を使って足す。`DRAW_TILE` の描く先の違いは記録だけ。`draw_tile` の包みに `clip:` は足さない。

## P2: 絵の gem (`BMP332`)

- `BMP332.info(path)` / `BMP332.info(data: bytes)`: 読み込まずに `{width:, height:, format:, ...}` (PNG・BMP。形は p0 の案)。
  読めないものは nil か決めた誤りの形。境界の `picture_info(path)` (FmrbGfx。全言語の SDK から使える形) も同じ C で。
- `BMP332.to_png(path または data:, max_width:, max_height:, max_bytes:)`: 読み直して小さいパレットの PNG のバイト列を返す
  (透明は `0x01`)。**C の中で済ませ、画素の文字列を VM に持たない**。断り方は Hash (`{error: :too_big, ...}`。p0 Q5b の (a))。
- `module BMP332` の sig を新しく書く (今は sig に無い: `load`・`parse`・`save`・`encode_png`・`info`・`to_png`)。
- ホストの試験 (`rake png332:test` の系統) に足す。今の `parse` → `encode_png` と同じバイト列になること (チャットの sha256 が
  変わらないように。変わるなら止まって返す)。

## P3: FmrbGfx の絵の置き場の管理 (パスを渡すだけの API)

決定 (plan.md 7 章 Q6・Q7・Q8・Q12):

```ruby
@gfx.draw_picture("/home/stamps/a.png", 10, 20, scale: 2)   # 描く。置き場に無ければ読む
w, h = @gfx.picture_size("/home/stamps/a.png")              # 大きさ (読まずに info で)
@gfx.put_picture("mine", w, h, pixels)                      # 手元の画素を名前で置く
@gfx.draw_picture("mine", 10, 20)
@gfx.release_pictures                                        # ぜんぶ離す (閉じるときは自動)
```

- `scale:` は 1〜32 と、縮小 (-2〜-32 か、p1 の書き方に合わせた形)。`checker: true` で透明の下に市松、`clip:` で切り抜き
  (p0 の案。名前と引数は sig の案を元に決めて report に)。
- 置き場: スプライトの絵に寄せる (`create_image` は使わない)。描き手 (アプリの gfx) ごとに既定 8 枚、`picture_limit=` で
  変えられる。超えたら最近使っていないものから離す。機械全体の枚数が一杯で作れないときは、自分の古いものを離して作り直し、
  だめなら描かずに false を返す (落ちない)。同じパスのファイルが書き換わったとき (大きさか mtime) は読み直す。
- 大きい絵: 置き場に置ける大きさの上限を決める (描画側の置き場: sim・Retro は機械全体で 256 KB、P4 系は 1 枚ずつ PSRAM)。
  上限を超える絵は false を返し、理由を取れる形に。**縮小して出したい大きい絵 (ビューワー) をどう置くか** (読むときに間引いて
  置く命令が要るのか、置いてから縮小で描けば足りるのか) を、p0・p1 を読んで決める。描画側の命令を足す必要が出たら
  **止まって返す** (線の形は P1 で 1 回だけ、と決めたため)。
- Ruby (mruby と Spinel の基底で同じ中身) で書く。**絵を使わないアプリの VM プール・内蔵 RAM が増えない** (状態は最初に使ったとき
  だけ確保。メソッドの登録の分は測って報告する)。アプリが終わるとき・Window を閉じるときに自動で離す
  (今ある後片付けの口に載せる。基底の変更は最小に)。
- `draw_checker(x, y, w, h)` (透明の下の市松。ビューワーとスプライトエディタが別々に持っているもの) も p0 の案どおり置き場の側に。
- sig、端末のヘルプ (`flash/help/`)、アプリの書き方の skill (`.claude/skills/fmrb-app-new/SKILL.md` に「絵を出す」の節を
  短く。**直下のリポジトリのファイルなので、直さずに、足す文の案を report に書く**)。

**入れない** (P4): 今のアプリを移すこと。今のアプリは変えない。

## 受け入れ条件

1. `rake png332:test` (とホストの試験一式 `rake test`) が通る。`to_png` の結果が、今の `parse` → `encode_png` と同じバイト列
   (flash/ の PNG と、α・tRNS の試験の絵で)。
2. 試しのアプリ (スクラッチに置く) で、sim の Modern と Retro・ブラウザ版・ネイティブ版: `draw_picture` で 16x16 を 2 倍、
   48x48 を等倍、64x64 を 1/2、透明つきの PNG を `checker: true` で出せる。`put_picture` の絵が出る。12 枚の違う絵を順に
   出しても落ちず、置き場は上限の数を超えない (離した数をログか戻り値で確かめる)。同じ絵の 2 回目は読み直さない。
   ファイルを書き換えると次に描くとき新しい絵。アプリを閉じると全部離れる (描画側の枚数が元に戻る)。
3. **絵を使わないアプリの VM プールが増えない**: `rake regress:run` を流し、基準からの差を report に (デスクトップの +2 KB は
   前の段で受け入れ済み。それ以外に増えたものがあれば、どのメソッドの分かを書く。KB 単位で増えるなら止まって返す)。
   `to_png` の VM プールの使用量を、今の `parse` → `encode_png` と比べて report に。
4. 内蔵 RAM・大きさ: `rake regress:size` (S3・P4) の増分。内蔵 RAM を増やさない。
5. `rake regress` が通る。最後に 1 回 `rake regress:screens`。今のアプリ (チャットのスタンプ・ビューワー・スプライトエディタ・
   App Store) を sim で開いて崩れていない。
6. `git status` に取りこぼしなし。起こしたものは止めてある。

## 範囲と止まる条件

- 触ってよい所 (fmruby-core): `lib/gems/picoruby-fmrb-bmp332/`、`lib/gems/picoruby-fmrb-app/` (FmrbGfx の mrblib と境界の
  `picture_info`)、Spinel の基底の対応する所、sig、ヘルプ、SDK のヘッダと boundary.json (足すだけ)、試験、
  doc/pictures/report/p2.md・p3.md、`rake docs:index` の結果。
- **触らない**: アプリ (flash/app・default_app)、描画側と線の形 (`components/fmrb_wire/include`、display_local、
  graphics-audio。**版を上げない**)、回帰の道具の基準 (変わるなら変化を report に書いて別コミット)、sdkconfig、submodule の中、
  直下のリポジトリ、asterism-chat・tutor・console。**端末に焼かない。VPS に触らない。**
- ブランチ `asterism-chat` の上にコミット (P2 と P3 は別のコミット)。push しない・develop へマージしない・`git add -A` を使わない。
  `lib/` を編集したらビルド前に `rake clean`。`.env` は NARYAv3 のまま (環境変数でターゲットを渡す)。
- 止まる条件: 描画側の命令・線の形を変える必要がある。内蔵 RAM が増える。絵を使わないアプリの VM プールが KB 単位で増える。
  `to_png` のバイト列が今と変わる。利用者 (アプリを書く人) から見た形を、plan 7 章・p0 の案と違う形にする必要が出たとき。
  選ばずに、選択肢にして返す。
- 呼び名: 「端末」「Window」「描画側」。この mruby には `NilClass#to_i`・Regexp・`Array#pack` が無い。ブロックは重い。
