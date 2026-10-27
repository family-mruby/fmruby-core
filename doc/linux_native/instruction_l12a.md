# L12a 指示書: 全端末に共通のクリップボード、PC では Linux とつなぐ

> 計画: doc/linux_native/plan.md (段取りの L12)。
> 前段の結果: report/l9a.md 〜 l9e.md (コンポジタ)、エディタのクリップボードは
> `main/prebuild_scripts/default_app/editor/clipboard.rb` (今はエディタの中だけ)

## ユーザの決定 (2026-10-28、変えない)

- **全端末に共通の口**: kernel に文字のクリップボードを 1 つ持たせ、Family mruby の
  アプリ同士でコピーと貼り付けができるようにする。端末では置き場所を PSRAM に
  し、**内蔵 RAM は +0**。
- **使うアプリはエディタと Shell**: エディタの Ctrl+C/X/V と Shell の貼り付けを
  共通のクリップボードにする。ほかのアプリには Ruby の口 (FmrbApp の口) を出す
  だけ。
- **PC ではそのクリップボードを Linux のクリップボードとつなぐ** (コンポジタの
  形では Wayland の選択、両方向)。
- **ブラウザ版はブラウザのクリップボードとつなぐ**。
- L12 は 2 つに分ける。L12a (この指示書) はクリップボード、L12b は日本語入力。

## 作業の内容

- kernel のクリップボード: UTF-8 の文字列 1 つ (上限の大きさを決めて report に
  書く。端末は PSRAM、native・ブラウザ版は普通のヒープ)。書く・読む・変わった
  回数 (世代) を問う口。複数のタスクから安全に使えること。Spinel のカーネルと
  全部 mruby の両方で動く Ruby の口 (`FmrbApp` の名前の付け方に合わせる)。
- エディタ: 今のエディタの中のクリップボードを共通の口に置き換える (エディタを
  2 つ開いてもコピーが渡る)。Spinel のエディタと mruby のエディタの両方。
- Shell: 貼り付けのキー (エディタと揃える、Ctrl+V など。今の Shell のキーの
  割り当てを読んで決める) で、クリップボードの中身を入力行に入れる。改行を含む
  ときの扱いを決めて report に書く。Shell の `!` で Linux のプログラムが動いて
  いる間は、その入力に送る。
- PC (コンポジタ):
  - Family mruby でコピーしたら、コンポジタが Wayland の選択 (wl_data_device、
    `text/plain;charset=utf-8` ほか文字の種類) として出す。
  - Linux のアプリが選択を出したら、文字の種類なら読み取って Family mruby の
    クリップボードに入れる (大きすぎるものは切るか断る)。待たない作り
    (受け渡しの口を使い、主スレッドを止めない)。
  - primary selection (中ボタンの貼り付け) は扱わなくてよい (report に書く)。
  - Xwayland の X11 のアプリとの間は wlroots の仕組みで渡るかを確かめる。
- PC (SDL の窓、`sdl` の機種): SDL のクリップボードの口でつなぐ。
- ブラウザ版: ブラウザのクリップボードの口でつなぐ (ページの決まりで読めない
  ときの扱いを決める)。
- sim (Linux の docker): つながなくてよい (Family mruby の中だけ)。

## 触ってよい範囲

- fmruby-core: `main/kernel/`、`components/`、`lib/add/` (ガード付き)、
  `main/prebuild_scripts/` (kernel・エディタ・Shell)、`native/`、`hosted/`、
  `wasm/` (ブラウザのクリップボード)、`test/`、`rakelib/`、`tools/regress/`、
  `doc/linux_native/report/`、`doc/regression/usage.md`、`sig/` (Ruby の口の型。
  直したら `rake clean` の注意は sig/README.md)。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- HOME・XDG の場所は一時ディレクトリ。WSLg に窓を出しっぱなしにしない。開いた
  ままのブラウザ (wasm の道具) は閉じない (ブラウザ版の確認ができないときは
  その旨を report に書く)。
- kernel・エディタ・Shell の Ruby を変えたら、標準 (Spinel) と全部 mruby の
  両方で確かめる。エディタは 1 回起動して 1 打鍵まで (CLAUDE.md の決まり)。

## 受け入れ条件

1. sim (Modern) でエディタを 2 つ開き、片方でコピーした文字をもう片方に貼れる。
   Shell にも貼れる。
2. コンポジタの機種で、エディタでコピーした文字を foot に貼れる。foot で選んで
   コピーした文字 (foot のコピーのキー) をエディタに貼れる。日本語を含む文字で。
   (`rake regress:wl` に足す)
3. `sdl` の機種で SDL のクリップボードとつながる (host の試験か native の確認)。
4. ブラウザ版で、ブラウザのクリップボードとつながる (道具で確かめられれば)。
5. `rake regress`、`rake regress:screens`、`rake regress:size` (内蔵 RAM +0、
   PSRAM と flash の差を書く)、`rake regress:run` (HEAD との比較)、
   `rake native:test`、`rake regress:wl` が通る。
6. 1・2 を標準と全部 mruby の両方で。
7. 最後に amd64 の `.deb` を作る。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- 内蔵 RAM が増える作りしか無いとき。
- 端末の動きを、クリップボードを共通にする以外で変えないとできないとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l12a.md) に書くこと

- kernel のクリップボードの形 (置き場所・上限・世代・錠) と Ruby の口
- エディタと Shell の変更、改行の扱い
- Wayland・SDL・ブラウザとのつなぎ方、待たない作り
- 受け入れ条件ごとの結果と数値、VM で人が確かめる手順、残件
