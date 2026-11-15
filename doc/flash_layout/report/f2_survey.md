# F2 調査: 画像を小さくする (形式の比較と推奨)

> 調査 2026-11-16 | 親: ../plan.md の F2・未確定事項 2 | コードは変えていない。数は fmruby-core 10eaf9ad 時点の `flash/` (作業ツリー) から測った

## 0. 要約

- 同梱の画像は **すべて RGB332 の 8 bit BMP か PNG**。BMP は全部 RGB332 の値をそのまま画素に持つ (パレットは RGB332 の並び)。色数は 2〜19 色で、
  パレット PNG にすると **BMP は 1/10〜1/60** になる (麻雀の手牌 95,158 B → 3,899 B)。
- 表示側の 2 つ (P4 などの display_local と、Retro・sim の graphics-audio) は **既に PNG の復号器 (LovyanGFX の pngle) を持ち、起動画面と壁紙で毎回使っている**。
  スプライトの読み込み (`LOAD_SPRITE_IMAGE_BMP`) だけが BMP しか読まない。
- littlefs で数えると、P4 の像 (8 MB 区画) は **1,021 区画 (3.99 MB) → 838 区画 (3.27 MB)、-183 区画 = -0.71 MB**。作業ツリーに残った `cache/`
  も外すと 820 区画 (-0.79 MB)。Retro (NARYAv3) は 882 → 754 区画 (-0.50 MB)。公開のブラウザ版・.deb の画像は 1.37 MB → 0.12 MB。
- 自前の RGB332 RLE は PNG の 2.5〜4 倍大きく、復号器と道具を新しく作る必要がある。QOI (LovyanGFX に復号器あり) も PNG より大きい。
- **推奨: PNG に寄せる (読む側は BMP も読み続ける混在)**。直すのは表示側 2 か所の `LOAD_SPRITE_IMAGE_BMP` (先頭の 8 バイトで見分ける)、アプリの
  ファイル名、生成の道具。内蔵 RAM の静的な増分は 0 (pngle の 44 KB は起動画面の時点で既に確保済み。5 章)。読み込みは読むバイト数が 1/20 になるので
  速くなる見込み (6 章、実機の確認は既存のログ行で取れる)。

## 1. 画像を読む所

### 1.1 経路は 3 つ

| 経路 | API (Ruby など) | 表示側のコマンド | 受ける形式 |
|---|---|---|---|
| (A) スプライトの絵 | `SpriteImage#load_bmp(path)` (Spinel・mruby・MicroPython・Lua・wasm の SDK は同じコマンドへ) | `FMRB_LINK_GFX_LOAD_SPRITE_IMAGE_BMP` | **8 bit 無圧縮 BMP だけ** (`fmrb_bmp332_parse`。パレットは見ず、画素の値を RGB332 として使う) |
| (B) 一枚絵 | `FmrbGfx#create_image` / `draw_image` / `load_image` | `CREATE_IMAGE_FROM_FILE` / `DRAW_IMAGE` | P4・ネイティブ・ブラウザ: **PNG と JPEG** (JPEG は P4 の HW 復号、ホストは soft)。Retro・sim: **PNG だけ** |
| (C) 表示側への写し | `FmrbGfx#sync_file(src, dest:)` | `FILE_TRANSFER` (Retro・sim)、同じ FS への複製 (P4・ネイティブ・ブラウザ。host_task.c `host_file_local_write`) | 形式を問わない (大きさと CRC32 で比べる) |

ほかに BMP を読む所が 2 つある: BASIC の文字表 (`components/basic/assets/basic_assets.c`、`/usr/share/basic/*.bmp`、1・4・8 bpp、パレットの番号 0-3
を色の番号として使う) と、スプライトエディタ (`BMP332.load` / `BMP332.save`、picoruby-fmrb-bmp332)。マスク (`draw_image_masked`) はファイルから
読まない (メモリから作る 1 bpp)。

### 1.2 端末ごとの実装

| 端末 | (A) スプライト | (B) 一枚絵 | 備考 |
|---|---|---|---|
| P4 (Tab5・NARYAv4、display_local_task.cpp) | ファイル全体を `fmrb_sys_malloc` (PSRAM のシステムプール) に読み、`fmrb_bmp332_parse` → 1 画素ずつ `drawPixel` で RGB565 のスプライト (PSRAM) へ。上限 256 KB | PNG は RGB565 のスプライト (PSRAM) に `drawPng` で 1 度だけ復号して持つ。上限 256 KB・2048x2048 | (C) は同じ littlefs の中の複製 (`/cache/...`)。**元と写しの 2 つが storage に残る** |
| ネイティブ版・ブラウザ版 | P4 と同じファイル (hosted/hosted.cmake が display_local_task.cpp を使う) | 同じ | 容量の制約は無い。ブラウザは束 (.data) の大きさがダウンロードになる |
| Retro (graphics-audio、ESP32 WROVER) | ファイル全体を **`malloc` (= 内蔵 RAM。`CONFIG_SPIRAM_USE_CAPS_ALLOC`、`SPIRAM_USE_MALLOC` 無し)** に読み、parse → `drawPixel` で RGB332 のスプライト (スプライトプール) へ。**上限 64 KB** (`GH_SPRITE_BMP_MAX_FILE`) | PNG の生データを PSRAM に持ち、**`DRAW_IMAGE` のたびに `drawPng` で復号** (キャンバスへ直接)。上限 200,000 B | (C) は UART 921,600 bps (実効 約 90 KB/s) で GA 側の littlefs (2000 KB 区画) へ |
| Linux sim (graphics-audio の Linux ビルド) | Retro と同じコード、上限 256 KB | 同じ | (C) はソケット越しで遅い (48 KB に 1.9 秒、doc/user_extension/voice/report/v2.md)。`tiles_hand.bmp` は 4,154 ms (doc/fullscreen_hires/report/h5_sim.md) |

### 1.3 アプリごとの読み込み

| 使う所 | ファイル | 経路 | いつ読むか |
|---|---|---|---|
| 起動画面 | `/boot/boot.png` (200x200、652 色) | (B) | 毎回の起動 (`boot_splash` が false でなければ) |
| 壁紙 | `/usr/share/backgrounds/bg_[cyber_]WxH.png`。Retro・sim は `/data/bg_426x240.png` へ写して読む | (B) (+C) | 起動時と再描画のたび (Retro は毎回復号) |
| ランチャー・デスクトップのアイコン | `usr/share/icon/*.bmp`、各アプリの `*.bmp` (24x24) | (C) で `/cache/launcher/` へ → (A) | 起動時にまとめて (12〜15 枚) |
| 麻雀 | `tiles_river.bmp` (240x128)、`tiles_hand.bmp` (420x224、640x360 の卓)、`large/tiles_river_r90/r180/r270` (P4 は観戦と鳴き)、`large/tiles_hand2_*`・`tiles_river2_*`・`tiles_side` (1280x720 の卓、ネイティブ・ブラウザだけ) | (C) で `/cache/app/mahjong/` へ → (A) | **アプリの開始時にまとめて** (`load_sheet`、1 枚ごとに `mahjong: tiles: <名前> N ms` をログに出す)。1280x720 では 10 枚 973 KB |
| flappy・breakout・picorabbit・rpg_demo・picoruby デモ | 16x16 前後の BMP | (C) で `/cache/app/<名前>/` へ → (A) | 開始時にまとめて (数枚〜十数枚) |
| shooter | `usr/share/sprites/shooter/*.bmp` 12 枚 | (C) → (A) | 開始時 |
| walk_scroller (F0 で像から外した) | 背景 15 枚 (426 幅) と歩く人 5 枚 | (A) | 開始時 |
| BASIC | `usr/share/basic/font_b.bmp`・`tile_a.bmp` (128x128) | core が直接 (basic_assets.c) | BASIC の起動時 |
| AppStore・picorabbit のスライド | PNG (32x24 など) | (B) | 表示のたび |

## 2. 今の画像と大きさ

### 2.1 測り方

Pillow 12.3 で (1) **PNG**: 使う色だけのパレット (1/2/4/8 bit) と、番号 = RGB332 の 256 色パレットの両方を `optimize=True` で作り、小さい方 (可逆)。
RGB の PNG (壁紙) は色が 256 以下なら同じ色のパレット PNG に作り直した (可逆)。(2) **RLE**: RGB332 の 1 バイト画素を (個数, 値) の対で詰め、8 バイトの
頭を足した見積り (PackBits もほぼ同じ)。(3) **QOI**: 標準の符号化器を書いて測った。台本は scratchpad の `measure.py`・`analyze.py`・`final.py`。
「区画」は littlefs の 4 KB ブロック数の見積り (512 B 以下は inline で 0、それ以外は切り上げ)。

### 2.2 用途ごと (git で追っているもの)

| 用途 | 枚数 | 今 (B) | PNG (B) | RLE (B) | 区画 今→PNG | P4 の像 | Retro の像 |
|---|---|---|---|---|---|---|---|
| 麻雀 large/ | 10 | 973,084 | 22,967 | 198,562 | 243 → 9 | 3 枚 | 0 |
| walk_scroller (像から外し済み) | 20 | 668,720 | 11,680 | 40,152 | 175 → 8 | 0 | 0 |
| 麻雀 (手牌・河・アイコン) | 3 | 128,610 | 6,291 | 38,302 | 33 → 2 | 3 | 0 |
| 壁紙 (PNG) | 8 | 124,915 | 66,730 | (PNG のまま) | 35 → 21 | 4 | 2 |
| BASIC | 2 | 34,924 | 3,537 | 15,970 | 10 → 2 | 2 | 2 |
| shooter | 12 | 24,376 | 1,774 | 1,942 | 14 → 0 | 12 | 12 |
| アイコン | 12 | 19,848 | 1,526 | 2,012 | 12 → 0 | 12 | 12 |
| 起動画面 (PNG、652 色) | 1 | 14,658 | 14,658 | (PNG のまま) | 4 → 4 | 1 | 1 |
| rpg_demo | 9 | 13,798 | 2,018 | 2,250 | 9 → 0 | 9 | 9 |
| picorabbit | 8 | 10,672 | 1,406 | 1,234 | 8 → 0 | 8 | 8 |
| flappy | 6 | 7,876 | 921 | 790 | 6 → 0 | 6 | 6 |
| breakout | 6 | 7,156 | 632 | 244 | 6 → 0 | 6 | 6 |
| その他 (アプリのアイコン 2・sprites/test・asterism・app/test) | 7 | 9,629 | 2,348 | — | 5 → 1 | | |
| **計** | **104** | **2,038,266** | **136,488** | **443,710** | **560 → 47** | | |

### 2.3 大きいもの (1 枚ずつ)

| ファイル | 大きさ | 色数 | 今 | PNG | RLE | QOI | 使う所 |
|---|---|---|---|---|---|---|---|
| mahjong/large/tiles_hand2_a.bmp・_b | 840x224 | 9・14 | 189,238 ×2 | 3,092・2,096 | 25,018・25,818 | 26,023・27,708 | 1280x720 の手牌 (ネイティブ・ブラウザ) |
| mahjong/large/tiles_river2_r0/90/180/270 | 480x256・640x192 | 14 | 123,958 ×4 | 2,675〜2,708 | 25,042〜28,194 | 25,516〜29,428 | 1280x720 の河 |
| mahjong/tiles_hand.bmp | 420x224 | 14 | 95,158 | 3,899 | 25,572 | 24,165 | 640x360 の手牌 (P4) |
| mahjong/tiles_river.bmp・large/tiles_river_r90/r180/r270 | 240x128・320x96 | 14 | 31,798 ×4 | 2,217〜2,312 | 12,532〜14,032 | 10,901〜11,136 | 河・向きを変えた牌 (P4) |
| walk_scroller/scene*_far | 426x160 | 4〜5 | 69,558 ×5 | 376〜1,523 | 836〜6,264 | 1,436〜6,611 | 遠景 |
| walk_scroller/scene*_mid・_gnd | 426x84・426x50 | 2〜8 | 37,030・22,478 ×5 | 152〜1,158 | 588〜4,016 | — | 中景・地面 |
| backgrounds/bg_cyber_640x360.png | 640x360 | 28 | 22,734 | 12,314 (パレット) | (51,337) | 62,542 | P4 の壁紙 |
| backgrounds/bg_cyber_426x240.png | 426x240 | 28 | 19,615 | 10,445 (パレット) | (31,294) | 37,564 | 壁紙 |
| basic/font_b.bmp・tile_a.bmp | 128x128 | 2・4 | 17,462 ×2 | 1,071・2,466 | 7,996・7,974 | — | BASIC の文字表 |
| boot/boot.png | 200x200 | 652 | 14,658 | 14,053 (RGB のまま) | (RGB332 にすると色が落ちる) | 14,714 | 起動画面 |
| アイコン 24x24 | 24x24 | 2 | 1,654 ×12 | 102〜143 | 90〜258 | — | ランチャー |
| 16x16 前後のスプライト | 4x4〜80x60 | 2〜10 | 1,094〜5,878 | 94〜212 | 22〜474 | — | 各ゲーム |

- **RGB332 で描かれるもの**: BMP は全部 (画素の値が RGB332)。Retro はスプライトもキャンバスも RGB332。P4 などは RGB565 のスプライトに入るが中身は RGB332
  の色。壁紙と起動画面は Retro では RGB332 に落ちて描かれ、P4 では RGB565。壁紙は RGB332 ちょうどの色ではない (4 色・28 色) ので、RGB332 の自前の
  形式にすると P4 で色が変わる。起動画面は 652 色で RGB332 に収まらない。
- 小さいスプライトは 1 枚 1.1〜1.7 KB でも **littlefs では 4 KB の区画を 1 つずつ使う**。PNG にすると大半が 512 B 以下になり inline に入る
  (区画を使わない)。数で効くのはこちらで、P4 の像の BMP 64 枚のうち 57 枚が inline に入る。

### 2.4 littlefs の像で数えた結果

f01 と同じ作り方 (`flash/` の写しから tmp・var/cache を除き、除外の一覧を当てる。flash_local は入れない) で、`littlefs-python 0.13.3`
(`block_size=4096`・`name_max=64`) に書いて使用ブロックを数えた (台本 `lfsimg.py`)。変える案は、スプライトの BMP をパレット PNG に、壁紙を可逆の
パレット PNG に、BASIC の 2 枚を 1 bpp・4 bpp の BMP (4 bpp は大きさの見積り) にしたもの。

| 端末 (区画) | 今 | `cache/` だけ外す | スプライトだけ PNG | 全部 (案 A) | 案 A + `cache/` を外す |
|---|---|---|---|---|---|
| Tab5・NARYAv4 (8 MB = 2,048) | 1,021 (3.99 MB) | 934 (-87) | 844 (-177) | **838 (-183、-0.71 MB)** | 820 (-201、-0.79 MB) |
| NARYAv3 (5 MB = 1,280) | 882 (3.45 MB) | 795 (-87) | 757 (-125) | **754 (-128、-0.50 MB)** | 736 (-146、-0.57 MB) |

(sim が並行して `flash/` に書くので、回によってファイル数が数個ずれた。区画の差は ±数個の幅で読む。)

- **`flash/cache/` について (f01 の記述の訂正)**: `cache/` はビルドが作るものではなく、sim・ネイティブ版・ブラウザ版が表示側の写し (`sync_file` の
  宛先) として作業ツリーに書いたものの残り (`.gitignore` 済み、日付は 8〜10 月)。CMake の staging が `flash/` を丸ごと写すので、**手元のビルドの像
  にだけ入る** (CI のきれいな checkout の像には無い)。今の作業ツリーで 36 ファイル 207 KB。Retro の像にも麻雀の写し (127 KB) が入っているが、Retro
  の写しの宛先は GA 側の FS なので core の像の `cache/` は使われない。

### 2.5 公開のブラウザ版・.deb

git の画像から共通の除外を引いた 82 枚: **1,367,301 B → 124,087 B** (麻雀 large/ の 973 KB が 23 KB になるのが大半)。ブラウザ版はこの分だけ
最初のダウンロードが軽くなる。

## 3. 形式の比較

| | PNG (パレット) | 自前の RGB332 RLE | QOI | BMP (今) |
|---|---|---|---|---|
| 大きさ (git の BMP 96 枚) | 69,758 B | 307,222 B | 約 350 KB | 1,913,351 B |
| P4 の像の区画 (2.4) | -183 | 見積り -150 前後 (inline に入らないものが多い) | RLE と同程度 | 0 |
| 復号器 | 両方の表示側に既にある (lgfx_pngle、起動画面・壁紙で毎回動いている) | 新しく 2 か所 (display_local、graphics-audio) + ホストの道具 | LovyanGFX に既にある (`drawQoi`) | ある |
| 道具・編集 | どの画像エディタでも開ける。Ruby の書き出しは zlib で 40 行 (tools/mahjong_tilegen.rb の `to_png` が既にある) | 書き出し・見る道具を新しく作る。画像エディタで開けない | 対応するエディタが少ない | エディタで開ける |
| 復号の重さ | inflate + フィルタ。ホストで 1.2〜2.3 ns/画素 (6 章) | ほぼ memcpy | 軽い | 無し (ただし読むバイトが多い) |
| 作業 RAM | pngle_t 44,408 B (起動時に確保済みを再利用) + 行バッファ 幅+2 B | ほぼ 0 | qoi_t 約 300 B (calloc、都度) | ファイル全体 |
| RGB332 に収まらない絵 | そのまま扱える | 扱えない (壁紙・起動画面は別の形式が残る) | 扱える | — |

## 4. 推奨: PNG に寄せる (読む側は BMP も読む混在)

| 画像 | どうするか | 理由 |
|---|---|---|
| スプライトの BMP (アイコン・各ゲーム・麻雀・walk_scroller・rpg_demo など、git の 96 枚) | **パレット PNG** (使う色だけ。色の値は今の RGB332 の並びのまま) | 1/10〜1/60。小さいものは inline に入り区画を使わない |
| 壁紙の PNG 8 枚 | **同じ色のパレット PNG に作り直す** (可逆) | -45%。読む経路は変わらない |
| 起動画面 `boot.png`、asterism のアイコン | そのまま | 652 色・RGB。既に PNG |
| BASIC の `font_b.bmp`・`tile_a.bmp` | **BMP のまま 1 bpp・4 bpp に** (コード変更なし。読み手が 1/4/8 bpp を受ける) | 17,462 B → 約 2,100 B・8,310 B。文字表は画像エディタで直す前提の BMP なので形式を変えない |
| スプライトエディタが保存する絵 | BMP のまま (読み込みは両方受ける) | 端末の上で deflate を書く必要が無い |
| 利用者の BMP・App Store の絵 | 読み続ける | 先頭の 2 バイト `BM` か 8 バイトの PNG の署名で見分ける |

### 4.1 直す所

| 所 | 内容 |
|---|---|
| fmruby-core `main/drivers/display_local/display_local_task.cpp` の `LOAD_SPRITE_IMAGE_BMP` (P4・ネイティブ・ブラウザ共通) | PNG の署名なら、PSRAM に 8 bit (rgb332_1Byte) の仮のスプライトを作って `drawPng` し (ここで RGB888→RGB332 は切り捨てで、RGB332 の並びのパレットは元の値に戻る)、今の BMP と同じ画素の書き込みへ渡す。**今と同じ画素の値になる**ので透過色 (麻雀の 0xE3 など) の比較が変わらない。RGB565 のスプライトへ直接 `drawPng` すると RGB332→565 の変換の経路が BMP と違い、透過色が合わない恐れがあるので避ける |
| fmruby-graphics-audio `main/graphics/graphics_handler.cpp` の `LOAD_SPRITE_IMAGE_BMP` (Retro・sim) | PNG の署名なら、ファイルを **PSRAM** (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`、Linux は malloc) に読み、RGB332 のスプライトへ直接 `drawPng`。BMP の経路の内蔵 RAM への一時確保 (最大 64 KB) をこの経路では使わない |
| 線の形 (fmrb_wire) | 変えない。コマンド名 `LOAD_SPRITE_IMAGE_BMP` と Ruby の `load_bmp` はそのまま (BMP も PNG も受けると注釈と help に書く)。名前を変えるなら別の段で `rake wire:check` |
| アプリ・カーネルのファイル名 (`.bmp` → `.png`) | launcher.rb (`S_BMP_EXT`)・desktop_icons、mahjong (`load_sheet` の名前)、flappy、shooter、rpg_demo、picoruby デモ、breakout、walk_scroller、picorabbit (lib/gems の renderer)、`*.app.toml` の `icon` は `.icon` のままで良い (BMP の名前は launcher が作る)。全部 mruby・Spinel の両方の構成を通る Ruby |
| 生成の道具 | tools/gen_icon_bmp.rb (`rake icons`)、gen_shooter_sprites.rb、gen_picorabbit_sprites.rb、mahjong_tilegen.rb、flash/app/game/breakout/generate_sprites.rb、rpg_demo/generate_*.rb、tools/basic/gen_basic_*.rb (BASIC は bpp だけ)。共通の小さな PNG 書き出し (パレット・bit 深さ) を 1 つ置く。道具の無い絵 (flappy・walk_scroller) は一度だけ変換 |
| 文書 | flash/help の SpriteImage・FmrbGfx (en/ja)・index.txt、fmrb-app-new の skill、doc/reference の該当箇所 |
| 回帰 | `regress:screens` で画素が変わらないこと (同じ値になる作りなので基準は動かない見込み)、`regress:size` |

### 4.2 別に効くもの (形式と独立、親とユーザの判断)

1. **Modern (同じ FS) では `sync_file` の複製をやめ、元のパスから直接読む**: P4・ネイティブ・ブラウザは表示側が core と同じ FS を見ているので、
   `/cache/...` への複製は storage を倍使い、CRC のための読みを 2 回足すだけ (32 KB の複製に 353 ms の実測、voice/report/v2.md)。`FmrbGfx#sync_file`
   の中で Modern なら何もせず元のパスを返す形にすると、アプリの側は変わらない。
2. **staging で `flash/cache` を外す**: 像に入るのは開発者の作業ツリーの残りだけで、端末ごとに中身が違う。外すと -87 区画 (P4・NARYAv3 とも)。
   f01 は「最初の起動が遅くなるので残す」としたが、1 を入れれば複製そのものが無くなる。
3. **pngle_t (44,408 B) が内蔵 RAM にある見込み**: `LGFXBase::draw_png` は static の pngle を `malloc` で 1 度だけ作り、解放しない。core も
   graphics-audio も `CONFIG_SPIRAM_USE_CAPS_ALLOC` で malloc は内蔵 RAM。起動画面の `drawPng` で確保され、以後ずっと残る。PSRAM へ移せば内蔵 RAM が
   約 44 KB 空く可能性がある (lz_buf を PSRAM に置いた時の復号の速さは要計測)。M1 のログで起動画面の前後の `internal=` の差を見て確認するのが先。

## 5. 内蔵 RAM

| 端末 | 今 (BMP) | 案 A (PNG) | 静的な増分 |
|---|---|---|---|
| P4 | ファイル全体を PSRAM のシステムプールへ (最大 256 KB、一時) | ファイル (PSRAM、1/20) + 仮の 8 bit スプライト w×h (PSRAM、一時)。pngle_t 44 KB は起動画面で確保済みのものを再利用。行バッファ (幅+2 B) は内蔵で、次の PNG まで残る (今も壁紙の分が残っている) | 0 |
| Retro (WROVER) | ファイル全体を **内蔵 RAM** へ (最大 64 KB、一時) | ファイルを PSRAM へ。pngle_t は起動画面・壁紙の `drawPng` で確保済み。行バッファ ≤ 約 1 KB | 0 (一時の山は下がる) |
| ネイティブ・ブラウザ | 制約なし | 同じ | — |

条件: 起動画面 (`boot_splash = false`) と壁紙 (`none`) を両方切った端末では pngle_t が起動時に無く、最初の PNG で 44 KB が確保される。これは今も
AppStore の絵などで起きることで、案 A で新しく生じる話ではないが、4.2 の 3 で一緒に扱える。

## 6. 読み込みの時間

### 6.1 復号の重さ (ホストで実測)

lgfx_pngle.c と lgfx_miniz.c (hosted/vendor/m5gfx) をそのまま `gcc -O2` で組み、200 回の平均を取った (i7-13700、台本 `bench.c`)。BMP は
`fmrb_bmp332_parse` + 画素の写し。どちらも表示側の画素の書き込み (`drawPixel`) は含まない (両案で同じ)。

| 画像 | BMP | PNG (使う色だけ) | PNG (256 色、番号 = RGB332) |
|---|---|---|---|
| tiles_hand 420x224 | 0.020 ms (95 KB) | 0.215 ms (4,041 B、4 bit) | 0.110 ms (3,899 B) |
| tiles_hand2_a 840x224 | 0.047 ms | 0.339 ms | — |
| tiles_river 240x128 | 0.006 ms | 0.064 ms | — |
| scene0_far 426x160 | 0.016 ms | 0.096 ms | — |
| font_b 128x128 | 0.003 ms | 0.025 ms | — |
| アイコン 24x24 | <0.001 ms | 0.003 ms | — |

8 bit のパレット PNG は 4 bit より復号が速い (ビットの展開が無い)。大きいシートは 8 bit で作るのが良い (大きさの差は数 %)。

### 6.2 端末での見積り

- **復号**: ホストとの比を P4 (360 MHz、in-order) で 30〜60 倍、WROVER (240 MHz) で 60〜100 倍と置くと、tiles_hand の PNG は P4 で 3〜13 ms。
  アイコン・16x16 は 1 ms 未満。
- **読むバイト数**: 麻雀の開始 (P4、640x360) は河と手牌で BMP 127 KB を、`sync_file` の CRC (元と写し) と読み込みで **3 回、計 381 KB** 読む。PNG
  なら 6.2 KB × 3 = 19 KB。littlefs の読みを 0.5〜2 MB/s と置くと 190〜760 ms が 10〜40 ms になり、復号の増分 (十数 ms) を十分上回る。初回 (写しが
  無い時) の書き込みも 127 KB → 6 KB (32 KB に 353 ms の実測から 約 1.4 秒 → 0.07 秒)。
- **Retro**: スプライトの BMP は約 120 KB → 約 10 KB。初回の UART 転送 (約 90 KB/s) が約 1.3 秒 → 0.1 秒。GA 側の 2000 KB の区画の写しも同じだけ減る。
- **sim**: tiles_hand.bmp の 4,154 ms (転送込み) が 0.2 秒前後の見込み。
- **確認の仕方**: 麻雀は `mahjong: tiles: <名前> N ms` のログ行が既にあるので、同じコミットのビルド同士で前後を比べる (P4 の実機・sim)。アイコンは
  起動ログの時刻差。

### 6.3 まとめて読むもの

麻雀は開始時に 2 枚 (P4 の 640x360)、1280x720 では 10 枚 973 KB を続けて読む。ランチャーは起動時にアイコン 12〜15 枚。flappy・shooter・rpg_demo・
picorabbit は開始時に数枚〜12 枚。どれも PNG にすると読むバイトが 1/10 以下になる。

## 7. 決めること (ユーザ)

1. 形式: **PNG に寄せる (推奨)** / 自前の RGB332 RLE / このまま。
2. `load_bmp` の名前: そのまま (推奨、両方を受けると書く) / `load` を足して `load_bmp` を残す。
3. 4.2 の 1・2 (Modern の `sync_file` の複製をやめる、`flash/cache` を像から外す) を F2 に入れるか、別の段にするか。
4. 4.2 の 3 (pngle_t を PSRAM へ) を調べるか。F4 の内蔵 RAM とは独立。
