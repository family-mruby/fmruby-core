# 実装指示 P1: 置き場・サンプル・自動検査

> **A と B に分けて進める** (2026-09-02)。
> **P1-A** = `family-mruby-apps` リポジトリ単体。fmruby-core を一切触らない。**実装済 (report/p1a.md)**。
> **P1-B** = 実機側の前提の確認。sim / ブラウザ / 実機を使う。**未着手**。

計画は `plan.md`、形式の正は `spec.md`。作業場所は
`family-mruby-apps` リポジトリ (`git@github.com:family-mruby/family-mruby-apps.git`、
手元では `tmp/family-mruby-apps`) と、確認のための fmruby-core の sim / wasm。

## この段の狙い

Web 側の見栄えではない。**spec.md が置いている 2 つの前提を実地で潰す**こと。

1. `.app.toml` に配布用の鍵を足しても、実機の toml 解析器は未知の鍵を黙って
   無視するので壊れない。
2. `required_heap_kb_*` を省略したときの既定 (普通のアプリ扱い) が、今の挙動と
   変わらない。

この 2 つが崩れると「P3 までファームウェアを変更しない」が成立しない。
**先にこれを確かめてから道具を作る**。

## 手順

### 1. 前提の確認 (P1-B)

**当初はここを最初に置いていたが、A と B に分けたので後回しにした。**
代わりに、道具を作る前に**静的に確認できる分だけ先に潰した**
(report/p1a.md の「前提の静的確認」)。読み手 2 つのコードを読み、未知の鍵が
無視されることと、単一行の配列が両方の読み手を素通りすることを確かめてある。

残るのは実際に動かす確認で、これが P1-B。手で置いて動かす。

`paint_pad.app.toml` を書く (配布鍵を全部入れる):

```toml
app_screen_name = "Paint Pad"
app_screen_name_ja = "お絵かき"
default_window_mode = "window"
default_window_width = 260
default_window_height = 180
task_stack_kb = 32

app_id = "paint_pad"
app_version = "1.0.0"
app_author = "family-mruby"
app_description = "Draw with the mouse"
app_description_ja = "マウスで描く"
app_category = "tool"
app_env = ["retro", "modern", "web"]
app_min_width = 320
app_min_height = 240
app_source = "https://github.com/family-mruby/family-mruby-apps"
```

`paint_pad.app.rb` は窓に線を引くだけの短いもので足りる。末尾の起動行
(`begin / PaintPad.new / .start / rescue`) を忘れない。

確認:

```
# sim
rake build:linux                     # 両リポジトリ
file build/fmruby-core.elf           # x86-64 であること (stale な esp32 を掴まない)
mkdir -p flash/app/usr/paint_pad && cp ... 
sim_up → sim_app /app/usr/paint_pad/paint_pad.app.rb → sim_screenshot
docker logs fmruby_core | grep -i "toml\|unknown\|warn"
```

**見るもの**: 窓が toml の大きさで開くこと、表示名が出ること、そして
**ログに未知の鍵の警告が無いこと**。警告が出たらこの段の前提が崩れているので、
道具を作る前に plan.md へ持ち帰る。

同じものを `web_fs` でブラウザ版へ置いて起動する。

`hello_store` (1 ファイル、コメント埋め込み) でも同じ確認をする:

```ruby
#---fmrb
# app_screen_name = "Hello Store"
# app_id = "hello_store"
# app_version = "1.0.0"
# ...
#---
```

コメント埋め込みは**先頭 512 バイトまでしか読まない**うえ、囲みが最初の
非コメント行より前になければならない (`fmrb_app_spawner.c:398`)。配布鍵を
全部書くと 512 バイトを超えるので、**1 ファイル配布では鍵を絞る**必要がある。
どこまで入るかをここで実測し、結果を report に書く。

### 2. リポジトリの骨格

```
apps/hello_store/hello_store.app.rb
apps/paint_pad/paint_pad.app.{toml,rb}
apps/wide_only/wide_only.app.{toml,rb}
tools/registry.rb
tools/validate.rb
Rakefile
registry.json
.github/workflows/ci.yml
LICENSE
README.md
```

`wide_only` は弾かれる側の検体。`app_env = ["modern", "web"]`、
`app_min_width = 400` (Modern の 426 には収まり、Retro の 320 には収まらない)。中身は何でもよい。

### 3. `tools/registry.rb`

`apps/*/` を歩き、`.app.toml` (無ければ `.rb` のコメント埋め込み) から
`registry.json` を作る。出力の形は spec.md 5 節。

- 素の Ruby と標準ライブラリだけで書く (周辺ツールは Ruby、外部 gem に
  頼らない)。toml は使う鍵が平の代入と配列だけなので、小さな読み取りで足りる。
- `sha256` と `size` は `digest` と `File.size` で入れる。
- 出力は**鍵の順を固定**する。並びが揺れると差分検査 (下) が毎回落ちる。

### 3.5 `tools/gen_thumb.rb`

作者が置いた `<app_id>.png` から、実機用の `<app_id>.thumb.bmp` (32x24) を
作る。`rake registry` から呼ぶ。

**外部の画像処理は使わない。** 読むのは `fmruby-core/tools/fmrb_png.rb`
(`FmrbPng.read_rgb`)、書くのは `fmruby-core/tool/gen_icon_bmp.rb` の
`write_bmp` と同じ形 (8 ビット、画素は RGB332 そのもの、パレットは付ける、
透明は 0)。この 2 つを apps リポジトリへ写すか、参照するかは実装者が決める
(写すなら出どころをコメントに書く)。

縦横比が合わない分は 0 (透明) で埋める。Retro のスクリーンショットは 4:3、Modern は 16:9 で
入ってくる。

### 4. `tools/validate.rb`

spec.md 5.1 の検査。落とすべきものを落とせることを、わざと壊した検体で
確かめる (受け入れ条件)。

- `app_id` の重複、ディレクトリ名との不一致
- 必須鍵の欠落、`app_version` の形式
- `.app.toml` と対になる本体の不在 (`<base>.rb/.lua/.bas/.py`)
- `app_env` / `app_category` の値が表に無い
- `app_files` に `..` や絶対パスが混じる
- `app_license` が書かれていて SPDX の表に無い
- `app_screenshot` (既定 `<app_id>.png`) が無い、または PNG として読めない
- 実機で落ちる書き方の文字列検査 (`Regexp` リテラル、`defined?`、
  `Array#pack`、`File.binread`)

**picoruby による構文検査はこの段ではやらない**。CI で picoruby を建てる手間に
見合わない。文字列検査で代用し、本物は後の段へ回す。

### 5. CI

`.github/workflows/ci.yml` は 2 つだけ:

1. `rake validate`
2. `rake registry` を走らせ、`git diff --exit-code registry.json`

**CI は `registry.json` を書き戻さない**。書き込み権限を与えないため、および
raw から取る先が常に main の実体であるため。作者は手元で `rake registry` を
走らせてコミットする。README にそう書く。

### 6. 配信の確認

main へ入れたあと:

```
curl -I https://raw.githubusercontent.com/family-mruby/family-mruby-apps/main/registry.json
```

200 で `access-control-allow-origin: *` と
`cross-origin-resource-policy: cross-origin` が返ること。これが spec.md 6 節の
前提であり、P2 の店の画面はこれに乗る。

## 受け入れ条件

plan.md の P1 の節にある一覧をそのまま使う。**前提の確認 4 件を先に埋める**。

## 気をつけること

- `rake build:linux` は esp32 の `build/` が残っていると Xtensa のまま
  「Linux build complete」と出す。`file build/fmruby-core.elf` で確かめる。
- sim は 3 コンテナまとめて起動・停止する。core だけ入れ直すと画面が死ぬ。
- **ライセンスは MIT で確定済み** (spec.md 11.1)。`LICENSE` を置き、README に
  「ここに出すことは MIT で公開することへの同意である」と書く。`app_license` は
  任意で、書く場合は SPDX の識別子。`validate.rb` は表にある値かだけを見る。

## report に残すこと

`report/p1.md` に、確定した仕様ではなく**やってみて分かったこと**を書く。
特に:

- コメント埋め込み toml に配布鍵がどこまで入るか (512 バイトの実測)
- 未知の鍵に対する実機側の実際の反応 (無視するのか、何か言うのか)
- `registry.json` の並びを固定するために必要だったこと
- 落とせなかった壊し方があれば、それ
