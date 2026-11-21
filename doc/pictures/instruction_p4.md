# P4 指示書: 4 つのアプリを汎用の絵の部品に移す

> 2026-11-21 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/pictures/plan.md (7 章が決定)、report/p0.md の 8 章
> (移す先と削る所の一覧)、**report/p1.md・p2.md・p3.md (入った API の最終形)**、`sig/fmrb_picture.rbs`・`sig/fmrb_gfx.rbs`・
> `fmrb_sprite.rbs`、`lib/gems/picoruby-fmrb-app/mrblib/fmrb-picture.rb`、4 つのアプリ (`flash/app/modern/chat/stamps.rb` と
> `chat/`、`flash/app/tool/image_viewer.app.rb`、`flash/app/tool/sprite_editor.app.rb`、`main/ruby/default_app/appstore.app.rb`)、
> doc/asterism_chat/report の c2・c3・c3a・c3b (各アプリの受け入れ条件と確認のしかた)、doc/regression/usage.md。

P2・P3 の後の仮決め (親。推奨のまま): VM プールの +488 B/アプリは受け入れる (基準は P4 の後に親が更新)。書き換わりを見るのは
1 秒に 1 回。失敗の理由は `picture_error`。置き場の上限は下の 2 のとおり広げる。

## 1. 先にやること: 今の画面を撮っておく (移す前)

移す前の版で、sim の Modern とブラウザ版 (要るものは Retro の sim も) の決まった場面を撮り、スクラッチに残す。移した後に同じ場面を
撮って比べる (`tools/` の画像の比べる道具。P1 が使った `fmrb_pngdiff`)。場面の例:
- チャット: スタンプと文字の混ざった部屋 (c3 の確認と同じ作り方。ローカルのルータ + `../asterism-chat` 0.2.0 + 試験のパック。
  パックはスクラッチに作り直す)、スタンプの一覧 (パックのタブ・自分の絵のタブ)、絵が無いときの案内。
- 画像ビューワー: 小さい絵 (拡大)、Window より大きい絵 (縮小)、透明つきの絵 (市松)、開けない絵。
- スプライトエディタ: 新規 32x32 に数点塗った所、タイルのシートを開いた所、透明色の市松。
- App Store: 店の一覧 (絵つき)。手元の HTTP の口に一覧と絵を置く形 (`/home/appstore_base.txt`。c3 がブラウザ版で使った形)。

## 2. 置き場の側に足すもの (アプリに回り道を残さないため)

- **ファイルが大きくて置けない絵 (`:file_too_large`) でも、点の数が上限内なら出せるようにする**: 置き場の中で、VM の読み手
  (`BMP332.load`) で読んで画素から置く道に切り替える (今のビューワーが出せている 64 KB 超の PNG が、移した後も出ること)。
  アプリの側には書かない。メモリの山 (読んでいる間) を測って report に。
- P4 で 4 つのアプリを移していて「これが置き場に無いのでアプリに書くしかない」となったものは、置き場に足せるなら足す
  (利用者から見た API を増やす・変えるなら、**足す前に止まって選択肢にして返す**)。

## 3. 移すもの

1. **チャットのスタンプ** (`chat/stamps.rb`): 書き直しは `BMP332.to_png`、読む前の足切りは `BMP332.info`、出すのは
   `draw_picture(path, x, y, scale:, clip:)`、手元の絵の置き場の使い回しは置き場に任せる。**拡大した絵を一時ファイルに書くのを
   やめる** (貯めるのは受け取った元の PNG だけ。貯める場所とファイル名は今のまま、中身が元の大きさになる。古い形の貯めた絵が
   端末に残っていても崩れないこと)。**sha256 が今と変わらないこと** (同じ絵が同じ sha。p2 で確認済みのバイト列)。
   残るもの: sha256、`post`・`stamp_put`・`stamp_get` の流れ、一覧の画面、パックと自分の絵を探す所。
2. **画像ビューワー**: 絵を VM に読まずに `draw_picture` で出す (拡大・縮小・市松・切り抜き)。足切りは `picture_size`・
   `picture_error`。前・次の絵で前の絵を離す。
3. **スプライトエディタ**: 256 色の升目・市松の帯は今 `SpriteImage` を自前で作っている。置き場 (`put_picture`・`draw_checker`) と
   `write_pixels`・`draw_tile(scale:)` に替えて減らせる所を替える。編集中の絵そのもの (VM に画素を持つ) は今のまま。
   **なぞっている間の速さを落とさない** (c3a の 7〜11 ms。前後を測る)。
4. **App Store** (決定 Q8: 汎用の置き場に寄せる): 店の絵を `create_image` (機械全体 8 枚) から置き場に移す。`release_images` の
   自前の使い回しを消す。P4-Nano・Tab5 で店の絵が RGB332 の色になるのは受け入れ済み。Spinel で書かれているか mruby かを
   先に確かめる (Spinel なら、置き場を Spinel から呼ぶ最初の例になる: p3 の「確かめていないこと」)。
   移した後、`create_image`・`load_image` を使うアプリが残るかを数えて report に。

## 4. 受け入れ条件

1. 1 の場面で、移す前と後の画面が同じ (差の点数を report に。違う所は理由を書く。App Store の店の絵の色は、
   display_local の経路 (ブラウザ版) で変わる: その差は絵の中だけであること)。
2. 各アプリの元の受け入れ条件のうち、絵に関わるものをもう一度通す: チャット (c3 の 1〜5: 互いにスタンプが出る・2 回目は
   運ばない・自分の絵と α つき・断りの文・50 件の部屋)、ビューワー (c3b の 1・3)、スプライトエディタ (c3a の 1・3・4)、
   App Store (店の一覧と、1 つ入れる所)。
3. アプリの行数が減る (前後の `wc -l` を report に)。置き場の側に足した行数も。VM プール (各アプリを開いた状態) を前後で。
   チャットは c3 の 657 KB・682 KB と比べる。
4. `rake regress` が通る。appstore などの描画の記録の基準が変わるなら、変化を report に書いて**別コミット**にする
   (意図した変化: 命令が変わるため。親が検収する)。最後に 1 回 `rake regress:screens`。`rake regress:run` を流して、
   基準からの差を report に (基準の更新はしない。親がする)。
5. 内蔵 RAM を増やさない (`rake regress:size` の S3・P4。置き場に足した分)。
6. `git status` に取りこぼしなし (前からある 3 つを除く)。起こしたものは止めてある。配布イメージにスタンプの絵が無い
   (`git ls-files flash | grep -i stamp` に絵が無い) のは今のまま。

## 5. 範囲と止まる条件

- 触ってよい所 (fmruby-core): 4 つのアプリ、`lib/gems/picoruby-fmrb-app/mrblib/fmrb-picture.rb` (2 の足し) と sig・ヘルプ、
  端末の案内 (変わる所があれば)、回帰の基準 (4 の別コミットだけ)、doc/pictures/report/p4.md、plan.md の状態行、
  `rake docs:index` の結果。`../asterism-chat` は触らない (線の形は変えない)。
- **触らない**: 描画側と線の形 (版を上げない)、絵の gem の C (不具合の直しを除く)、カーネル・ほかのアプリ、sdkconfig、
  submodule の中、graphics-audio、直下のリポジトリ、asterism-tutor・console。**端末に焼かない。VPS に触らない
  (VPS でチャットホストが動いているが、確認はローカルのルータで行う)。**
- ブランチ `asterism-chat` の上にコミット (アプリごとに分ける)。push しない・develop へマージしない・`git add -A` を使わない。
  `lib/` を編集したらビルド前に `rake clean`。`.env` は NARYAv3 のまま。graphics-audio はブランチ `pictures` のまま。
- 止まる条件: 描画側の命令・線の形を変える必要がある。内蔵 RAM が増える。スタンプの sha256 が変わる。移した後に、今できている
  ことができなくなる (出せていた絵が出ない・遅くなる) ものがあり、置き場の足しで直せない。利用者から見た API を増やす・変える
  必要がある。選ばずに、選択肢にして返す。
- 呼び名: 「端末」「Window」「描画側」「チャットホスト」。この mruby には `NilClass#to_i`・Regexp・`Array#pack` が無い。
