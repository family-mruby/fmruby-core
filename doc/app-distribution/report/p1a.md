# P1-A: apps リポジトリ単体 (2026-09-02)

> 状態: 完了 | `family-mruby-apps` の骨格・サンプル 3 本・生成と検査の道具。
> fmruby-core は 1 行も触っていない。残るは P1-B (実機側の前提の確認)。

計画は plan.md、指示は instruction_p1.md、形式の正は spec.md。

## やったこと

`tmp/family-mruby-apps` (= `git@github.com:family-mruby/family-mruby-apps.git`)
に 22 ファイル。Ruby は 841 行で、**外部の gem は 1 つも使っていない**
(標準ライブラリの zlib / digest / json / fileutils / tmpdir だけ)。

```
LICENSE                     MIT
README.md                   アプリの書き方と、実機で落ちる書き方の注意
Rakefile                    validate / registry / selftest / check_registry
tools/manifest.rb           .app.toml とコメント埋め込みの読み取り
tools/validate.rb           spec.md 5.1 の検査
tools/registry.rb           registry.json の生成
tools/gen_thumb.rb          PNG -> 32x24 の RGB332 BMP
tools/fmrb_png.rb           fmruby-core から複製 (下記)
tools/selftest.rb           検査が効いていることの検査
apps/hello_store/           1 ファイル・コメント埋め込み・鍵の省略
apps/paint_pad/             別ファイル toml・配布鍵を全部・320x240
apps/wide_only/             弾かれる側 (modern/web のみ、min_width 400)
registry.json               生成物 (3405 バイト)
.github/workflows/ci.yml    rake selftest + rake check_registry
```

## 前提の静的確認 (P1-B の前借り)

instruction では「手で置いて動かす」を最初に置いていたが、fmruby-core を触ら
ない範囲でやるという条件がついたので、**コードを読んで確かめられる分を先に
潰した**。

### 未知の鍵は無視される — 読み手 2 つとも確認

| 読み手 | 実体 | なぜ安全か |
|---|---|---|
| C 側 | `fmrb_toml_get_string` / `_int` (`components/fmrb_toml/fmrb_toml.c:91`) | tomlc99 の表から**鍵で引くだけ**。無ければ既定値を返す。表に何が余分に入っていても触らない |
| Ruby 側 | `parse_app_toml` (`launcher.rb:426`) | 行ループで `next unless key == <4 つのうちどれか>`。**値を触る前に弾く** |

### 単一行の配列 (`app_env = ["retro", ...]`) も両方を素通りする

C 側は tomlc99 が正しく配列として解析し、誰も読まないだけ。Ruby 側は
`app_env` が 4 つの鍵に入っていないので値に到達しない。仮に複数行に跨って
書いても、継続行には `=` が無く `next unless eq` で落ちる。

**したがって P1-B で残るのは「本当に警告が出ないか」の実地確認だけ**になった。
壊れる筋は静的には見つかっていない。

## 決めたこと・分かったこと

### コメント埋め込みに配布鍵は全部は入らない

実機は `.rb` の**先頭 512 バイト**しか読まない (`fmrb_app_spawner.c:398`)。
`hello_store` で必須 6 鍵 + `app_screen_name` + `_ja` + `app_description` を
書いたところ **249 バイト**で、まだ余裕はある。ただし `app_description` を
日本語で書き、`app_source` と `app_license` を足すと 400 バイトを超える。

**1 ファイル配布では鍵を絞る**のが正しい運用で、README にそう書いた
(「入らなければ `.app.toml` を使う」)。実測は P1-B で境界まで詰める。

### 縮小は箱平均でないと絵にならない

`gen_thumb.rb` は最初、近傍から 1 画素拾う形で書いた。426x240 を 32x18 に
落とすと**13 画素に 1 つしか見ない**ので、窓枠が消えたり文字が砂になったり
して何の絵か分からなくなる。行き先の 1 画素が覆う範囲を平均する形に変えた。

生成物を実機と同じ読み方 (画素 = RGB332、パレット無視) で読み直して確認した:

```
################################   ← メニューバー
::::::::::::::::::::::::::::::::
 .:::::::::::::::::::::::::::      ← 窓のタイトルバー
.....                        .
 .::::::::...                 .    ← 中身
```

16:9 の写しが 32x24 の中で上下 3 行ずつ空いているのも見えており、縦横比を
保った上での余白詰めも効いている。

### 黒が透明に化ける

透明色は 0 で、RGB332 の黒も 0 になる。真っ黒な画面の写しを入れると絵の
一部に穴が開くので、`rgb332` は 0 になった画素を 0x01 (一番暗い非ゼロ) へ
寄せている。テーマ色に 0x01 を使わない話 (別件) とは向きが逆で、こちらは
**絵の側が 0 を避ける**。

### fmrb_png.rb は複製した

apps リポジトリは単体で成立する必要があるので参照にできない。複製した上で
**JPEG の経路 (Pillow を呼ぶ) を落とした**。この repo が読むのは作者が
コミットした PNG だけで、CI に python を要求する理由が無い。出どころと
改変点はファイル冒頭に書いてある。

## 自己検査で見つけた自分のバグ 2 件

`rake selftest` (18 検体) は、検査そのものが効かなくなっていないかを見る。
書いた直後の 1 回目で**自分のバグを 2 件出した**。

1. **正規表現リテラルを検出できていなかった。** `=~` と `Regexp` の語しか
   見ておらず、`PATTERN = /abc/` を素通ししていた。picoruby に `Regexp` は
   無いので、これは実機で落ちる書き方そのもの。
   スラッシュは割り算でもあるので、**値が来る位置** (`=` `(` `[` `,` `when`
   `return` の直後) のスラッシュだけを数える形にした。`(w - dw) / 2` や
   `x / 16` は識別子・閉じ括弧の後なので当たらない。
   この「当たってはいけない」側も検体にした (`division is not mistaken for
   a regexp`)。**検査が過敏になる壊れ方も、鈍くなる壊れ方と同じくらい悪い**。
2. **検体の方が壊れていた。** `.start` を消す検体が `/^app\.start$/` で
   置換していたが、実物は `begin` の中で字下げされていて一致しない。
   検査は正しく、テストが嘘をついていた。

### `check_registry` は 2 方向を見る必要があった

最初 `git diff --exit-code` だけにしていたが、**追跡されていないファイルには
何も言わない**。生成した `.thumb.bmp` をコミットし忘れた場合が素通りする。
`git status --porcelain` に変えて両方を見るようにし、実際に 2 通り
(コミット済みの registry.json が古い / 生成物が未コミット) で落ちることを
確かめた。

## 受け入れ条件の現在地

| | 状態 |
|---|---|
| `rake registry` が 3 本を載せる | **済** (3405 バイト) |
| わざと壊した検体を落とす | **済** (18 検体。当初の 4 通りから増やした) |
| CI が PR で走り、registry が古いと落ちる | **書いた**。実際に PR で回すのは push 後 |
| `LICENSE` と README の同意の一文 | **済** |
| SPDX に無い `app_license` を落とす | **済** (検体つき) |
| 配布鍵つきの toml が sim で警告なく起動する | **P1-B** |
| ブラウザ版でも起動する | **P1-B** |
| 鍵を省略した `hello_store` が普通に起動する | **P1-B** |
| raw.githubusercontent.com が 200 とヘッダを返す | **P1-B** (push 後) |
| Retro 実機で起動する | **P1-B** (ユーザ依頼) |

## 残した宿題

- **画面の写しは仮のもの**。まだアプリを動かせていないので、`*.png` は
  それらしい画を Ruby で描いた置き換え前提の絵である。P1-B で sim を
  動かして本物に差し替える。`rake registry` を回し直すだけでよい。
- **picoruby の本物の構文検査は入れていない**。文字列の検査 (`Regexp` /
  `defined?` / `pack` / `binread` / `.start` の有無) で代用している。
- **まだコミットしていない**。作業ツリーに置いてある。
