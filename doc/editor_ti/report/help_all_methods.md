# 報告: 全メソッドの長文ヘルプ

> 状態: 完了 | 更新: 2026-09-02 | 577/577 メソッドに長文。
> 索引 577 件・39 クラス・78 ファイル・436KB

指示書 instruction_help.md の残り (FmrbAudio 以降) を書き切った回の記録。
前半 (FmrbGfx・FmrbApp) は help_fmrb_gfx.md と help_fmrb_app.md。

## 書いた範囲

| 区分 | クラス | メソッド |
|---|---|---|
| 端末の API | FmrbGfx 52 / FmrbApp 73 / FmrbUI 54 / FmrbMidi + MIDI::Device 59 / P5 42 / FmrbAudio 18 / FmrbSprite + GfxBlock 17 / FmrbTile 27 / FmrbI18n + File 11 | 353 |
| 組み込み | String 55 / Array 36 / Integer 28 / Object 25 / Float 19 / Hash 18 / NilClass 11 / Range 9 / Kernel 7 / Symbol 5 / Enumerable 4 / Untyped 4 / Proc 2 / Class 1 | 224 |

**引数のとおりに動くものにも書いた**。指示書のとおり二層で、単純なものは
散文を付けず**呼び出し例だけ**にしてある (「引数 x は x 座標です」は書かない)。
読み手が知りたいのは型ではなく**書き方**なので、`gfx.fill_rect(10, 10, 40,
20, FmrbGfx::RED)` の一行が要約より役に立つ。

組み込みには、この端末でしか当たらない罠を織り込んだ:

| 場所 | 書いたこと |
|---|---|
| `String#length` / `#bytesize` | 文字数とバイト数。日本語で食い違う |
| `String#setbyte` | `Array#pack` が無いので `"\x00" * n` に書き込む |
| `String#end_with?` | Regexp が無いので拡張子判定はこれ |
| `Array#[]=` | `a[i], a[j] = a[j], a[i]` は動かない |
| `Array#each` / `Integer#times` / `Range#each` | ブロック 1 回 0.4ms |
| `Object#sleep_ms` | `_spin` の外では戻らない。`Machine.delay_ms` を使う |
| `Object#relinquish` | 長い計算に挟む |
| `Object#is_a?` | `defined?` が無い |
| `Kernel#proc` | Spinel は外側のローカルを掴めない |
| `Float#==` | 小数を == で比べない |

## 直したこと

### sig に足りなかったもの

- `String#bytesize` / `String#setbyte` — **こちらを使えと案内している当の
  メソッドが型 db に無かった**。補完に出ず、書き方が分からない。
- `SmfPlayer#tempo_scale=` / `MmlPlayer#bpm=` — 読みだけ宣言してあった。
- `SmfPlayer#channel_usage` は Hash、`#tempo_scale` は Float。宣言が
  Array / Integer になっていた。

### 演算子のヘルプが 1 つも出ていなかった

`gen_help.rb` の `file_name_for` が `+` や `<<` を「ファイル名にできない」と
して**黙って捨てていた** (警告は出るが生成は続く)。組み込みはメソッドの半分が
演算子なので、String・Integer・Array のページが穴だらけになる。
`OPERATOR_WORDS` で綴りを与え、`op_add.md` のように置くようにした。
索引には `+` の行が入るが、エディタは行から**語**を拾うので `+` を引くことは
なく、実害はない。クラスのページを上から読むときに載っている、が目的。

### 英語のページが日本語のままだった

要約行 `# 日本語 <<en>> English` の**英語側が 283 件で欠けていた**
(FmrbApp 65・FmrbMidi 55・FmrbGfx 48・P5 42・FmrbTile 27・FmrbAudio 18・
FmrbSprite 17・FmrbMisc 11)。英語のページを開くと見出しの下が日本語で並ぶ。
全部埋めた。長文側で英語が無かった 6 件 (`play_wav`、`draw_rect`、
`fill_rect`、`set_font`、`SpriteInstance.new`、`FmrbUI#button`) も訳した。

**`<<en>>` は 1 ブロックに 1 つだけ**。`FmrbUI#button` は日本語 → 英語 →
日本語 → 英語と 2 組書かれていて、`pick_lang` が最初の印で切るため、後半の
日本語が英語のページに残っていた。日本語をまとめ、英語をまとめる形に直した。

## 踏んだ罠

### 要約行を消してしまう挿入

長文は `def` の直前に入れる。**要約行を含む文字列を目印にして「その前」に
入れると、要約が長文の最後尾に回る**。1 行目が空の `#` になり、型 db の
要約が空になる (エディタの補完に何も出ない)。fmrb_sprite.rbs の 15 件を
そうしてしまい、順序を戻して直した。以後は `def` の行だけを目印にしている。

検査は 1 行で書ける。**入れたあとに必ず回すこと**:

```
# 「空の # 行の直前がコメントでない」= 要約が無い
ruby -e 'Dir["sig/*.rbs"].each { |f| ... }'
```

同じ検査で、**もともと要約が無かった** 17 メソッド (Enumerable 4、
Untyped 4、Array の `&` `all?` `count` `max` `min` `|` `Array.new` ほか) も
見つかった。要約を書いて埋めてある。

### 112 バイトの上限

要約は両言語あわせて 112 バイトまで (`ET_DOC_MAX`)。英語を足したときに
`set_font` だけが 181 バイトになった。**日本語側の括弧書きを長文へ移して**
短くした。全件検査して 0 件。

## 残っていること

- **同じ名前が複数のクラスにあると、索引の先頭が選ばれる**。`destroy` は
  7 クラス、`start` `stop` `tick` `draw` も重なる。補完は受け手の型を
  知っているが、**その型が help に渡っていない** (`suggestion()` に
  クラスの欄が無い) ため、F1 は名前だけで引いている。直すなら
  `COMP_FIELD_*` にもう 1 欄足す。
- 例の中に 39 桁を超える行が 60 行ある (大半は前半の回のもの)。折り返しで
  読めはするが、そろえたい。

## 確かめたこと

- `rake ti:test` 通過 (型 db は sig から作り直される)。
- sim (NARYAv4 構成、Spinel エディタ) で `fill_rect` / `bytesize` /
  `ensure_view` を F1。見出し・シグネチャ・要約・長文・markdown の強調が
  出て、`help: <名前> -> <クラス> (1 entry)` がログに出る。
  ページの読み込みは FmrbGfx.en.md (674 行 16.8KB) で pool 19%。
- `flash/help` は 78 ファイル 436KB。ブロック単位の littlefs でも
  8MB 区画に対して小さい。
