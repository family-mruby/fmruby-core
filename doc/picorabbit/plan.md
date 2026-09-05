# PicoRabbit (Tab5) の拡張計画

> 状態: 進行中 | 更新: 2026-09-03 | P0-P4・P6-P8 完了 (P8 = 動画 .mjpg、Tab5/wasm/sim 検収済)。残は P5 (見せ場、任意)

PicoRabbit は Harucom 向けの Markdown 発表ツールで、Family mruby には
`flash/app/tool/picorabbit.app.rb` + gem `lib/add/picoruby-fmrb-picorabbit`
(parser / renderer / slide) として基礎が移植済みである (`/home/slides/demo.md`、
全画面起動、`fmrb` コードブロックの事前コンパイル、Ctrl+Tab の park)。
本計画は、これを**日本語の発表で Tab5 から使える**ところまで持っていく。

前提: Tab5 はフレームバッファ 426x240 を 1280x720 に**整数 3 倍**で拡大する。
16x16 のドットは画面上 48px で、ドット絵としてそのまま見える。
実装の気づき・計測値は `report/` に置く。

## 先に直すこと (拡張の土台)

| # | 現状 | 直し |
|---|---|---|
| 1 | `render_timer` が**亀 = スライド進捗、うさぎ = 時間**になっている | Rabbit 本家 (と Harucom 版が直した形) は**うさぎ = 発表者の進み、亀 = 時間**。入れ替える。居眠りの仕掛け (後述) はこの向きでないと成り立たない |
| 2 | キー判定が `ev[:keycode]` | `ev[:scancode]` に揃える (sim は SDL keysym、実機は HID Usage ID で値が食い違う既知の罠) |
| 3 | 文字描画が全部 `:default` フォント、`mixed` 無し | 和文は `draw_text_mixed`、折り返し幅は `text_width(str, :default)` の全角換算で数える |
| 4 | `on_update` が 200ms ごとに footer + timer の帯を**塗り直して present** している (5 回/秒) | スプライト化 (P1) で消える。それまでの応急処置はしない |
| 5 | 背景色が theme を見ていない箇所があれば theme_bg / theme_fg に (apps テーマ統一の規約) | renderer の `@theme.bg` を `FmrbConst::THEME_WINDOW_BG` 既定に |

## P1: うさぎと亀のスプライト化 (一番効くところ)

今は `"*"` の文字と 4x4 の矩形。`SpriteImage` + `SpriteInstance` に置き換える。

- **16x16、1 コマ 1 BMP**、全 8 コマ。sprite_editor (TILE=16) で描ける形。

  | キャラ | コマ | 用途 |
  |---|---|---|
  | 亀 | 歩き 2 | 1 秒ごとに交互 (時間の進み) |
  | 亀 | 万歳 1 | 時間切れ (ゴール到達) |
  | うさぎ | 走り 2 | ページ送り直後しばらく |
  | うさぎ | ジャンプ 1 | ページ送りの瞬間 (今の jump 物理を流用) |
  | うさぎ | 居眠り 1 | 進捗が時間より 10% 以上先行 |
  | うさぎ | 焦り 1 | 進捗が時間より 10% 以上遅れ |

- 表示は `move` + `frame=` + `present` だけ。composite 側で合成されるので
  **スライド本文を汚さず、帯の塗り直しも不要**。`on_update` は 1 秒周期に
  落とし、変化が無ければ present しない (FmrbUI と同じ考え方)。
- 置き場所: `flash/usr/share/picorabbit/usagi_*.bmp`、`kame_*.bmp`。
- 下書きは Claude がテキストのドット表から BMP に起こして画像で見せ、
  気に入らないコマを sprite_editor で直す分担。
- 居眠り/焦りは「うさぎの進捗 − 亀の進捗」の符号と大きさで決める。
  時間切れで亀が万歳、うさぎはそのまま。

## P2: 日本語対応 (発表の前提条件)

**完了 (report/p2.md)**。予定と変わった点は 2 つ: `text_size` は theme では
なく frontmatter を renderer が直接読む形にし、ルビは parser ではなく
renderer の `parse_inline` の担当にした (inline 書式なので置き場所が揃う)。

- 本文・箇条書き・見出しを `draw_text_mixed` 化、折り返しを全角 2 桁換算に。
- 見出しは `set_text_size(2)` (16px = 画面上 48px)。本文 8px は画面上 24px で
  手元では読めるが、**プロジェクタでは本文も 2 倍が要る**可能性が高い。
  theme に `text_size` を持たせ、`.md` の frontmatter (`text_size: 2`) で
  切り替えられるようにする。
- ルビ `{漢字|かんじ}`: parser の inline に 1 種追加、renderer は本文の上に
  8px (text_size 1) で小さく描く。
- 全角の禁則処理はやらない。

## P3: 発表者支援

本家の割り当て (rabbit_behavior.md) に寄せる。

- タッチ: **1 本指タップで次のスライド、2 本指タップ (右クリック) で前**
  (今は mouse_up が全部 advance)。本家はクリック = 次のスライド (段階送り
  でない) なので、タップも段階を飛ばす側に揃える。画面の左右で分ける案は
  Tab5 のタッチが相対移動 (タップ = カーソル位置) なので不成立
  (instruction_p3.md T1)。
- 黒画面 Shift+b / 白画面 Shift+w (本家)、残り時間の数字表示トグル
  (**Shift+t**、本家 timer テーマ相当: 超過は赤)、タイマー一時停止
  (**Shift+p**、質疑用。本家に無い)、索引モード `i` (一覧からのジャンプ。
  本家の番号ジャンプ相当)。**発表者支援は Shift 層に置く**: 素の t / p /
  b / w は P0 で「タイマー戻し」「前」に使ってしまっている。
- ゴールを手前に置く指定 (本家の `image-slide-number-last-slide`)。質疑や
  付録をコースから外すため。**スライドの中に `{::goal/}` の行**を置く形に
  決めた (`{::wait/}` と同じ拾い方で済むため)。
- 時間切れで亀が万歳した瞬間に短い音 (`note_on` 1 回)。frontmatter
  `chime: true` のときだけ。

## P4: 起動メニューと書き出し

発表の道具として一人で完結させる段。詳細は instruction_p4.md。

**sim まで完了 (report/p4.md)。実機 (Tab5) の確認だけ残っている**。予定と
変わった点: 命令番号は 0x33、`intro_ja.md` は 8 枚ではなく 9 枚、sim の
`/mnt/sd` はアプリが作る普通のディレクトリ (POSIX には SD のマウントが無い)。
書き出したファイルの拡張子は書く側に従う (実機 `.jpg` / sim `.bmp`)。

- **起動時のメニュー画面**: `/home/slides` と `/mnt/sd/slides` の `.md` を
  一覧し、選んで開始する (今は先頭のファイルを開くだけで、日本語デッキを
  実機で開く手段が無い)。操作は索引モードと同じ作法 (Up/Down/Enter、
  カーソルを乗せてタップ)。Start / Export / Quit のボタンは FmrbUI。
- **Export**: 選んだデッキの全スライドを**デッキ名のディレクトリ**に
  1 枚 1 ファイルで書き出す (`/mnt/sd/picorabbit/<デッキ名>/01.jpg ...`)。
  **ブラウザ版だけ `/home/picorabbit/<デッキ名>/`** (2026-09-05)。あちらに
  カードは無く、`/mnt/sd` はリロードで消える記憶なので、残る場所へ出す。
  各スライドは wait を全部開いた最終状態、うさぎ・亀・残り時間は隠す、
  ページ番号は残す。
- 書き出しの実体は **GFX 命令 1 つ** (「直前の present の合成結果を
  指定パスへ書く」)。Tab5 は remote desktop 用にある P4 のハードウェア
  JPEG エンコーダ (`rd_encoder_jpeg`、426x240 を幅 432 に詰めて符号化)
  を流用し、sim は SDL の BMP 書き出しで同じ命令を受ける (流れの検収用)。
  Retro (WROVER) は非対応 (NOT_SUPPORTED)。
- 解像度はまず 426x240。共有用の 1280x720 は後から (DSI フレーム
  バッファは縦長に回転しているので別扱い)。
- 日本語の確認用デッキ `intro_ja.md` (発表らしい構成、ルビ・wait・
  goal・chime 入り) を正式サンプルとして同梱する。

## P5: 見せ場 (任意、後回し)

- `fmrb` ブロックで家紋やレイキャストをスライド内で動かすデモ。
  Ctrl+Tab でアプリを切り替えられるので、スライド内で動かす優先度は低い。
- スライド切替のワイプ 1 種類。

## P6: 本文を efont で描く

Modern の本文が粗いのは 8px の字を整数倍に引き伸ばしているため。本文を
**efont 12 (等倍)**、見出しを **efont 16**、太字を **efont 12 bold** で描く。
フォントは display 側の持ち物なので、Ruby 側に機種ごとの持ち物表を置き、
無い機種 (Retro は WROVER の区画が小さい) では近いものに落として同じ .md を
そのまま出す。Gothic / 24px 以上は入れない (焼き時間)。詳細は
instruction_p6.md。

## P7: スライドに画像を載せる

`![alt](path)` は parser が拾うところまであるが、描画側にファイルを渡して
いない (Retro と sim では別ストレージ) ので出ない。`sync_file` で写し、
本文幅と残り高さに等比で収める (`![w=200](..)` / `![60%](..)` で指定も可)。
PNG のみ。詳細は instruction_p7.md。
**画像は 426x240 以下・100KB 以下**を目安にする (それ以上は Retro の転送と
描画側のメモリで苦しい。初回の転送はスライドを出すのを待たせる)。

## P8: スライドに動画を載せる

`![fps=15](movies/demo.mjpg)` のように **拡張子 `.mjpg` の `![..](..)`** を
動画として扱う。描くのは renderer ではなく表示側の再生器
(`FmrbGfx#video_open`、doc/archive/video) で、renderer は絵の置き場所に
開いて present の後に動かし、次に canvas を塗る前に止める。alt は空白区切りの
語で読み、`fps=NN` (1〜30) と `once` (繰り返さない) が動画の指定。詳細は
instruction_p8.md。

- **Modern (Tab5) とブラウザ版 (wasm) で動く**。Retro と sim では
  `video_open` が nil なので `[video: path]` の 1 行になる。
- 再生器は**拡縮しない**ので、コマの大きさのまま置き、本文幅と残り高さに
  入らなければ代替表示。1 枚に 1 本。音は無い。幅・高さは 16 の倍数、
  P4 は 448x256 まで。
- wait の段階送りも描き直しなので、動画は段階ごとに先頭からやり直す。
- サンプルは `flash/usr/share/samples/slides/movies/demo.mjpg` (288x160、
  30 コマ、`tools/gen_test_mjpg.py`)。実物は
  `ffmpeg -i src.mp4 -vf "scale=288:160,fps=15" -q:v 4 -f mjpeg out.mjpg`。

## P9: 背景画像と行ごとの文字サイズ

- **文字サイズ**: 寄せと同じ行指示で `{:.small}` / `{:.large}` / `{:.xlarge}`
  (併記可 `{:.center .xlarge}`)。段は 8 / 12 / 16 / 24 / 32 で、24 と 32 は
  12 と 16 の整数倍 (新しい字形は Tab5 の区画に入らない)。本文 (`font_size`)
  を基準に 1 段下 / 1 段上 / 2 段上。text・箇条書き・番号・引用・表紙の副題に
  効く。見出しには効かない。
- **背景画像**: frontmatter `background: path` (デッキ全体) と枚ごとの
  `{::background path/}` (`none` で無地に戻す)。PNG を**画面に引き伸ばして**
  貼る (426x240 で作れば等倍)。背景のある枚は本文を透明描画、見出しの帯・
  コード枠・下の帯は塗りのまま。画像はデッキ内で 1 回だけ作って持つ (3 枚まで)。
- **影付き文字**: `{:.shadow}` (行) か frontmatter `shadow: true` (全行)。
  同じ文字列をテーマの `shadow` 色で右下に 1 セル (24/32 では 2 px) ずらして
  描き、その上に本来の色で描く。影のある行は地色の箱を敷かない (箱が影を
  消すため)。帯の上の見出しと inline code は対象外。背景画像と組み合わせる
  ための機能。
- **見出しの様式**: frontmatter `heading: band | underline | plain` と枚ごとの
  `{::heading X/}`。underline は本文色の字 + 帯色の線 (左端から少し空けて
  右端まで、2px)。表紙は指定が無ければ帯なし (背景画像に載せる)。
- **縦中央**: `{::valign center/}` (枚) か frontmatter `valign: center` (全枚、
  `{::valign top/}` で戻す)。描く前に同じ規則で高さを測り、余りの半分だけ
  下から始める。`fmrb` ブロックのある枚は測れないので上寄せのまま。
- Modern の表示側 DRAW_IMAGE が scale を無視していたのをこの段で直した
  (report/p9.md)。P7 の画像の拡縮も Modern ではこれで初めて効く。
- 詳細は instruction_p9.md。

## 順番と関所

**土台 (1-5) → P1 → P2 → P3 → P4 → P6 → P7 → P8 → P9 → P5**。P1 は絵が先。各段の終わりに sim で
`/home/slides/demo.md` を通し、`GFX STATS` の presents/s が放置中 1/s
(デスクトップの時計のみ) であることを確認する。Tab5 実機の見た目は
ユーザ確認 (現在 Tab5 は off)。

## ルール

- fmruby-core/CLAUDE.md に従う。sprite は `flash/usr/share` に置き、
  アプリは `/usr/share/...` の絶対パスで読む。
- each / times / ブロック保存を避ける (ruby_writing_constraints)。
  このアプリは mruby 専用だが、書き方は揃える。
