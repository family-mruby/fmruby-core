# 実装指示書 P6: 本文を efont で描く (機種ごとのフォントの持ち物)

対象: 実装担当セッション。作業リポジトリ: fmruby-core **と fmruby-graphics-audio**
(T1 は両方)。先に読むもの: plan.md の P6、instruction_p0_p1.md の「進め方」、
report/p4.md (EXPORT_FRAME で両リポジトリの写しを同期した前例)。
報告は report/p6.md。

## 背景 (確かめてある事実)

- Modern (Tab5) の本文が粗いのは、8px の字 (Font0 / misaki) を `text_size: 2`
  で整数倍に引き伸ばし、さらに DSI で 3 倍にしているから (画面上のドットが
  6px 角)。**字を大きなフォントで等倍に描けば直る**。
- Retro は 320x240 をそのまま NTSC に出すので粗さの問題は無い。WROVER の
  app 区画は 2000K で余裕が少なく、フォントを足せばそのまま焼き時間になる。
- display 側は LovyanGFX で、**efontJA は 10/12/14/16/24 の各サイズに
  `_b` (太字) `_i` (斜体) `_bi` が同梱済み** (ソースにある。リンクは参照した
  ものだけ)。今使っているのは Font0 / misaki_8 / efontJA_12 の 3 つ。
- `fmrb-gfx.rb` の `text_width` / `font_height` は **Ruby 側の幅表
  (`FONT_METRICS`) で計算し、display に往復しない**。表に無いフォントは
  幅が合わなくなるので、フォントを足すときは表も足す。
- GFX の `SET_FONT` は `family` (0 = Font0、1 = efontJA) と `size` の 2 バイト
  (`fmrb_link_protocol.h`、両リポジトリに写し)。

## 決定事項 (ユーザと合意)

| 用途 | Modern | Retro |
|---|---|---|
| 本文 | **efont 12** (既存) | efont 12 (既存) |
| 太字 | **efont 12 bold** (新規) | 余裕があれば同じ。無ければ従来の 2 回描き |
| 見出し | **efont 16** (新規) | 余裕があれば同じ。無ければ従来の misaki 8 x 2 |
| ルビ | misaki 8 (既存) | 同じ |
| コード | efont 12 (半角 6px、等幅) | 同じ |

Gothic / Mincho / 24px は**入れない**。フォントは「希望」であり、無い機種では
Ruby 側の表に従って近いものに落ちる。同じ .md が両機種でそのまま出ること。

## T1: GFX の太字と 16px (両リポジトリ)

- `fmrb_link_protocol.h` の family に `FMRB_LINK_GFX_FONT_FAMILY_JA_BOLD = 2`
  を足す (両写しに同じ値)。size は従来どおり px。
- display_p4 (Tab5, core 側) の `SET_FONT`: family 1 は size 12 → efontJA_12、
  **16 → efontJA_16**; family 2 は 12 → efontJA_12_b (16 の太字は入れない:
  見出しは帯の色で十分立つ)。知らない組み合わせは**近いものに落として警告
  1 行** (12 以外の JA は 12 へ、bold が無ければ通常へ)。
- graphics-audio の `SET_FONT` も同じ対応にするが、**WROVER に入れるかは
  実測で決める**: `rake build:esp32` (WROVER) で efontJA_12_b と efontJA_16 を
  参照した bin の増分と app 区画 (2000K) の残りを report に書く。残りが
  **200KB 未満になるなら入れない** (その場合は fall back。sim の Linux build
  は LovyanGFX が全部持っているので、sim では常に描ける)。
- `fmrb-gfx.rb`:
  - `set_font(:ja_bold, 12)` を受ける。`FONT_METRICS` に `[:ja_bold, 12]`
    (= `[:ja, 12]` と同じ幅) と `[:ja, 16]` (`char_w: 16, half_w: 8,
    kana_w: 16, line_h: 16`) を足す。半角カナ幅は sim の
    `flash/app/test/ja_width.app.rb` で実測して書く。
  - **機種の持ち物表** `FONT_AVAILABLE` を置き、`set_font` は頼まれた
    (family, size) が無ければ表の規則で置き換え、**実際に選んだ
    `[family, size]` を返す**。`@current_font` は置き換え後の値になるので、
    `text_width` / `font_height` は描かれるものの幅を返す。規則:
    `[:ja, 16]` が無ければ `[:ja, 12]`、`[:ja_bold, n]` が無ければ `[:ja, n]`。
  - 表の引き方: `FmrbConst::HW_FAMILY` (`"modern"` / `"retro"`) を**新設**する。
    esp32 は `FMRB_HW_MODERN` の有無、Linux は Rakefile が選ぶターゲットに
    応じて `-DFMRB_HW_FAMILY_MODERN` を linux ビルドにだけ足して判定する
    (`FMRB_HW_MODERN` 自体を linux で定義すると P4 専用コードを巻き込むので
    **別の define** にする。`fmrb_hw_defines.cmake` の linux 分岐)。
    sim は実機と同じ持ち物表に従う (Retro 向け sim では 16 が出ない = 実機と
    同じ。WROVER に入れると決めたときは表も Retro 側に足す)。
- `sig/fmrb_gfx.rbs` の `set_font` に family の追加と戻り値を書く
  (sig を直したら `rake clean`)。

## T2: PicoRabbit の renderer

- frontmatter に `font:` (`efont` | `misaki`、既定 **efont**) と `font_size:`
  (本文 px、既定 **12**、見出しはその次のサイズ = 16) を足す。従来の
  `text_size` は misaki のときだけ意味を持つ (efont では無視して警告)。
  既存の demo.md / demo_ja.md / intro_ja.md から `text_size: 2` を外し、
  既定の efont 12 で出るようにする。
- 描画: `set_font(:ja, 12)` + `draw_text` (efont は ASCII も持つので
  `draw_text_mixed` は不要)。見出しは `set_font(:ja, 16)`。**太字の区間は
  `set_font(:ja_bold, 12)`** で描き、`set_font` の戻り値が bold でなければ
  従来の 1px ずらし 2 回描きに落とす。ルビは misaki 8 のまま
  (`set_font(:ja, 8)`)。inline code とコードブロックも efont 12。
- 行高・インデント・引用バー・コード枠の寸法は `font_height` と
  `text_width` から取り、size 1 固定の定数 (`CHAR_W` 等) に依存しない
  (P2 の `@ts` 倍の仕組みを「フォントの高さ」基準に置き換える)。
- `misaki` を選んだデッキは P2 までの挙動 (Font0 + misaki hybrid、
  `text_size` 倍) をそのまま通す。
- 索引モード・メニュー・フッタ (ページ番号 / 時計) は今のままでよい
  (size 1 の Font0。変えるなら別段)。

## 検収

### sim (Modern 向け: FMRB_HW_TARGET=TAB5、426x240)

- intro_ja.md 全 9 枚のスクリーンショット。本文 12px、見出し 16px、太字が
  efont の太字で出ている (1px ずらしの二重線でない) ことを**文字 1 つを
  6 倍に拡大した画像**で示す。
- 折り返しが右端で起き、右余白の列が地の色だけ (pngscan)。
- ルビが base の上 8px (P2 と同じ検査)。
- `font: misaki` + `text_size: 2` にした一時 .md が P2 と同じ絵 (pngdiff 0)。
- 放置中の present 1/s、E/Exception 0。

### sim (Retro 向け: FMRB_HW_TARGET=NARYAv3、320x240)

- 同じ intro_ja.md を開き、**落ちずに**従来どおりの見た目 (16 が無ければ
  misaki x 2 の見出し、bold が無ければ 2 回描き) になる。WROVER に入れると
  決めた場合はその見た目。

### Tab5

- intro_ja.md 全 9 枚 (実機の写真)。**bin の増分と flash の所要時間**
  (460800bps、変更前後) を report に書く。
- Export した JPEG を `fmrb_rd_fs.rb pull` で取り、実機画面と許容差 48 で
  一致 (P4 の検収と同じ)。

### Retro (S3 + WROVER)

- WROVER に入れる判断をしたときだけ: WROVER を焼き直し、Retro 実機で
  intro_ja.md が開くことをユーザ確認 (remote desktop が無いので写真は
  ユーザ)。入れない判断なら Retro は sim の確認で閉じる。

## 受け入れ条件

- 上の検収が report/p6.md に揃う。
- コミット: (1) graphics-audio の SET_FONT、(2) core の protocol + display_p4 +
  fmrb-gfx.rb + sig + FmrbConst::HW_FAMILY、(3) renderer + サンプル 3 本の
  frontmatter。(1)(2) は同じ family 値を足したことをメッセージに。
  本書は (3) に含める。英語、ユーザ確認のうえ。
- `.env` が作業前の値に戻っている。

## やらないこと

- Gothic / Mincho / 24px 以上、斜体、プロポーショナル (`GothicP`) 、
  SD カードからの VLW フォント読み込み (幅の往復が要る。欲しくなったら別段)。
- 索引・メニュー・フッタのフォント変更。
