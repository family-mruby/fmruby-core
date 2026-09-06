# 実装指示書 P9: 背景画像と行ごとの文字サイズ

対象: parser / slide / renderer / アプリ / サンプル (fmruby-core のみ、GFX 側の
変更なし)。先に読むもの: plan.md の P9、instruction_p7.md (画像の流れ)、
report/p6.md (フォントの段)。報告は report/p9.md。

## 確かめてある事実

- 字形は efont **12 と 16 だけ** (`FONT_AVAILABLE`)。Tab5 の app 区画は
  残り 5% で新しい字形は足せない。表示側は LovyanGFX の `setTextSize` を
  フォントに関係なく掛ける (P4 `display_p4_task.cpp:1414`、graphics-audio
  `graphics_handler.cpp:1012`) ので、**24 = 12x2、32 = 16x2** は整数倍で出る
  (Modern では階段が見える。見出し用途なら許容)。8 は misaki。
- `text_width(str, :ja, px)` は現在の倍率が掛かるので、測るときは `set_ts(1)`
  にして呼び手が倍率を掛ける (既存の `wb` / `w1` の作法)。
- `draw_image(id, x:, y:, scale_x:, scale_y:)` は縦横別の倍率と 1 より大きい
  倍率を受ける (両表示側とも `drawPng(..., scale_x, scale_y)`)。
- `draw_str(x, y, str, color, bg = nil)` は bg 無しで透明描画になる。
  renderer で `@theme.bg` を bg に渡している呼び出しは 12 か所。
- 画像は描画側の資源で数に限りがある。sync_file は同内容なら転送しない。

## 仕様

### 文字サイズ (行指示)

```
大きく見せたい行
{:.large}
もっと大きく、中央に
{:.xlarge .center}
注記
{:.small}
```

- `{:` 〜 `}` の中を空白区切りの class として読む。`.center` `.right` は寄せ
  (従来どおり)、`.small` `.large` `.xlarge` は大きさ。**直前の描画要素 1 つ**に
  付く (寄せと同じ規則、Element#size)。text / bullet / numbered / blockquote と
  表紙の副題に効く。見出し・コード枠・画像には効かない。
- 段は **8 / 12 / 16 / 24 / 32** (24 = 12x2、32 = 16x2)。本文 (`font_size`、
  既定 12) を基準に small = 1 段下、large = 1 段上、xlarge = 2 段上、端で止める。
  misaki (`font: misaki`) では倍率 `text_size` に -1 / +1 / +2 (1〜3 で止める)。
- 行の高さは大きさに追従 (efont: px + px/4)。太字は 12 系 (12, 24) のときだけ
  太字字形、それ以外は従来の 1px ずらし 2 度描き。ルビは 8 のまま。
- 番号・箇条書きの印は本文と同じ大きさで描く (印の幅は `wb` で測る)。

### 影付き文字 (追加指示、2026-09-04)

```
Family mruby
{:.center .xlarge .shadow}   # この行だけ
---
shadow: true                 # frontmatter: 全行
---
```

- `draw_str` で、影のある行は同じ文字列を `theme.shadow` 色で
  (+mul, +mul) にずらして先に描き、その上に本来の色で描く。地色の箱は敷かない
  (箱が影を消す)。bg を自分で持つ描画 (見出しの帯、inline code) には影を
  付けない。
- テーマに `shadow` を足す (default / light は黒、dark は暗い灰)。

### 見出しの様式 (追加指示、2026-09-04)

```
---
heading: underline            # デッキ全体: band (従来の帯) / underline / plain
---
# 表題
{::heading band/}             # 枚ごとに変える
```

- `underline`: 字は本文の色、その下に帯の色 (`title_bg`) の線を **左端から少し
  空けて右端まで** (始点 `MARGIN_X + 2`、終点 `@w - MARGIN_X`)、太さ 2px
  (見出し 24px 以上は 3px)。`plain` は字だけ。どの様式も高さは `title_bar_h`
  で同じ (本文の始まりが動かない)。
- 帯でない見出しは地色の箱を敷かず、`shadow: true` なら影が付く (帯の字には
  付かない、の規則のまま)。
- **表紙は指定が無ければ帯を敷かない** (背景画像に載せる前提)。表紙にも
  `{::heading band/}` / `{::heading underline/}` は書ける。underline は表題の
  塊の下に同じ線。
- サンプルの demo.md / demo_ja.md は `heading: underline` にする。

### 縦中央 (追加指示、2026-09-04)

```
# 表題
{::valign center/}            # この枚の本文を、見出し帯と下の帯の間で縦に中央へ
---
valign: center                # frontmatter: 全枚。{::valign top/} で枚ごとに戻す
---
```

- 描く前に同じ規則で高さだけ足す `measure_content` (大きさ、溢れの打ち切りは
  描画と同じ。**wait は全部開いた状態で測る**ので、段階が進んでも塊は動かない)。
  余りの半分だけ下から始める。溢れる枚は上から。
- 画像は測るときに作って持ち (`@img_hold`)、描くときに使う (2 度復号しない)。
  動画は開いて大きさを読んで閉じる。`fmrb` ブロックは高さが描くまで分からない
  ので 0 と数える (その枚は上寄せのままにするのがよい)。

### 背景画像

```
---
background: images/bg.png        # デッキ全体
---
# 表紙
{::background /usr/share/backgrounds/bg_426x240.png/}   # この枚だけ
# 次
{::background none/}             # この枚は無地に戻す
```

- 相対パスは .md 基準 (`image_source`)。PNG のみ。
- **画面いっぱいに引き伸ばす** (`scale_x = @w / iw`、`scale_y = @h / ih`。
  426x240 の絵を用意すれば等倍)。
- 背景がある枚は、`@theme.bg` を敷く文字描画を**透明**にする (`draw_str` で
  `bg == @theme.bg` なら nil に落とす)。見出しの帯・コード枠・inline code・
  下の帯 (時計の消し込みに要る) は塗りを残す。索引・メニューには背景を出さない。
- 画像は `sync_file` → `create_image` を**デッキ内で 1 回**だけ行い
  (path → [id, w, h] の表)、表は 3 枚まで (溢れたら古いものを delete)。
  デッキの入れ替えと終了で全部 delete (`release_images`)。
- 読めない / PNG でないときは無地のまま進む (例外で落とさない)。ログに 1 行。
- 描くたび (段階送りも) に draw_image 1 回。所要をログに出す (初回は sync 込み)。

## サンプル

demo.md / demo_ja.md に「大きさ」の枚 (large / xlarge center / small) を 1 枚、
最後の枚に `{::background /usr/share/backgrounds/bg_426x240.png/}`。同梱済みの
4KB の絵を使い、新しい画像は足さない。

## 検収

- sim (Modern 426x240、Retro 320x240): 大きさの枚で 3 段が出る、背景の枚で
  絵が画面全体に伸びて文字が透けて載る、Retro 向けでも同じ .md が落ちない、
  索引に背景が出ない、放置中 present 1/s、E/Exception 0。
- Tab5: 同じ 2 枚の実機画面、背景の draw の所要。
- 受け入れ: report/p9.md に上が揃う。コミット 2 本 (実装 + サンプル)、英語、
  ユーザ確認のうえ。

## やらないこと

等比で余白を付ける置き方 (引き伸ばしのみ)、JPEG 背景、行内の部分的な
大きさ変更、見出しの大きさ指定、背景の上の窓枠の透過。
