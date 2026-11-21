# P1 指示書: 描画側と線の形 (透明・拡大と縮小・画素からスプライトの絵)

> 2026-11-21 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/pictures/plan.md (全部。**7 章が決定**)、
> **report/p0.md (全部。経路・直す場所・命令の案・sig の案の正)**、fmruby-core/CLAUDE.md と隣の
> `../fmruby-graphics-audio/CLAUDE.md`、直下の CLAUDE.md の「仕組みと注意」(線の形のヘッダは `components/fmrb_wire/include` の
> 1 か所、`rake wire:check`)、doc/regression/usage.md、gfx_boundary_issues (描画境界の課題と制約: doc を grep。提案の前に
> C1〜C6 を読む)、doc/reference/internal_ram_budget.md。

## 1. 作るもの (p0.md の案のとおり。名前は plan.md 7 章)

1. **透明**: 2 つの描画側 (graphics-audio と、fmruby-core の `display_local_task.cpp` の系統) で、PNG をスプライトの絵に
   読むとき、透明な画素 (α < 128、tRNS の透明) を透明色 `0x01` (その絵の透明色の扱いは p0 の案) にする。決まりは
   `fmrb_png332.h` の 1 つを両方が使う (写しを作らない。graphics-audio は fmruby-core のヘッダを直接読む今の仕組みで)。
   `create_image` の α は変えない (食い違いを gfx_boundary_issues の資料に書く)。
2. **拡大と縮小して描く**: 新しい命令 (p0 の `DRAW_TILE_SCALED`)。整数倍 1〜32 と、整数分の 1 (1/2〜1/32)。切り抜きつき。
   透明色を抜く。今の `draw_tile` の命令は変えない。境界の関数 `draw_tile_scaled_raw` と、`draw_tile` の `scale:`
   (縮小の書き方は p0 の案を元に決め、sig に書く)。
3. **画素からスプライトの絵を作る**: 新しい命令 (p0 の `SPRITE_IMAGE_DATA`)、境界 `write_sprite_image`、
   `SpriteImage#write_pixels`。RGB332 のバイト列。分けて送る形と、P4 系 (RGB565 の canvas) での色の変換。
4. **版**: 線の形の変更をここに全部まとめ、link と GA の版を 1 回だけ上げる (p0 の見立て: link 7・GA 3.2.0)。
   `rake wire:check` を通す。SDK (C・wasm のアプリの境界) に影響が無いことを確かめる。
5. sig (`sig/fmrb_gfx.rbs`・`fmrb_sprite.rbs`)、端末のヘルプ (`flash/help/`。あれば)、mruby のアプリと Spinel の基底の両方。

**入れない** (後の段): `BMP332.info`・`to_png` (P2)、置き場の管理 `draw_picture` (P3)、アプリを移す (P4)。
今のアプリ (チャット・ビューワー・スプライトエディタ・App Store) は変えない。今までどおり動くこと。

## 2. 受け入れ条件

1. sim の Modern・ブラウザ版・ネイティブ版 (sdl) で、試しのアプリ (スクラッチに置く。リポジトリに入れない) が:
   α つきの PNG を `SpriteImage#load_bmp` で直に読んで描くと透明が抜ける / 16x16 の絵を 2 倍・3 倍・8 倍、64x64 の絵を
   1/2・1/4 で描ける (透明が抜け、切り抜きが効く) / 手元の画素 (RGB332 の文字列) から作ったスプライトの絵が描ける。
   **graphics-audio の経路 (sim) と display_local の経路 (ブラウザ版・ネイティブ版) で同じ絵に見える** (画面の写しを
   比べる。色の深さの違いで出る差は書く)。Retro の sim でも通る。
2. 今ある絵とアプリの見た目が変わらない: `rake regress` (描画の記録)。変わる基準があれば、変化を report に書いて
   別コミットにする (親が検収する)。`rake regress:screens`。ランチャーのアイコン・App Store・チャットのスタンプ・
   ビューワー・スプライトエディタを sim で開いて崩れていない。
3. 版: core と graphics-audio の版がそろい、sim が起動する。片方だけ古いと、今までどおり分かる形で止まる (その文を report に)。
4. 内蔵 RAM・ファームの大きさ: `rake regress:size` (S3・P4) の増分を report に。**内蔵 RAM を増やさない**
   (増えるなら止まって返す)。描画側の時間 (拡大 32 倍、48x48 を 8 倍など) を sim で測って書く。
5. ESP32 のビルドが通る (S3 = NARYAv3 と P4 = NARYAv4。**焼かない**)。graphics-audio の ESP32 (WROVER) のビルドも通る。
   **Retro の焼き直しが要る** (S3 と WROVER) ことと、その手順を report に書く (焼くのはユーザと親)。
6. `git status` に取りこぼしなし (両方のリポジトリ)。起こしたものは止めてある。

## 3. 範囲と止まる条件

- 触ってよい所: fmruby-core の描画まわり (`display_local` の系統、`components/fmrb_wire/include`、境界の C、
  `lib/gems/picoruby-fmrb-app/` と `picoruby-fmrb-bmp332/include/` のヘッダ、Spinel の基底の対応する所、sig、ヘルプ、
  試験)、**`../fmruby-graphics-audio`** (絵の読み込み・スプライト・新しい命令・版。そのリポジトリの中でブランチ
  `pictures` を切ってコミット)、doc/pictures/report/p1.md、gfx の資料の追記、`rake docs:index` の結果。
- **触らない**: アプリ (flash/app・default_app の動き)、カーネルのほかの所、回帰の道具 (基準の更新は上の決まり)、sdkconfig、
  submodule の中 (LovyanGFX などは patch の仕組みで。直接編集しない)、asterism-chat・tutor・console。
  **端末 (GPD・P4-Nano・Retro) に焼かない・触らない。VPS に触らない。**
- fmruby-core はブランチ `asterism-chat` の上にコミット。両方とも push しない・develop へマージしない・`git add -A` を使わない。
  コミットは英語で `<type>(<place>): <summary>`、Co-Authored-By のトレーラ。
- `lib/` を編集したらビルド前に `rake clean`。`build/` (ESP32) には P4-Nano 用のビルドが入っているが、5 の確認で
  作り直してよい (焼かない)。`.env` は NARYAv3 のまま (環境変数でターゲットを渡す)。
- 止まる条件: 内蔵 RAM が増える。submodule の中の直しが patch の仕組みで表せない。SDK の境界 (C・wasm のアプリ) が
  壊れる。p0 の案と違う形にしないと作れない所で、**利用者 (アプリを書く人) から見た形が変わるもの**。ユーザの作業が要る。
  選ばずに、選択肢にして返す。
- 呼び名: 「端末」「Window」「描画側」。この mruby には `NilClass#to_i`・Regexp・`Array#pack` が無い。
