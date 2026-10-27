# L12b 指示書: PC では Linux の IME で日本語を打つ

> 計画: doc/linux_native/plan.md (段取りの L12)。
> 前段の結果: report/l12a.md、report/l9a.md 〜 l9e.md、
> doc/editor_ja/plan.md (Family mruby の今のかな入力: ローマ字 → かな、漢字変換なし)、
> `main/drivers/usb/fmrb_kana.h`

## ユーザの決定 (2026-10-28、変えない)

- **PC では、Linux の窓にも Family mruby のアプリにも Linux の IME を使う**
  (fcitx5 + mozc などの、漢字変換のある IME)。Family mruby のアプリでも漢字が
  打てる。ESP32 の端末は今のかな入力のまま。
- **対象の形**: コンポジタの形 (`linux_pc_wl`) は Wayland の IME の口で、GNOME/KDE
  の上の窓の形 (`sdl`) は SDL の IME の口で。cage の形 (`linux_pc`) と Cardputer Zero
  は今のかな入力のまま。
- **IME が入っていない PC では、今のかな入力に戻る**。

## 作業の内容

### 1. コンポジタの形

- コンポジタに input-method (IME が使う口) と text-input (アプリが使う口) を
  持たせる (wlroots 0.19 の input-method-v2・text-input-v3)。Linux のアプリは
  これで IME を使う。IME の候補の窓 (input-popup) は、絵の上の層に、入力中の
  場所の近くに出す (絵の矩形の中に収める)。
- Family mruby のアプリ: フォーカスが Family mruby にあるとき、コンポジタ自身が
  text-input の側に立って IME にキーを渡し、確定した文字 (commit) を Family mruby
  に UTF-8 のキーとして送る (今のかな入力が確定したかなを送る形と同じ)。
  入力中の文字 (preedit) と候補の出し方を決める:
  - 候補の窓は IME の窓をそのまま絵の上に出す。置く場所を決めるため、アプリが
    今の入力の位置を kernel に知らせる口を足す (エディタと Shell が使う)。
  - preedit は IME の窓の中に出す設定で足りるか、アプリの中に出すべきかを
    調べ、決めて report に書く。
- 切り替えのキー (半角/全角・Ctrl+Space) は IME に渡す。メニューバーの
  かなの表示は、IME の状態が分かる範囲で合わせ、分からなければ PC では出さない
  (report に書く)。Family mruby の今のかな入力は、IME を使っている間は働かない
  ようにする。
- IME の起こし方: セッション (ログイン画面の入口) で fcitx5 があれば起こす、
  など。入っていないときは今のかな入力に戻ることをログに 1 行出す。

### 2. SDL の窓の形

- SDL の IME の口 (SDL_StartTextInput・SDL_TEXTEDITING・SDL_TEXTINPUT、
  入力の位置は SDL_SetTextInputRect) で、ibus・fcitx からの確定した文字を
  Family mruby に送る。IME が無いときは今のかな入力。

### 3. 端末・sim・ブラウザ版・cage・Cardputer

- 今のかな入力のまま。動き・画面・内蔵 RAM を変えない。

## 触ってよい範囲

- fmruby-core: `native/`、`hosted/`、`config/machines/`、`main/kernel/` (入力の
  位置の口・かな入力を止める所)、`main/drivers/usb/` (かな入力の切り替えの所)、
  `lib/add/` の該当箇所、`main/prebuild_scripts/` (エディタ・Shell の入力の位置)、
  `native/package/` (セッションで IME を起こす所、Depends・Recommends)、`test/`、
  `rakelib/`、`tools/regress/`、`doc/linux_native/report/`、`doc/regression/usage.md`、
  `sig/`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- sudo は使わない (コンテナの像に fcitx5・mozc を入れるのはよい。手元の WSL に
  入れない)。HOME・XDG の場所は一時ディレクトリ。WSLg に窓を出しっぱなしに
  しない。
- kernel・エディタ・Shell の Ruby を変えたら、標準 (Spinel) と全部 mruby の両方で
  確かめる。

## 受け入れ条件

1. コンテナのコンポジタで fcitx5 + mozc を起こし、foot に「にほんご」を打って
   変換して「日本語」が入る (`rake regress:wl` に足す)。
2. 同じく Family mruby のエディタに「日本語」が入り、候補の窓が入力の位置の
   近くの絵の中に出る (grim の画面)。
3. IME が無いとき、エディタで今のかな入力 (ローマ字 → かな) が働く。
4. `sdl` の機種の IME の口が、確定した文字をエディタに渡す (試せる範囲で。
   試せなければ作りと理由を report に)。
5. 端末・sim・ブラウザ版のかな入力が今どおり (`rake regress:screens`、
   `rake regress`)。`rake regress:size` (内蔵 RAM +0)、`rake native:test`、
   `rake regress:wl`、deb_check が通る。
6. 1・2 を標準と全部 mruby の両方で。
7. 最後に amd64 の `.deb` を作る。

## 止まる条件

- 上の決定と食い違う作りが要るとき (例: wlroots 0.19 で Family mruby のアプリ側に
  立つ形が成り立たない)。
- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える作り
  しか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l12b.md) に書くこと

- input-method・text-input のつなぎ方、Family mruby のアプリへの渡し方、
  候補の窓と preedit の扱い、入力の位置の口
- 切り替えのキーと表示、IME の起こし方と無いときの戻り方
- SDL の形のつなぎ方
- 受け入れ条件ごとの結果と数値、VM で人が確かめる手順 (fcitx5・mozc の入れ方)、残件
