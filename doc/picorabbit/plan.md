# PicoRabbit (Tab5) の拡張計画

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

- タッチ: 画面の**右半分タップで進む、左で戻る** (今は mouse_up が全部 advance)。
  本家はクリック = 次のスライド (段階送りでない) なので、タップも段階を
  飛ばす側に揃える。
- 黒画面 Shift+b / 白画面 Shift+w (本家)、残り時間の数字表示トグル (T、
  本家 timer テーマ相当: 超過は赤)、タイマー一時停止 (P、質疑用。本家に
  無い)、索引モード `i` (一覧からのジャンプ。本家の番号ジャンプ相当)。
- ゴールを手前に置く指定 (本家の `image-slide-number-last-slide`)。質疑や
  付録をコースから外すため。frontmatter かスライド属性で 1 つ。
- 時間切れで亀が万歳した瞬間に短い音 (`note_on` 1 回)。frontmatter
  `chime: true` のときだけ。

## P4: 見せ場 (任意)

- `fmrb` ブロックで家紋やレイキャストをスライド内で動かすデモ。
- スライド切替のワイプ 1 種類。

## 順番と関所

**土台 (1-5) → P1 → P2 → P3 → P4**。P1 は絵が先。各段の終わりに sim で
`/home/slides/demo.md` を通し、`GFX STATS` の presents/s が放置中 1/s
(デスクトップの時計のみ) であることを確認する。Tab5 実機の見た目は
ユーザ確認 (現在 Tab5 は off)。

## ルール

- fmruby-core/CLAUDE.md に従う。sprite は `flash/usr/share` に置き、
  アプリは `/usr/share/...` の絶対パスで読む。
- each / times / ブロック保存を避ける (ruby_writing_constraints)。
  このアプリは mruby 専用だが、書き方は揃える。
