# 実装指示書 P0 / P1: PicoRabbit の土台直しと、うさぎと亀のスプライト化

対象: 実装担当セッション。作業リポジトリ: fmruby-core。

先に読むもの:

1. plan.md — 全体計画。本書は「先に直すこと (1-5)」を P0、「P1」をそのまま
   P1 として、コードの裏取りと決定事項を足したもの。
2. rabbit_behavior.md — 本家 Rabbit の挙動 (うさぎ/亀の式、キー、マウス)。
   振る舞いで迷ったらこれに合わせる。
3. doc/ui_widgets/instruction_u1.md の「進め方の約束」 — sim の罠
   (stale build の偽グリーン、3 コンテナ再起動、.env の復元) はそのまま適用。
4. fmrb-app-new skill (アプリの規約と picoruby の地雷)。

## 現状 (コードで確認済み)

- `flash/app/tool/picorabbit.app.rb` (182 行) + gem
  `lib/add/picoruby-fmrb-picorabbit/mrblib/` (parser 195 / renderer 522 /
  slide 51 行)。**lib/ を触ったら `rake clean`**。
- plan の 5 件は全部その通り。加えて 1 番は根が深い:
  `render_timer` (renderer 485 行〜) で**ジャンプ物理 `@usagi_jump_y` が
  緑の `*` = 亀側に付いている**。`*` は元々うさぎのつもりで書かれ、役割だけ
  逆に配線されている。入れ替えで物理も一緒にうさぎへ戻る。
- `on_event` は `ev[:keycode]` を `FmrbConst::KEY_*` と比較している
  (sim は SDL keysym、実機は HID Usage ID で値が違う)。
- 文字は全部 `draw_text`、折り返しは `CHAR_W` の文字数換算。
- `on_update` が 200ms ごとに `redraw_timer_area` で帯を塗って present
  (5 回/秒)。

## 進め方

- 順序は **P0 (1-5) → P1**。報告は report/p0.md、report/p1.md
  (ディレクトリは作る)。
- **関門は 2 つ**:
  - P0 完了時: sim で下の「P0 の検収」を通し、report を見せて確認を待つ。
  - P1 の**絵の承認**: ドット絵 8 コマを画像で見せ、ユーザが OK を出す
    (気に入らないコマは sprite_editor で直す分担)。絵の OK 前にアプリ側を
    作り込まない (位置・状態遷移の骨組みまでは進めてよい)。
- コミットは P0 で 1 本、P1 で「絵 (生成ツール + BMP)」と「アプリ側」の
  2 本。メッセージは英語、ユーザ確認のうえ。**plan.md は未追跡なので P0 の
  コミットに含める** (本書も)。
- **each の扱い**: renderer / parser は each を多用している (11 か所)。描画は
  ページ送り時だけなので、**触る行と新規コードだけ while に揃え、既存の each
  は残す**。一括の書き換えはしない (plan の「書き方は揃える」はこの解釈)。
- このアプリは mruby 専用。Spinel の生成対象ではないので doctor は不要。
- Tab5 は現在 off。実機の見た目はユーザ確認 (P1 の絵は整数 3 倍で 48px に
  なる前提で描く)。

## P0: 土台 (plan の 1-5)

### 1. うさぎ = 発表の進み、亀 = 時間

`render_timer` を次の対応に直す (本家の向き。rabbit_behavior.md):

- うさぎ: **`(slide_idx - 1) / (total_slides - 2)`** を 0..1 に収める
  (本家の式。**タイトルスライドは競走に数えない**: 最初の本文スライドが
  出発点、最後がゴール。スライドが 3 枚未満なら常に 1)。色は `timer_rabbit`
- 亀: `elapsed / allotted_ms` → 色は `timer_turtle`。**1.0 で止める**
  (本家はゴールを過ぎても歩き続けるが、我々は P1 で万歳させる。ここは
  本家と違えると決めた点)
- `@usagi_jump_y` はうさぎの描画に付ける (ジャンプはうさぎのもの)。

P1 で絵に置き換わるので、ここでは `*` と矩形の配線を直すだけでよい
(どちらを何で描くかは問わない)。`redraw_timer_area` も同じ向きになる。

あわせて **`allotted_time` の値の読み方**を広げる (parser か renderer の
`to_i` の箇所): 裸の整数は**分** (Harucom 形式、demo.md の `5`)、
`5m` / `90s` / `1h30m` のような単位付きは本家の書式として秒に換算する。
数えられない値は「タイマー無し」。

### 2. `ev[:scancode]` に揃える

`on_event` の `kc = ev[:keycode]` を `ev[:scancode]` (USB HID Usage ID) に
する。定数は editor.app.rb など既存アプリが scancode をどう名付けているかを
見て**同じ流儀**にする (無ければアプリ内に `SC_RIGHT = 0x4F` のように置く)。
`ev[:character]` の 10/13/32/27 比較は消す。

割り当ては**本家に合わせる** (rabbit_behavior.md の表)。本家は「次 (段階
送り)」と「次のスライド (段階を飛ばす)」を区別するので、それも持ち込む:

| 操作 | キー (scancode) |
|---|---|
| 次 (wait を 1 つ進める) | Space 0x2C / Enter 0x28 / PgDn 0x4E / Tab 0x2B / `n` `f` `j` `l` |
| 次のスライド (wait を飛ばす) | Right 0x4F / Down 0x51 |
| 前 (wait を 1 つ戻す) | PgUp 0x4B / BackSpace 0x2A / `p` `b` `h` `k` |
| 前のスライド | Left 0x50 / Up 0x52 |
| 最初 / 最後 | Home 0x4A, `a` / End 0x4D, `e` |
| 終了 | Esc 0x29 / `q` |
| うさぎジャンプ | **`u`** (本家に無い機能。Up は本家では「前のスライド」なので明け渡す) |
| タイマーを元に戻す | **`t`** (本家は Alt+t。Linux sim では Alt が届かないので修飾なしにする) |

文字キーは scancode (a=0x04 起点) で引く。`ev[:character]` は使わない
(かなモード中に変わる)。

### 3. `draw_text_mixed` と画素幅の折り返し

- 見出し・本文・箇条書き・番号付き・引用の `draw_text` を
  `draw_text_mixed(x, y, str, color, bg)` に。コードブロックと inline code は
  ASCII 前提なので `draw_text` のままでよい。
- 折り返しと中央寄せの幅は **`text_width(str, :default)` の画素幅**で
  数える (文字数 x CHAR_W をやめる)。`text_width` は**現在の text_size を
  掛けて返す**ので、見出し (size 2) を測るときはサイズを合わせてから測るか
  1 で測って 2 倍する (FmrbUI の `measure` と同じ注意)。
- `draw_rich_text` の折り返しは「語単位」を残しつつ、幅判定だけ画素にする。
  和文 (空白が無い) は文字単位で折る。禁則処理はしない (plan P2)。
- 確認用に、和文を含む .md を**一時的に** `flash/home/slides/` に置いて
  sim で見る (コミットしない。正式な日本語サンプルは P2)。

### 4. `on_update` の帯塗り

**P0 では触らない** (plan のとおり応急処置はしない)。P1 で消える。
したがって **P0 の関門では presents/s の条件は適用しない** (P1 から)。

### 5. テーマ既定を theme 定数に

`Theme.default` の `bg` を `FmrbConst::THEME_WINDOW_BG`、`text` を
`FmrbConst::THEME_TEXT` に。他の色 (title_bg 等) は演出色なのでそのまま。
`light` テーマはそのまま。

### P0 の検収 (sim)

`/home/slides/demo.md` で:

- 上の表のキーが `fmrb_input.rb key` で効く (scancode 経路)。特に
  **Space は wait を 1 つ進め、Right は wait を飛ばして次のスライド**に行く
  ことを、wait のあるスライドで 2 枚撮って示す。`q` / Esc で終了、`u` で
  うさぎ側が跳ぶ、`t` で亀が出発点に戻る。
- `allotted_time: 5` (分) と `allotted_time: 90s` の両方でタイマーが出る。
- タイマーの向き: `allotted_time` を短く (1 分) した一時 .md で、**時間とともに
  動くのが亀、ページ送りで動くのがうさぎ**であることをスクリーンショット
  2 枚で示す。
- 和文を含む一時 .md で、本文・見出し・箇条書きが欠けずに出て、折り返しが
  画面幅で起きる。
- Ctrl+Tab で park → 復帰が従来どおり。
- `docker logs fmruby_core | grep -cE "^E \(|Exception"` が 0。

## P1: うさぎと亀のスプライト化

### 資産の形式 (確定事項。sprite_editor で直せる形にするための条件)

sprite_editor (`flash/app/tool/sprite_editor.app.rb`) と既存資産
(flappy / shooter) を読んで確定した条件:

- **16x16、1 コマ 1 BMP**。sprite_editor は 16 の倍数サイズの BMP をタイル
  シートとして開くので、16x16 単体 (1 タイル) もそのまま開ける。
  `SpriteImage#load_bmp` はファイル全体を 1 画像に読むので、コマ = ファイル
  の対応が素直。
- **8bit インデックス BMP、画素バイト = RGB332 値そのもの**
  (ローダはパレットを無視して画素バイトを色として使う。
  `tool/gen_shooter_sprites.rb` 冒頭の説明と同じ)。`BMP332.save` が
  吐く形式。
- **透明は 0x00**。`SpriteImage.new(..., transparent_color: 0,
  use_transparent: true)`。したがって**絵の中で純黒 0x00 は使えない**
  (輪郭は 0x48 などの暗色で)。
- **色は sprite_editor の 16 色パレットだけを使う**
  (`SpriteEditorApp::PALETTE`: 0x00 0xE0 0x1C 0x03 0xFC 0x1F 0xE3 0xFF
  0x6D 0xF0 0x88 0x14 0x5F 0xF4 0x02 0x48)。パレット外の色を置くと、
  ユーザが editor で塗り直したあと**その色に戻せない**。
- 置き場所: `flash/usr/share/picorabbit/`。ファイル名:

  | ファイル | 用途 |
  |---|---|
  | kame_walk1.bmp / kame_walk2.bmp | 亀 歩き (1 秒交互) |
  | kame_banzai.bmp | 亀 万歳 (時間切れ) |
  | usagi_run1.bmp / usagi_run2.bmp | うさぎ 走り (ページ送り直後) |
  | usagi_jump.bmp | うさぎ ジャンプ |
  | usagi_sleep.bmp | うさぎ 居眠り (進捗が時間より 10% 以上先) |
  | usagi_hurry.bmp | うさぎ 焦り (進捗が時間より 10% 以上後) |

### 絵の作り方 (Claude の下書き → ユーザが editor で直す)

- `tool/gen_picorabbit_sprites.rb` を `tool/gen_shooter_sprites.rb` に
  倣って書く (ドット表の文字列が元。1 文字 1 画素、`.` が透明)。
  出力先 `flash/usr/share/picorabbit/`。
- **上書きガード**: 出力先に既にファイルがあれば書かない (`--force` で
  上書き)。ユーザが sprite_editor で直した BMP が**正**になるので、
  生成ツールを再実行して消してはいけない。ツールの冒頭にこの旨を書く。
- 承認用の画像: 8 コマを**横に並べ 3 倍に拡大した PNG** (実機の見え方) を
  作って見せる。PNG の生成は Pillow でよい (画像の生成は Python で可。
  読むだけのツールは Ruby)。scratchpad に置き、コミットしない。
- sim でも見る: `SpriteImage` で読んで画面に置いた実物のスクリーンショットも
  1 枚添える (BMP ローダの解釈違いはここで出る)。
- ユーザが sprite_editor で直す手順を report に 3 行で書いておく
  (ランチャーから sprite_editor → ファイル選択で
  `/usr/share/picorabbit/xxx.bmp` → 編集 → S で保存)。

### アプリ側

- 読み込み: flappy と同じ。`sync_file` で `/cache/app/picorabbit/` へ写し、
  `SpriteImage` を 8 枚 (transparent 0 / use_transparent true)、
  `SpriteInstance` を 2 体 (うさぎ: run1 run2 jump sleep hurry、亀: walk1
  walk2 banzai の順で frames 配列)。読み込みは `on_create` の
  `load_presentation` の後で 1 回。
- `allotted_time` が無い発表ではスプライトを出さない (今の
  `render_timer` が return するのと同じ扱い)。
- 表示は `move` + `frame=` + `present` だけ。**帯の塗り直し
  (`redraw_timer_area`) は消す**。スライド本文にはトラック線だけ残す
  (`render_slide` 内で描く)。
- `on_update` は **1000 を返す**。毎回やるのは:
  1. 亀の x を elapsed から計算、歩きコマを交互に
  2. うさぎの x を slide_idx から計算 (ページ送りで動く)
  3. うさぎの状態: 直近のページ送りから N 秒は run1/run2、それ以外は
     「うさぎ進捗 - 亀進捗」が +0.10 以上で sleep、-0.10 以下で hurry、
     それ以外は run1 で静止
  4. 時間切れ (亀 1.0) で亀 banzai、うさぎはそのまま
  5. **位置かコマが前回と変わったときだけ present**
- ジャンプ (`u`): 1 秒刻みでは跳ねて見えないので、跳んでいる間だけ
  `on_update` の戻り値を 100 にして既存の物理 (`@usagi_jump_y` / `@usagi_vy`)
  を回し、着地したら 1000 に戻す。跳躍中は usagi_jump コマ。
- ページ送り時の present は従来の `render_slide` の中の 1 回だけ。うさぎの
  `move` は render_slide の present の**前**に済ませる (present を増やさない)。
- Ctrl+Tab で park したとき、**スプライトがデスクトップの上に残らない**
  ことを確認する (全画面の退避とスプライトの可視性の組み合わせは
  このアプリが初めて通る経路)。残るなら park/unpark で `visible=` を切る。

### P1 の検収 (sim)

- `allotted_time: 2` の一時 .md で: 起動直後 (亀 0、うさぎ 0) / 1 分後
  (亀 中央、うさぎ 0 = sleep ではなく hurry) / ページを最後まで送る
  (うさぎ右端 = sleep) / 2 分後 (亀 banzai) の 4 枚。
- `GFX STATS` で、**放置中の PicoRabbit 由来の present が最大 1/s**
  (亀の歩き交互のみ) で、帯の塗り直しが無いこと。数字を report に書く。
- Up でジャンプ → 着地後に 1000ms 周期へ戻る (ログか GFX STATS の
  presents/s で見る)。
- Ctrl+Tab 退避中のデスクトップのスクリーンショットにスプライトが無い。
- ログに E / Exception が 0。BMP 8 枚の読み込みログが出る。

## 受け入れ条件 (P0 + P1)

- plan.md の 1-5 が閉じている (4 は P1 で閉じる)。
- 8 コマの BMP が `flash/usr/share/picorabbit/` にあり、sprite_editor で
  開いて 1 画素直して保存し直せる (report に実施の記録)。
- 上記 2 つの検収を通り、report/p0.md・report/p1.md に画像と数字がある。
- `.env` の FMRB_HW_TARGET が作業前の値に戻っている。
