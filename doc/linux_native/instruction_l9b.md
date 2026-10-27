# L9b 指示書: Linux のアプリの起動の口、Xwayland、ログイン画面の入口

> 計画: doc/linux_native/plan.md (「コンポジタと外の窓」「Linux のプログラムとの連携」の 1、段取りの L9)。
> 前段の結果: report/l9a.md (特に 9 章「L9b への見立て」と 10 章の残件)、
> report/l8.md、report/l7.md (cage の session と .deb)、report/l10.md (pty の口)

## ユーザの決定 (2026-10-27、変えない)

- **一覧の形**: Linux のアプリ (XDG の `.desktop`) は、ランチャーの中に
  **「Linux」の区分を 1 つ作り、その中に並べる**。Family mruby のアプリとは
  混ぜない。
- **ログイン画面の入口**: 今の「Family mruby」の入口 1 つを、cage から
  **このコンポジタ (`linux_pc_wl`) に置き換える**。cage の作り (`linux_pc` の
  機種の定義) は残すが、入口には出さない。
- **Xwayland**: X11 のアプリが**必要になったときに起こす** (wlroots の
  遅延起動)。起動時には起こさない。

## 作業の内容

### 1. Linux のアプリを起こす口

- `WAYLAND_DISPLAY` をコンポジタ自身の口に、`DISPLAY` を Xwayland の口に
  した環境でプロセスを起こし、覚える口を作る (L10 の pty の口の外側の、
  端末を持たない版。置き場所は `fmrb_platform_sys` の隣、native だけが
  実装を持ち、ほかは「使えない」)。協調の作りなので待たない (poll、
  waitpid も待たない)。ゾンビを残さない。
- `.desktop` の `Exec` は XDG の決まり (`%f` `%u` などの置き換え、引用) に
  従って分け、シェルを通さず起こす。`Terminal=true` のものは foot などの
  端末の中で起こすか、一覧から外す (作りを読んで選び、report に書く)。
- 起こしたプロセスと窓を結ぶ: `StartupWMClass` (無ければ実行ファイル名) と
  xdg の app id (X11 は WM_CLASS) を突き合わせ、代役の名前とアイコンを
  一覧の項目のものにする。
- 終わり方: 今の「窓に頼む → 5 秒待つ → クライアントを止める」に、
  **窓を出さずに残った、起こしたプロセス**を足す (起こした記録で分かる)。
  Family mruby の Exit の後にプロセスが残らないこと。

### 2. ランチャーの「Linux」の区分

- `/usr/share/applications/`・`/usr/local/share/applications/`・利用者の
  `~/.local/share/applications/` (`XDG_DATA_DIRS`・`XDG_DATA_HOME` に従う) の
  `.desktop` を読む。`NoDisplay=true`・`Hidden=true`、`OnlyShowIn`・
  `NotShowIn` が合わないもの (`XDG_CURRENT_DESKTOP` を自分の名前にして判定)、
  `TryExec` が無いものは外す。名前は `Name[ja]` など language に合うものを使う。
- ランチャーの今の作りで、区分を 1 つ足す形 (押すと Linux のアプリの一覧に
  入る、など) を作りを読んで選び、report に理由を書く。数百のアプリでも
  開く速さが落ちないこと (読み込みは一度にせず、ランチャーの今のキャッシュの
  形に合わせるなど)。
- アイコン: アイコンのテーマ (hicolor など) の PNG を、ランチャーのアイコンの
  大きさと色数に変えて使う。見つからない・変えられないものは共通の印にする。
  変換の重さと置き場所 (キャッシュ) を report に書く。
- keys のデスクトップと、Linux の窓の無い端末・sim・ブラウザ版では区分を
  出さない (機種の定義で compositor を持つときだけ)。

### 3. Xwayland

- wlroots の Xwayland を遅延起動で持ち、X11 の窓も L9a の代役で扱う
  (`comp_route` に同じ番号の窓として入れ、置き場所は
  `wlr_xwayland_surface_configure`)。
- X11 のアプリにはサーバ側の飾りを頼む決まった口が無い。`_MOTIF_WM_HINTS` で
  飾りを持たないと言う窓だけ代役の枠を付け、自分で飾りを描く窓をどうするか
  (二重の題の帯を許す・枠だけ省く など) を作りを読んで選び、report に書く。
  ここが利用者から見た動きを大きく変えるなら止まって返す。
- override-redirect の窓 (X11 のメニュー・ツールチップ) は絵の上の層に出す
  (L9a のポップアップと同じ)。
- Xwayland が入っていない PC では、X11 のアプリが使えないと分かる 1 行を
  ログに出し、Wayland のアプリは今どおり動く。

### 4. ログイン画面の入口

- `family-mruby-session` を cage ではなく `family-mruby --machine linux_pc_wl`
  を直接起こす形に変える (DRM/KMS、logind・libseat で座席を得る)。
  `ROTATE` はコンポジタ自身が出力を回す形で残す (wlr_output の transform)。
  `FMRB_ARGS` も残す。
- 終わり方の 5 秒の待ちと、ログアウトの順番を合わせる (Exit の後に表示
  管理 (SDDM) のログイン画面に戻ること)。電源の項目 (logind) の動きは変えない。
- `.deb` の Depends・Recommends を合わせる (cage は不要になる。Xwayland は
  Recommends)。`rake native:deb ARCH=amd64` と deb_check を通す。
  arm64 (Cardputer) の作りは変えない。
- **VirtualBox の Kubuntu 26.04 で人が確かめる手順**を report に書く:
  入れ方、入口の選び方、`WLR_RENDERER=pixman` への切り替え、マウスの矢印が
  見えないときの `WLR_NO_HARDWARE_CURSORS=1` (session の設定ファイルで
  渡せるようにする)、ログの置き場所。

### 5. L9a の残件のうち、ここで直すもの

- アプリが自分で大きさを変えたとき (`RESIZED`)、代役の窓をその大きさに
  合わせる (X11 のアプリは自分で大きさを決めることが多い)。foot が文字の
  格子に合わせて小さく描いた余りの黒も、これで消える形にする。
- 代役の名前 (タスクバーの名前) を一覧の項目の名前にする。

## 触ってよい範囲

- fmruby-core: `main/prebuild_scripts/kernel/` (ランチャーとデスクトップ、
  代役)、`main/` と `lib/add/` の受け口の該当箇所 (native だけの部分)、
  `native/` (session と package を含む)、`hosted/`、`config/machines/`、
  `rakelib/`、`tools/regress/`、`test/`、`doc/linux_native/report/`。
- family-mruby (ルート): `tools/`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/`、
  plan.md、端末・sim・ブラウザ版の動き、arm64 の作り方、`linux_pc` (cage) の
  機種の定義そのもの。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- sudo は使わない。HOME は一時ディレクトリ (`.desktop` の試験の分も、一時の
  `XDG_DATA_HOME` に置く)。利用者のホームや `/usr` に書かない。WSLg に窓を
  出しっぱなしにしない。開いたままのブラウザ (wasm の道具) は閉じない。
- kernel・デスクトップの Ruby を変えたら、標準 (Spinel のカーネル) と
  全部 mruby の両方で確かめる。Spinel の罠 (型が絞れない呼び出しで黙って 0、
  mixin の素の定数) に注意。

## 受け入れ条件 (`rake regress:wl` の一式を広げて自動で確かめる)

1. `linux_pc_wl` のランチャーに「Linux」の区分が出て、一時の
   `XDG_DATA_HOME` に置いた `.desktop` (foot と、`NoDisplay=true` のもの、
   `Name[ja]` を持つもの) が決まりどおりに出る・出ない・名前が変わる。
   コンテナの `/usr/share/applications/` の全部を読んでも、ランチャーを開く
   時間が測れる範囲で増えない (数値を report に)。
2. 一覧から foot を起こすと、代役の名前が一覧の名前になり、窓が出る。
3. X11 のアプリ (コンテナに入れた xterm や xeyes など軽いもの) を一覧または
   Shell の `!` から起こすと、そのとき初めて Xwayland が起き、窓が代役つきで
   出て、キーとポインタが届く。Xwayland を起こす前のプロセスの一覧に
   Xwayland が無いこと。
4. 自分で大きさを変えるアプリ (X11 のアプリ、または foot の格子) で、代役が
   アプリの大きさに合う (合成後の画面で黒の余りが無い)。
5. Exit で、Wayland の窓・X11 の窓・窓を出さずに残ったプロセス (例: 一覧
   から起こした `sleep` 相当) が全部終わり、Xwayland も終わり、プロセスが
   残らない。
6. session: コンテナの中で `family-mruby-session` を headless (または入れ子) で
   起こしてコンポジタが立ち、Exit で session が終わる。`ROTATE=90` で出力が
   回る (grim の画面で)。`.deb` (amd64) を作り deb_check が通る。arm64 の
   deb_check も今どおり通る。
7. keys のデスクトップ (`sdl_keys`)・`sdl` の native、sim、ブラウザ版に区分が
   出ない。`rake regress`、`rake regress:screens`、`rake regress:size`
   (内蔵 RAM +0) が通る。`rake regress:wl` が通る。画面の基準が変わったら
   変化を report に書き、基準の更新は別のコミットにする。
8. 1・2・3・5 を標準と全部 mruby の両方で。pixman で通る。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- X11 の飾りの扱いで、利用者から見た動きを大きく変える選択が要るとき。
- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える
  作りしか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l9b.md) に書くこと

- 起こす口の形、`.desktop` の読み方と外す決まり、`Terminal=true` の扱い
- ランチャーの区分の作り、アイコンの変換と重さ
- Xwayland の遅延起動、X11 の飾りの扱いとその理由
- session の変更、Depends、VirtualBox の Kubuntu 26.04 で人が確かめる手順
- 受け入れ条件ごとの結果と、確かめたコマンド・数値 (内蔵 RAM・flash の差)
- 撤回した見立て、踏んだ罠、残件
