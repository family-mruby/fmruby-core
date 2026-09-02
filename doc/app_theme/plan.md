# 窓枠とアプリ配色をテーマに繋ぐ

> 状態: 完了 | 更新: 2026-09-02 | A・B・C + D 実装済。窓枠は 4 か所あり Python と Lua も繋いだ (report/guest_languages.md)。壁紙はテーマ追従 + パス指定 (report/wallpaper.md)

デスクトップの配色を変えても、**アプリの窓枠と中身が付いてこない**。
ユーザ報告 (2026-08-31)。Spinel アプリでも同じ。

## 何が起きているか (調査結果)

配線そのものは両エンジンとも生きている。足りないのは呼び出し側だけだった。

- `[theme]` は kernel が読んで `fmrb_theme_set()` に渡す
  (main/kernel/fmrb_kernel.c:243)。mruby VM には生成時に
  `FmrbConst::THEME_*` として入り、Spinel には
  `FmrbSpx.fmrb_spx_theme_color(i)` の FFI で入る (起動時に一度評価)。
- **窓枠だけ誰もテーマを見ていない**。描いているのはアプリ基底クラスの
  1 か所で、色が直値で書いてある:
  - mruby: `lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb` の `_build_frame_block`
  - Spinel: `main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb` の `_paint_frame`
    (Spinel は proc を保存できないので即時描画に書き換えた際、値ごと写された)

  値は `0xC5` (タイトルバー) と `0x60` (縁) — **classic の `menu_bg` /
  `border` そのもの**。実機の配色に合わせて書かれ、テーマ機構ができたときに
  繋ぎ忘れた形。同じ数字が 2 か所にある。
- **中身はアプリ次第**。`clear_user_area` の既定は `THEME_WINDOW_BG`、
  FmrbUI は 6 色すべてテーマ由来、デスクトップのダイアログ類もテーマ。
  一方で古い組み込みアプリは自前のパレットを持つ:

  | アプリ | 直値の色 | テーマ参照 |
  |---|---|---|
  | editor | 18 | 0 |
  | shell | 16 | 0 |
  | inspector | 12 | 0 |
  | logviewer / monitor | 4 | 6 |

## 方針

### A. 窓枠をテーマへ (全アプリに一度に効く)

| 部品 | 今 | これから |
|---|---|---|
| タイトルバー | `0xC5` | `THEME_MENU_BG` |
| タイトル文字・ハンバーガー | `WHITE` / `0xFB` | `THEME_TEXT_LIGHT` |
| 閉じるボタン | `0xFF` | `THEME_TEXT_LIGHT` |
| 窓の縁 | `0x60` | `THEME_BORDER` |

classic では見た目が変わらない (値が一致する) ことが、移し替えが正しいこと
の確認になる。**mruby と Spinel の 2 か所を必ず同時に**直す。

### B. 組み込みアプリの既定色をテーマから採る

対象は **editor と shell** (inspector は後回し)。**構文強調の色は対象外** —
あれは意味の色であって装飾ではなく、テーマに項目もない。

editor: 背景 → `THEME_WINDOW_BG`、本文 → `THEME_TEXT`、メニューバー →
`THEME_MENU_BG` / `THEME_TEXT_LIGHT`、選択 → `THEME_HIGHLIGHT`、
ステータス行 → `THEME_MENU_BG` / `THEME_TEXT_LIGHT`、gutter → `THEME_BUTTON`。
shell: 背景 → `THEME_WINDOW_BG`、文字 → `THEME_TEXT`。

### C. Editor と Shell はユーザが色を変えられる (テーマは既定値になる)

**上書きは `/home/colors.toml`**。理由は 3 つ:

- ユーザの持ち物だから `/home` (配布物と混ぜない。doc/wasm/storage_persistence.md)
- ブラウザ版では `/home` だけがリロードを越えて残る。`/etc` は残らない
- `/home` は書き出しの tar に入るので、設定ごと持ち運べる

```toml
# 書かなかった項目はテーマの値のまま
[editor]
bg = 0x24
text = 0xFF
[shell]
bg = 0x00
text = 0x1C
```

- 読むのは**アプリ起動時に 1 度**。定数の初期値として解決するので、
  各描画箇所は今のまま (`BG_COLOR` などの参照を書き換えない)。
- 解析は `SvcConf.parse` と同じ「小さな toml もどきを Ruby で読む」やり方
  (C の fmrb_toml に Ruby 束縛は無く、10 行のために作る価値はない)。
- 変える口はアプリごとの流儀に合わせる:
  - editor: View メニューに Colors、その場で反映して保存
  - shell: `color` コマンド (`color bg 0x24` / `color list` / `color reset`)
- **再起動は要らない** (システムテーマの変更は再起動が要るが、これは要らない)。
- 「テーマに戻す」は該当項目を消すこと。

## 実装した結果 (2026-08-31)

A と B、そして C の**土台 (`/home/colors.toml` を読む所)** まで入れた。
残っているのは C の変更 UI だけ。

- 窓枠: 両エンジンの 1 か所ずつをテーマ参照に。
- editor: 紙・文字・メニュー・ステータス・選択・カーソル・ドロップダウン・
  終了確認をテーマ由来に。**構文強調は 2 系統にした** — 意味の色なので
  テーマからは採らないが、暗い紙の上では従来の配色 (黒や濃紺) が見えないため、
  紙の明るさ (RGB332 の重みつき輝度、閾値 45) で明るい紙用と暗い紙用を選ぶ。
  スロット 0 (ふつうの文字) だけは常に本文色に従う。
- shell: 紙と文字をテーマ由来に。
- `FmrbColors.section(name)` を mruby の gem と Spinel の基底の両方へ。
  Spinel で確実に通る書き方だけを使う (`to_i(16)` と `downcase` は使わず、
  16 進の桁を手で引く。`File.open` + `rescue` は編集器で実績のある形)。

### 実測 (ブラウザ版、web_* ツールで両テーマを往復)

| 見たもの | cyberpunk | classic |
|---|---|---|
| 窓枠 (Spinel = editor) | 濃紺のバー・青緑の縁 | 従来どおり暗赤のバー |
| 窓枠 (mruby = monitor / shell) | 同上 | 同上 |
| editor の紙 | 黒に緑の文字、暗い紙用の構文色 | 白に黒、明るい紙用の構文色 |
| `/home/colors.toml` の上書き | editor だけ深緑の紙・黄の文字に変わり、デスクトップと枠はテーマのまま | 同左 |
| shell の上書き | 黒地に緑 (mruby 側の経路も通った) | 同左 |

**classic でも editor の見た目は少し変わる**: 紙が「ほぼ白のピンク」から
テーマの白へ、メニューバーが紫から暗赤へ。B の狙いどおりだが、元の配色が
好きなら `/home/colors.toml` に書けば戻せる。

### 踏んだ罠

- **`draw_round_rect` の引数を 1 つ落とした**まま置換して、デスクトップが
  `ArgumentError (given 5, expected 6)` で即死した。node ビルド
  (`rake wasm:run`) のログで 1 分で分かる。機械的な置換のあとは引数の数を
  数えること。
- **`rake wasm:web` は `spinel:gen` を呼ばない**。Spinel 側の .rb を直したら
  先に `rake spinel:gen`、そのあと lib/ を触っていれば `rake wasm:mruby`、
  最後に `rake wasm:web`。
- ランチャーからの起動は**ダブルクリック**、ファイル選択は
  **クリックしてから enter**。どちらも 1 回のクリックでは選ぶだけ。

## C の実装 (2026-08-31、ユーザ判断で全項目 + 名前と数値の両方)

### 色名

**web の色名をこの機械の 256 色に落とした表**を持つ。103 の名前が 78 の
異なる色に潰れるので、表は 2 つに分けた: `PALETTE` が色ごとに 1 つの名前
(選ぶ側が見せるのはこれ)、`ALIAS` が同じ色に着地する別の綴り (人が知って
いる名前が通るように)。

**性能**: 文字列リテラル 2 本 (名前 724 + 199 バイト、値 78 + 23 バイト)
で約 1 KB、オブジェクトは 0 個。ハッシュは作らない。引くのは colors.toml を
読むときと選択 UI を開いたときだけで、**描画経路には一切乗らない**。
`0x1F` のような数値も従来どおり書ける。

### editor: 31 項目

紙・文字・カーソル・選択・gutter・メニュー 4 色・ステータス 4 色・
ドロップダウン 4 色・ダイアログ 4 色・問題行 2 色、そして**構文強調 8 色**。
`EDITOR_COLOR_KEYS` と `EDITOR_COLOR_VALUES` が対の並びで、Colors ダイアログ
(View メニュー、または Alt+V → C) がこれを歩く。上下で選び、Enter で入力、
`r` でその項目を既定へ、`x` で editor の指定を全部消す。

**editor は次回起動から効く**。色は定数として起動時に解決されるため。
ダイアログはそう表示する (取り繕わない)。

### shell: `color` コマンド

`color` / `color bg <色>` / `color text <色>` / `color names` / `color reset`。
**その場で反映される** — shell の 2 色は定数ではなく変数だから。
FmrbUI に `bg` の setter を足して、widget の背景も一緒に動かしている。

### 書き戻し

`FmrbColors.set` は**行単位の書き換え**で、コメント・他のアプリの節・
知らないキーをそのまま残す (システムの Config ダイアログが
system_conf.toml に対してやっているのと同じ規則)。値は名前があれば名前で
書くので、手で開いても読める。

## D. 残っていた配色をテーマへ、エディタのバーは一段違う色に (ユーザ指摘)

### エディタのメニューがタイトルバーと同じ色で見分けられない

窓枠が `THEME_MENU_BG` になったこと (A) で、その真下にあるエディタの
メニュー行と**同じ色の帯**になってしまった。**新しいテーマ項目は足さず、
`FmrbColors.shade` で一段ずらす**ことにした — 項目を増やすと 7 つの
system_conf と 3 つのプリセットと Spinel の生成器と wasm のページ設定に
同じ色を配って回ることになり、どのテーマでも自動で成立する方が良い。

規則は「赤緑を 7 段中 2 段、青を 3 段中 1 段下げる」。ただし 3 つの結果は
拒んで逆向きに振る:

- `0x01` — デスクトップの透明キー ([[テーマ色に 0x01 を使わない]])。黒にする
- **元の色と同じ** — もう下がれない色 (黒など)
- **`avoid` に渡した色** — 呼び出し側のもう一方の隣人。cyberpunk では
  menu_bg を暗くすると**ちょうど紙の色 (黒)** になり、今度は紙に溶けた

エディタは `shade(THEME_MENU_BG, BG_COLOR)` を使う。結果:

| テーマ | タイトルバー | エディタのバー |
|---|---|---|
| classic / light | `0xC5` 深紅 | `0x80` えんじ |
| cyberpunk | `0x22` 濃紺 | `0x6B` 明るい青紫 (黒い紙と区別) |
| dark | `0x49` | `0x00` 黒 |

もちろん `[editor] menu_bg` で好きな色にできる。

### inspector とデスクトップのダイアログ

- inspector: 紙・文字・muted・見出し・選択をテーマから
  (見出しは `THEME_DIR_COLOR` = テーマの強調インク、選択行の文字は
  `THEME_TEXT_LIGHT`)。**緑=良好 / 赤=異常 / 青=生バイトは残す** — 意味の色。
- デスクトップ側: タイトル帯の上に直接書かれていた `FmrbGfx::WHITE` が
  14 か所あり、すべて `THEME_TEXT_LIGHT` に。ランチャーの選択タイルも
  `THEME_HIGHLIGHT` に。
- **error_dialog だけはそのまま**。何かが壊れたときに出るもので、壊れている
  ものの候補にはテーマ自身も入る (紙と文字が同じ色のテーマでは、その事実を
  説明する文章ごと消える)。赤と黒で固定しておく理由をコードにも書いた。

## 途中で見つかった別の不具合 2 件

どちらもこの作業とは無関係の既存の穴で、直してある。

1. **Spinel 版エディタのメニュー文字ショートカットが全滅していた**。
   `hk.index(scancode)` が「分岐 1 つ + 既定値 0」の C に落ちており、
   受け手の型が合わないと黙って 0 を返す。結果、開いたメニューでどの文字を
   押しても先頭の項目が動いていた (Alt+F → X で「開く」)。明示的なループに
   置き換えた。詳細と再現の勘所は
   doc/spinel_aot/embedded_constraints.md 9.7。
2. **wasm ビルドは mruby アプリのバイトコードを再生成しない**。
   `main/prebuild_scripts/**/mrb/*.c` は ESP-IDF / Linux の CMake ビルドの
   生成物で、wasm はそれを拾うだけ。shell を直しても古いままリンクされ、
   `color` コマンドが「Unknown command」になった。`rake wasm:mrb` を足し、
   `wasm:core` と `wasm:web` から呼ぶようにした (ソースより古い .c だけを
   作り直す)。

## スコープ外

- 構文強調の色そのもの (意味の色なのでテーマからは採らない。ただし紙が暗い
  ときに読めるよう、明暗 2 系統の選択だけは入れた)。
- inspector とデモ・ゲーム類の配色 (自分の絵を持つものはそのままでよい)。
- テーマ項目そのものを増やすこと。今の 9 色で足りる範囲でやる。

## 受け入れ条件

- classic (実機の既定) で**窓枠**の見た目が変わらない (値が一致するため)。
  editor は B の狙いどおり紙と menu が少し変わる — 元に戻したければ
  `/home/colors.toml`。
- cyberpunk (web) と dark で、窓枠・editor・shell がテーマに従う。
- **標準構成 (Spinel カーネル + Spinel エディタ) と全 mruby の両方**で確認する
  (窓枠の実装が 2 か所にあるため。doc/engine_policy 相当の決まり)。
- `/home/colors.toml` を置くと editor と shell だけがその色になり、消すと
  テーマに戻る。リロード・再起動を挟んでも残る。

## 未確定事項

- 色見本から選ぶ UI (今は名前か数値を打つ)。名前の一覧は shell の
  `color names` で出るが、editor 側には無い。
- inspector とデスクトップ側のダイアログの配色 (今回は対象外)。
- `rake wasm:web` は `spinel:gen` を呼ばない (生成済みの gen/*.c を使う)。
  Spinel 側を直したら **先に `rake spinel:gen`** が要る。手順に組み込むか、
  wasm 側から呼ぶかは別途。
