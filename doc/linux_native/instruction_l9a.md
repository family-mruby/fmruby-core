# L9a 指示書: Linux の窓を Family mruby の窓として扱う

> 計画: doc/linux_native/plan.md (「コンポジタと外の窓」、段取りの L9)。
> 前段の結果: report/l8.md (特に 7 章「L9 に向けた見立て」)
>
> L9 は 2 つに分ける。L9a (この指示書) は窓の組み込み。L9b は Linux のアプリの
> 起動の口 (`.desktop` の一覧をランチャーへ)、Xwayland、ログイン画面の入口を
> cage からこのコンポジタへ切り替えること。

## ユーザの決定 (2026-10-27、変えない)

- **重なり**: Family mruby の窓と Linux の窓は**自由に前後する**。押した窓が
  前に出る。Family mruby の窓 (メニュー・ダイアログを含む) が Linux の窓の上に
  重なることもある。
- **終わり方**: Family mruby を終えるとき、中から起動した Linux のアプリは
  **一緒に閉じる**。各アプリに閉じるよう頼み (アプリが保存の確認を出せる)、
  数秒で終わらなければ止める。
- **回帰**: `rake native:wl_check` (headless) を `rake regress:full` に載せる。
  ふだんの `rake regress` には載せない。

## 作りの方針

- **合成の形**: Linux の窓は Family mruby の絵の**下**に描き、Family mruby は
  Linux の窓が見えるべき所だけを透過で空けた絵を出す (計画の「その窓の場所を
  透過で空けて合成」)。空ける所は kernel の window manager が前後関係から決める
  ので、Family mruby の窓が上にあれば自然にそこは空かない。コンポジタは
  「Linux の窓を置き場所に描く → 透過つきの Family mruby の絵を整数倍で重ねる」
  の順にする。透過の表し方 (今の 0x01 のカラーキーか、別の面か) は絵の受け渡しの
  作りを読んで決め、report に理由を書く。
- **窓の通知を kernel へ**: `comp_route` の通知 (出た・大きさ・題・消えた) を
  kernel が汲み、window manager が「中身を Family mruby が描かない窓」として
  扱う。題の帯・枠・閉じるボタンは Family mruby が描く (他の窓と同じ見た目)。
  Linux のアプリ自身の飾り (foot の題の帯) は、xdg-decoration で「サーバ側で
  描く」と伝えて出させない。
- **置き場所の受け渡し**: window manager が決めた置き場所 (Family mruby の座標、
  中身の矩形)・前後・フォーカスを、コンポジタへ渡す (L8 の見立ての 3 つ目の
  受け渡し)。中身の大きさは Family mruby の座標 × 倍率の本来の画素で、アプリに
  その大きさを頼む (configure)。
- **入力**: input_router がフォーカスを持つ窓を決める。Linux の窓ならキーと
  ポインタをコンポジタに返し、コンポジタが seat 経由でアプリへ配る。ポインタの
  座標は本来の画素に直して渡す (細かく動かせること)。
- **操作**: Family mruby の題の帯での移動、枠での大きさの変更 (今の窓と同じ)、
  閉じるボタンは xdg の close を頼む (アプリが確認を出せる)。アプリからの
  move・resize の頼みは、できる範囲で window manager の操作に変える。最大化・
  全画面は L9a では断ってよい (report に書く)。
- **キーボードの配列**: Linux の窓へのキーの配列を Family mruby の
  `keyboard_layout` と揃える。
- **端末・sim・ブラウザ版**: 外の窓が来ないので動きは変わらない。kernel の
  変更は、外の窓が無いときに今と同じ動きになること、内蔵 RAM を増やさないこと。

## 触ってよい範囲

- fmruby-core: `main/prebuild_scripts/kernel/` (window_manager・input_router・
  デスクトップの描き方)、`main/` の kernel と受け渡しの C の該当箇所、`native/`、
  `hosted/`、`config/machines/`、`rakelib/`、`tools/regress/` (wl_check を
  regress:full に載せる所)、`test/`、`doc/linux_native/report/`、
  `doc/regression/usage.md` (載せた一式の説明)。
- family-mruby (ルート): `tools/`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/`、
  plan.md、端末・sim・ブラウザ版の動き、arm64 の作り方、session (cage) の入口。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- sudo は使わない。HOME は一時ディレクトリ。WSLg に窓を出しっぱなしにしない。
- kernel の Ruby を変えたら、標準 (Spinel のカーネル) と全部 mruby の両方で
  確かめる。Spinel の罠 (型が絞れない呼び出しで黙って 0、mixin の素の定数) に注意。

## 受け入れ条件 (`rake native:wl_check` の一式を広げて自動で確かめる)

1. foot の窓が Family mruby の題の帯と枠つきで出る (foot 自身の題の帯は無い)。
   題の帯をつかんで動かせ、枠で大きさを変えると foot がその大きさで描き直す。
2. 前後: foot の上に Family mruby のエディタの窓を重ねると、重なった所はエディタが
   見え、foot を押すと foot が前に出る。Family mruby のメニュー・確認の
   ダイアログは foot の上に出る (grim の画面で、期待との画素差で示す)。
3. フォーカス: 前にある窓にキーが入る。foot へのポインタの座標が本来の画素の
   細かさで届く (例: foot の中の選択や、`xev` 相当の確かめ)。
4. 閉じるボタンで foot が終わり、窓が消える。応じないアプリ (閉じる頼みを
   無視するもの) は、Family mruby を終えるときに数秒で止められる。Family mruby の
   Exit で、起動した Linux のアプリが全部終わり、プロセスが残らない。
5. keyboard_layout を jp にしたとき、foot に打った記号が配列どおりに入る。
6. `rake regress:full` に wl_check が入って通る。`rake regress`、
   `rake regress:screens`、`rake regress:size` (内蔵 RAM +0) が通る。
   kernel を触ったので、sim の通しの確かめ (`regress:run` は基準が古いので
   HEAD との比較で差を示す) も行う。
7. 1-4 を標準と全部 mruby の両方で。pixman で通る (GLES は WSL2 で試せない)。

## 止まる条件

- 上の決定と食い違う作りが要るとき (例: 自由な前後が合成の形で成り立たない)。
- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える作り
  しか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l9a.md) に書くこと

- 合成の形 (透過の表し方)、置き場所と前後の受け渡し、xdg-decoration
- window manager・input_router に足した外の窓の扱い
- 終わり方 (頼む → 待つ → 止める) の作り
- 受け入れ条件ごとの結果と、確かめたコマンド・数値 (内蔵 RAM・flash の差)
- L9b への見立て、撤回した仮説、踏んだ罠、残件
