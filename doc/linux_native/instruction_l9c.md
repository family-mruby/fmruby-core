# L9c 指示書: 親の窓に付いたダイアログ、VM での描画の自動の切り替え

> 計画: doc/linux_native/plan.md (「後で直すもの」の「VM で見つけたもの」)。
> 前段の結果: report/l9a.md、report/l9b.md (7 章: VirtualBox の手順)

## 経緯 (2026-10-27、ユーザの VirtualBox の Kubuntu 26.04 での確認)

- ログイン画面の入口から Family mruby が立ち、Linux の区分・Konsole・重なり・
  xeyes・Exit でログイン画面に戻る、は動いた。
- **不具合**: KDE のアプリが出した「Saved Sessions / New Session /
  Manage Sessions...」の窓 (親の窓に付いたダイアログと見られる) が、代役の枠
  なしで、全部の窓 (Family mruby の Shell の窓を含む) の上に出て、Family mruby の
  絵の外 (整数倍の余りの黒い帯) まではみ出した。
- **VMSVGA (3D なし) では画面が黒いまま**だった。`/etc/default/family-mruby-session`
  に `WLR_RENDERER=pixman` と `WLR_NO_HARDWARE_CURSORS=1` を書くと動いた
  (どちらが効いたかは分けて確かめていない)。

## ユーザの決定 (2026-10-27、変えない)

- GPU で描けないとき・VM の上と分かったときは、session が**自分で pixman と
  矢印のソフト描画に切り替える**。設定ファイルに書いた値はそれより勝つ
  (例: VM で 3D を入れて GLES を試したい人は `WLR_RENDERER=gles2` と書ける)。
- 親の窓に付いたダイアログは、**親の窓の上に、Family mruby の枠つきで**出す。

## 作業の内容

### 1. ダイアログ (親を持つ窓)

- まず再現する: コンテナの中で、親を持つ xdg の窓を出すアプリ (Qt・GTK の
  ファイルを開く画面、`kdialog`・`zenity`、または試験用の小さなクライアント) で、
  今の扱い (どの層に入るか、代役が付くか、どこに置かれるか) を確かめ、report に
  原因を書く。xdg_popup と取り違えているのか、親つきの toplevel を代役の対象から
  外しているのか、置き場所が渡らないのか。
- 親を持つ toplevel (`xdg_toplevel.set_parent`、X11 の `WM_TRANSIENT_FOR`) にも
  代役を付け、普通の Linux の窓と同じく絵の下に描き、穴で見せる。
  - 置き場所: 親の代役の中身の真ん中 (Family mruby の画面に収まるようずらす)。
  - 前後: 出たとき前に出し、フォーカスを渡す。親を押して前に出したときに
    ダイアログが親の後ろに隠れない形にする (代役の並べ方で済むなら kernel は
    変えない。済まないなら作りを report に書いて止まる)。
  - 親が閉じたらダイアログも消える (アプリが消すのに任せてよい)。
- メニュー・ツールチップ (xdg_popup、X11 の override-redirect) は今どおり絵の
  上の層でよいが、**Family mruby の絵の矩形の外 (黒い帯) に出さない**。
  xdg_positioner の制約の箱を絵の矩形 (出力の座標) にし、X11 のものは置き場所を
  絵の中へ寄せる。

### 2. VM での描画の自動の切り替え

- session の台本 (`family-mruby-session`) か program の起動時に、次のどれかで
  pixman と `WLR_NO_HARDWARE_CURSORS=1` に切り替える。どれをどう使うかは作りを
  読んで選び、report に理由を書く。
  - VM の上と分かる (`systemd-detect-virt` など。VirtualBox・VMware・QEMU 等)。
  - GPU のレンダラが作れない、または使えないと分かる (wlroots の自動の選び方に
    任せたうえで、黒い画面になる場合を見分けられるか調べる)。
- 設定ファイルや環境変数で `WLR_RENDERER`・`WLR_NO_HARDWARE_CURSORS` が
  決まっていればそれに従う。切り替えたときは、理由をログに 1 行出す。
- 実機の PC (GPU あり、VM でない) では今と同じ動き (GLES) になること。
- VirtualBox はコンテナに無いので、VM の判定は差し替えられる形で試す
  (判定のコマンドを偽物にするなど)。最後の確認は親とユーザが VM で行う。

## 触ってよい範囲

- fmruby-core: `native/` (wlr、session、package)、`config/machines/`、
  `rakelib/`、`test/`、`doc/linux_native/report/`。代役の並べ方のために
  `main/prebuild_scripts/kernel/linux_window.app.rb` と `lib/add/` の native
  だけの部分。
- 触らないもの: kernel・デスクトップの Ruby (変えないとできなければ止まる)、
  fmruby-graphics-audio、sdkconfig、submodule の中、`flash/`、plan.md、
  端末・sim・ブラウザ版の動き、arm64 の作り方。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- sudo は使わない。HOME・XDG_DATA_HOME は一時ディレクトリ。WSLg に窓を出し
  っぱなしにしない。開いたままのブラウザ (wasm の道具) は閉じない。

## 受け入れ条件 (`rake regress:wl` の一式を広げて自動で確かめる)

1. 親を持つ xdg の窓 (と、入れられれば X11 の transient の窓) が、代役の枠つきで
   親の上の真ん中に出て、キーとポインタが届き、ボタンで閉じられる。合成後の
   画面で、絵の矩形の外にはみ出していない。
2. ダイアログが出ている間に Family mruby の窓を前に出すと、ダイアログはその
   後ろに隠れる (一番上に残らない)。親を押して前に出すとダイアログも見える。
3. メニュー (xdg_popup) を窓の端・画面の端で開いても、絵の矩形の外に出ない。
4. VM の判定が「VM」を返すと session が pixman と矢印のソフト描画で起き、
   理由がログに出る。設定ファイルに `WLR_RENDERER=gles2` があればそれが勝つ。
   判定が「VM でない」なら今と同じ。
5. `rake regress:wl`、`rake regress`、`rake native:test` が通る。amd64 の
   deb_check が通り、arm64 の deb_check も今どおり通る。
6. 1・2 を標準と全部 mruby の両方で、pixman で。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- kernel・デスクトップの Ruby を変えないとできないとき。
- 端末・sim・ブラウザ版の動きを変えないとできないとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l9c.md) に書くこと

- 不具合の原因 (再現の手順と、どの層・どの扱いに入っていたか)
- ダイアログの置き場所・前後の作り、ポップアップを絵の中に収める作り
- VM の判定と切り替えの作り、その理由
- 受け入れ条件ごとの結果と、確かめたコマンド・数値
- VM で人が確かめる手順 (設定ファイルを消して、自動で動くこと)
- 撤回した見立て、踏んだ罠、残件
