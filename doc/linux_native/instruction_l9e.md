# L9e 指示書: PC の全画面の見せ方と、コンポジタの小さな直し

> 計画: doc/linux_native/plan.md (「後で直すもの」の L9a〜L9d と VM の分)。
> 前段の結果: report/l9a.md 〜 report/l9d.md (特に l9d.md の 10 章)

## ユーザの決定 (2026-10-28、変えない)

- **全画面**: PC (コンポジタの機種) では、高解像度の全画面 (エディタの全画面
  など) を 640x360 にせず、**デスクトップと同じ解像度 (画面の大きさ、例
  640x400) の全画面**にする。PC で 640x360 を拡大するとかえって粗いため。
  426x240 を前提に作った全画面のアプリ (shooter などのゲーム) は今どおり
  426x240 を最大の整数倍で真ん中に出す。
- **Firefox などの自分で飾りを描くアプリ**の二重の帯は、今はそのままでよい。
- **出力の大きさの追従**は今の仕様 (起動時に 1 回決める) でよい。

## 作業の内容

### 1. 全画面

- 高解像度の全画面を求めるアプリ (`fullscreen_hires`) に、PC の広い画面では
  画面の大きさそのもの (例 640x400、960x540) を渡す。エディタが任意の大きさの
  全画面で正しく並ぶか (桁数・行数・メニュー・状態の行) を確かめる。
- 640x360 を決め打ちしている高解像度のアプリ (麻雀など) があれば、その扱いを
  調べ、崩れるなら 640x360 を 2 倍で真ん中に出す (余りは黒)。どちらにしたかを
  report に書く。
- 端末と共通の表示のタスクの条件を変える必要があれば、変えてよい (ユーザ承認
  済み)。ただし端末・sim・ブラウザ版の動きと画面の基準と内蔵 RAM は変えない
  (`regress:screens`・`regress:size` で示す)。

### 2. コンポジタの小さな直し

- 親の窓を押してダイアログが前に出たとき、キーボードもダイアログに移す。
- X11 のメニューを、Family mruby を押したときにも閉じる。
- 止まっている矢印の下に窓が現れたり動いたりしたとき、矢印を動かさなくても
  穴の判定をやり直す (置き場所が変わったときに判定し直す)。
- 全画面のアプリに切り替わる瞬間に前の見せ方の絵が 1 枚出るのを無くす。
- 終える前の待ち (最大 5 秒) の間も、制御の口 (shot・quit) に答える。
- アイコン: アイコンのテーマ (Breeze など、利用者の設定のテーマから hicolor へ
  順に) を読み、SVG しか無いものも変換して使う (librsvg など。.deb の Depends を
  合わせる)。代役 (タスクバー) にも一覧のアイコンを付けられるなら付ける。
- Qt のアプリが Plasma の見た目にならない件を調べ、Plasma が入っている PC では
  Plasma の見た目になるよう環境を整える (例: `QT_QPA_PLATFORMTHEME`)。
  `XDG_CURRENT_DESKTOP` で一覧の `OnlyShowIn` の判定が変わる場合は、判定が
  今と変わらないようにする。

## 触ってよい範囲

- fmruby-core: `native/`、`config/machines/`、`main/` の表示・kernel・アプリの
  起動のうち全画面の大きさの所、`main/prebuild_scripts/` (kernel・エディタの
  全画面の並べ方)、`lib/add/` の該当箇所、`rakelib/`、`tools/regress/`、`test/`、
  `doc/linux_native/report/`、`doc/regression/usage.md` (確認の一式が変わるとき)。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、plan.md、
  arm64 の作り方。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない
  (`rake regress:size` は build/ を退避して戻すので使ってよい)。
- sudo は使わない。HOME・XDG の場所は一時ディレクトリ。WSLg に窓を出し
  っぱなしにしない。開いたままのブラウザ (wasm の道具) は閉じない。
- kernel・エディタの Ruby を変えたら、標準 (Spinel) と全部 mruby の両方で
  確かめる。

## 受け入れ条件

1. 1280x800 の出力でエディタの全画面が 640x400 で出て、出力をちょうど埋め、
   1 打鍵が通る。1920x1080 では 960x540。shooter は今どおり 426x240 を最大の
   整数倍で真ん中。
2. 2 の各直しを `rake regress:wl` の確認に足して通す (ダイアログへのキー、X11 の
   メニューを閉じる、止まった矢印の下の穴、切り替えの 1 枚、待ちの間の制御の口)。
3. Breeze の SVG しか無いアプリ (コンテナに入れられるもの) のアイコンが一覧に
   出る。Qt の見た目の件は、確かめた方法と結果を report に書く。
4. `rake regress`、`rake regress:screens` (端末・sim の基準が変わらない)、
   `rake regress:size` (内蔵 RAM +0)、`rake native:test`、`rake regress:wl`、
   amd64・arm64 の deb_check が通る。
5. 1・2 を標準と全部 mruby の両方で、pixman で。
6. 最後に amd64 の `.deb` を作る。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える作り
  しか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l9e.md) に書くこと

- 全画面の大きさの決め方と、640x360 を決め打ちしたアプリの扱い
- 各直しの原因と直し方
- アイコンの読み方と変換の重さ、Qt の見た目の調べた結果
- 受け入れ条件ごとの結果と数値 (内蔵 RAM・flash の差)、VM で人が確かめる手順、残件
