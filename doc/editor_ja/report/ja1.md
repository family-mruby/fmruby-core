# JA1 完了報告: メニュー i18n (J1) + 日本語表示 2 セルモデル (J2)

対象指示書: `doc/editor_ja/instruction_ja1.md`。J3 (折り返し) と J4 (かな入力)
は範囲外。検証は Linux sim (FMRB_HW_TARGET=TAB5、426x240)、標準構成
(editor=spinel) と互換構成 (editor=mruby) の両方。S3/P4 ビルド通過。

| タスク | 内容 | コミット |
|---|---|---|
| T1 (J1) | メニューの i18n 化 | `0692956` |
| T2 前段 | gfx の既存バグ 3 件 | `ff51e9d` |
| T2 本体 | 2 セルモデル (ec_render_width + 編集領域) | 本コミット |

---

## T1: メニューの i18n 化

### i18n キー一覧

`main/prebuild_scripts/default_app/editor/i18n.rb` に en/ja を登録。
`FmrbI18n` の `STRINGS` は **VM ごと**なので、デスクトップの登録は
エディタからは見えない。同じ語 (save 等) も改めて登録している。

| 分類 | キー |
|---|---|
| メニューバー | `m_file` `m_edit` `m_search` `m_run` `m_hilight` `m_debug` `m_full` |
| File | `open` `save` `save_as` `template` `exit` |
| Edit | `cut` `copy` `paste` `select_all` |
| 検索 | `find` `find_keys` `not_found` |
| 終了確認 | `unsaved` `save_before_exit` `q_yes` `q_no` `q_cancel` |
| ステータス行 | `st_new` `st_ln` `st_col` `st_hl_off` |
| ステータスバッジ | `b_saved` `b_save_failed` `b_load_failed` `b_too_large` `b_doc_full` `b_empty` `b_inserted` `b_no_templates` `b_running` `b_run_failed` `b_run_path` `b_run_pid` |

アクセラレータ文字は表に入れていない。スキャンコード表
(`MENU_*_HOTKEYS`) が正で、言語によらず同じ。

### レイアウト

固定文字数配置をやめ、`FmrbI18n.text_width` の実測幅で累積配置する。
位置と幅を `@menu_ids` / `@menu_xs` / `@menu_ws` の並行配列に残し、
クリック判定はそれを読む (レイアウト規則が二重にならない)。
ドロップダウン幅も最長項目から算出 (`dropdown_width`)。

**アクセラレータ表示は「File(F)」「ファイル(F)」形式**。
従来の「先頭 1 文字だけ色を変える」は日本語ラベルでは成立しない
(「ファイル」は F で始まらない)。

括弧つきキーは 1 項目あたり 3 文字 (18px) 増える。**240px の窓には
入らない**ので、入らないときは**バー全体で括弧を落とす**
(項目を落とすより良い)。全画面 (426px) では表示される。キー自体は
どちらでも効く。

### Spinel エディタで動かすために必要だった 2 件 (指示書に無い)

1. **`FmrbI18n` が Spinel エディタのソース構成に入っていなかった**。
   `tool/spinel/gen_app_combined.rb` の editor libs に `fmrb-i18n.rb` と
   エディタの表を追加。mruby 側は `default_app/editor/*.rb` の glob で
   自動的に入る。
2. **Spinel の `FmrbConst::LANGUAGE` は生成時固定の `"en"`**
   (`tool/spinel/gen_const_rb.rb`)。設定ダイアログで変えた言語が
   永久に届かない。`FmrbApp.language` を追加し (mruby=定数、
   Spinel=FFI `fmrb_spx_app_language` の実行時読み)、`FmrbI18n.lang` は
   そちらを見るようにした。

### Spinel の落とし穴 (再発しうる)

`FmrbI18n.add("en" => {...}, "ja" => {...})` と**波括弧なしで書くと、
Spinel はキーワード引数と解釈してシンボルキーの Hash を渡す**。
生成 C の `sp_FmrbI18n_s_add(sp_SymPolyHash *)` で判る。結果、
`STRINGS["en"]` が引けず全キーが `key.to_s` にフォールバックして
**画面に `m_file` `st_new` がそのまま出た**。明示的な `add({...})` で解決。

**`main/prebuild_scripts/kernel/system_desktop/i18n.rb` は同じ書き方**なので、
デスクトップを Spinel 化するときは同じ手当てが要る。

---

## T2 前段: gfx の既存バグ 3 件

### 1. FONT_METRICS の幅誤り → 半角/全角/半角カナの 3 値に

`char_w` (全角) に加えて `half_w` (ASCII) と **`kana_w` (半角カナ)** を持たせた。
2 値では足りない理由は下記の実測による。metrics は **mruby 側
(`fmrb-gfx.rb`) と Spinel 側 (`fmrb_app_base_spinel.rb`) の 2 箇所**にあり、
両方直した。

### 2. `FmrbI18n.text_width` の半角カナ

U+FF61-FF9F を 8px 扱いしていたのを 4px に (misaki の実測どおり)。
`truncate_to` も同じ判定を使う。

### 3. `draw_text` の切り捨て

`cmd_copy_bytes` が UTF-8 の途中で切っていた (末尾が化ける) のを、
**継続バイトを遡って文字境界で切る**ように修正。

### 指示書の前提が誤っていた点: 上限 128→256 はできない

指示書は「リンク層は 255 まで許容するのでプロトコル変更は不要」として
`FMRB_GFX_MAX_TEXT_LEN` の 256 化を指示していた。リンク層は確かに 255 まで
通るが、**実際に上限を決めているのはメッセージのペイロード**:

- gfx コマンドは `gfx_cmd_t` 丸ごと `fmrb_msg_t.data` に入って host task へ渡る
- `FMRB_MAX_MSG_PAYLOAD_SIZE = 176` は
  コメントどおり「gfx_cmd_t text(~148B) + sync ptr(8B)」に合わせた値

256 にするとビルドが落ちる (実際に落ちた: `__builtin___memcpy_chk`
out of bounds)。**128 のまま据え置き、両者の関係を
`_Static_assert(sizeof(gfx_cmd_t) <= FMRB_MAX_MSG_PAYLOAD_SIZE)` で縛った**
(ユーザ判断 2026-08-11: 分割で対応、メッセージ枠は太らせない)。
長い行は呼び出し側で分割する — エディタは色区間ごとに分割する必要が
元からあるので、そこにバイト上限を足すだけで済む。

---

## T2 本体: 2 セルモデル

### 採用したセル幅の範囲表 (`cp_cells`、editor_core.c)

| コードポイント | セル |
|---|---|
| < U+1100 | 1 |
| U+1100-115F (ハングル字母) | 2 |
| U+2E80-A4CF (CJK 部首〜彝) | 2 |
| U+AC00-D7A3 (ハングル音節) | 2 |
| U+F900-FAFF (CJK 互換) | 2 |
| U+FF00-FF60 (全角形) | 2 |
| **U+FF61-FF9F (半角カナ)** | **2** ← 指示書は 1 |
| U+FFE0-FFE6 (全角記号) | 2 |
| その他 | 1 |

### 半角カナが 2 セルな理由 (実測)

指示書と plan.md は半角カナを 1 セル (efont 6px) としていたが、**実際には
efontJA_12 に半角カナのグリフが無く、全角の箱 (12px) で描かれる**。
1 セルとしてモデル化すると、フォントの送り幅と食い違って
**同じ draw_text 内の後続文字がずれ、カーソルと選択が合わなくなる**
(実際に KANA 行の後半が消える症状が出た)。

測定ツールを `flash/app/test/ja_width.app.rb` として追加した。
`"ｱｲｳｴｵ|"` を各フォントで描いてインクの範囲を測った結果:

| フォント | `ABCDE\|` | `あいうえお\|` | `ｱｲｳｴｵ\|` | 半角カナ |
|---|---|---|---|---|
| mixed (Font0+misaki_8) | 4..36 | 4..46 | 4..26 | **4px** (半角) |
| :ja 12 (efontJA_12) | 5..37 | 5..67 | 5..67 | **12px** (全角) |

つまり **misaki は半角カナを持ち 4px、efont は持たず 12px**。
`FmrbI18n.text_width` (misaki 経路) の 4px は正しく、
編集領域 (efont 経路) は 2 セルが正しい。FONT_METRICS の `kana_w` が
この差を持つ。

### 二重座標系の分離

- `@cx` / EditorCore は**文字インデックスのまま** (無改修)。
- エディタだけが `EditorCore.render_width` (1 文字 1 バイトのセル幅マップ)
  の接頭和で cell ↔ char を変換する。`visible_chars` / `cell_offset` が
  その 2 関数。
- 対象: カーソル位置と幅 (全角上で 2 セル)、選択矩形、水平スクロール。
- **マウスクリック → 文字位置は対象外**: 現状のエディタは編集領域の
  クリックでカーソルを移動しない (実装が無い)。J3/J4 で入れるなら
  `cell_offset` の逆走査で足りる。

### 水平スクロールの端の扱い

`@scroll_x` は「左端の**文字**インデックス」のまま (EditorCore が
文字で切るため)。右端に全角文字が半分だけ残る場合は**描かない**
(`visible_chars` が全角を跨がない)。結果、右端に最大 1 セルの空きが出る
ことがある。空白で埋めるより、文字が半分描かれないほうが読みやすいと
判断した。

カーソルが右端に収まるかの判定はセルで行う (`cursor_cell_overflow?`)。
文字数がセル数以上なら確実に溢れるので、その場合は幅マップを引かずに
真を返す (`EC_WIDTH_MAX_COLS` を超える要求を出さないため)。

### draw_text の分割の実装形

`draw_row_text` が行のバイト列を 1 回走査し、**ハイライト分類が変わる /
選択の内外が変わる / 次の文字で `DRAW_TEXT_MAX_BYTES` (120) を超える**
のいずれかで draw_text を 1 本発行する。文字インデックス (幅マップと
ハイライトマップの添字) とバイトオフセット (byteslice 用) を別々に
持って進むのがポイント。ASCII 行では分割は起きない。

### 混在レイアウトの整理

- 編集領域とカーソル: `set_font(:ja, 12)`、`CELL_W=6` / `LINE_H=12`
- メニューバー・ステータス行・ダイアログ: 従来の 6x8 (`CHAR_W`/`CHAR_H`)
- 切替は `begin_edit_font` / `end_edit_font` の 2 メソッドに集約し、
  `draw_edit_area` と `redraw_dirty` の行描画をそれで挟む。set_font 自体が
  キューに載るコマンドなので、順序で成立する。
- 行数は `@edit_rows = @edit_height / LINE_H`。**全画面 426x240 で 27 行 →
  18 行** (plan.md 3.1 の想定どおり)。列数は 6px セルのままなので不変。

---

## 確認したこと (sim)

| 項目 | 結果 | 画面 |
|---|---|---|
| en / ja でメニューバー・ドロップダウン・ダイアログが切り替わる | OK | `en3.png` `ja2.png` `ja3.png` `ja45.png` `ja6.png` |
| ja でメニューのクリック位置が正しい | OK (へんしゅう/さがすが正しく開く) | `ja45.png` |
| 全画面ではアクセラレータ「(F)」が出る | OK | `ja7.png` `ja9.png` |
| 日本語 (漢字/かな/半角カナ/ASCII 混在) の表示 | OK | `jt5.png` |
| カーソルが全角上で 2 セル幅 | OK | `z5.png` |
| 選択矩形が全角に追随 | OK | `z6.png` |
| コピー/ペースト (日本語) | OK | `z7.png` |
| 保存 → 再読込 → 再保存で md5 不変 | OK (`58ac74e2...`) | (ログ) |
| 長い日本語行が行末まで描かれる | OK (ink が x=419 まで、画面右端) | `ln1.png` |
| 横スクロールで桁がずれない | OK | `hs1.png` |
| 英語ファイルの表示・操作に退行なし | OK | `en_edit.png` |
| デスクトップ/ランチャー表示に退行なし | OK | `desk2.png` |
| 互換構成 (mruby エディタ) で同じ表示 | OK | `mj9.png` |

### edit_lat (この コミットのビルドで測定、全画面 426x240、rows=18)

| バッファ | 結果 |
|---|---|
| 英語 (`/home/hello.rb`) | `n=100 mean=1130us max=4369us p99<=5ms over25ms=0 draw_mean=1146us draw_max=4348us hl=1` |
| 日本語 (`/home/ja_sample.rb`) | `n=100 mean=6870us max=560736us p99<=15ms over25ms=1 draw_mean=1242us draw_max=4506us hl=1` |

日本語側の `max=560ms` / `over25ms=1` は**ファイル読み込み直後の初回
サンプル 1 件**で、ヒストグラムも `98,0,1,...,1` と 2 件だけ外れている。
定常の描画コスト (`draw_mean`) は 1.15ms 対 1.24ms で、全角の分だけ
draw_text が増えても実質差が無い。P1 の目標 (p99 < 33ms、25ms 超ゼロ) は
英語で満たしている。

**注意**: 過去の report/p1.md の数値とは**別ビルド・別解像度**なので
直接比較はしていない (同一コミット同士でのみ比較する方針)。

### できなかった確認

- **日本語クエリでの検索**: かな入力が無い (J4) ので、日本語を打ち込む
  手段が sim にも実機にも無い。ASCII クエリでの検索が日本語混在行でも
  正しい桁を指すことは確認済み。J4 完了後に再確認する。
- **Alt ホットキー**: sim では Alt が死んでいる (ルート CLAUDE.md)。
  スキャンコードのホットキーは変更していないので影響は無いはずだが、
  実機確認待ち。

---

## ユーザ確認待ち

- 実機 (S3 / P4) での日本語表示。特に **Retro (320x240) の 18 行**が
  実用になるか (窓モードだと更に減る)。
- efont の半角カナが箱になる件を許容するか。許容しないなら
  (a) 半角カナをフォント側で用意する、(b) エディタで全角カナに正規化して
  表示する、のどちらか。**今回は「実際に描かれるとおりにモデル化する」**
  ことだけを行った。
- 日本語ラベルの語彙 (子供向けにひらがな中心にした: 「へんしゅう」
  「さがす」「じっこう」「いろ」「ぜんめん」)。

## J3 (折り返し) への引継ぎ

- `ec_render_width` が J3 の基盤としてそのまま使える。`ec_wrap_segments`
  は同じ幅マップの接頭和で書ける。
- **右端の半端セルの扱いは J3 で消える**: 折り返しなら「入らない全角は
  次の画面行へ」が自然な答えになる。今回の「描かない」は横スクロール
  時の暫定。
- 行の描画は既に `draw_row_text(x, y, text, widths, nchars, hl, ...)` と
  「1 画面行ぶんのスライス」を受け取る形なので、折り返しでは
  `col0`/`nchars` を区分ごとに変えて同じ関数を呼べば良い。
- `@edit_rows` は画面行数、`@scroll_y` は論理行番号のまま。J3 の
  アンカー (論理行, 区分番号) を入れるときはこの 2 つが変更点になる。
- `DRAW_TEXT_MAX_BYTES` (120) の分割は折り返し後も必要 (1 画面行が
  106 セル = 半角カナなら 318 バイト)。
