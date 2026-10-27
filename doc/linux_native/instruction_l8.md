# L8 指示書: コンポジタの試作

> 計画: doc/linux_native/plan.md (「コンポジタと外の窓」、段取りの L8)。
> 前段の結果: report/l2n.md (協調の作りと補助スレッド)、report/l7.md (cage の
> 入れ子を WSLg で動かした手順と罠)、report/l10.md

## ユーザの決定 (2026-10-27、変えない)

- **wlroots は 0.19** (Kubuntu 26.04 = Ubuntu 26.04 の版) に合わせる。
- amd64 の `.deb` は Ubuntu 26.04 のコンテナで作る形に変える。arm64
  (Cardputer) は今までどおり Debian trixie のコンテナ。Cardputer の形は
  コンポジタを使わない。

## 目的

Family mruby のプロセスそのものを Wayland のコンポジタにする試作。tinywl
(wlroots 付属の最小の例) を出発点に、Family mruby の画面を 1 枚の絵として
敷き、その上に Linux の端末の窓を 1 つ、液晶の本来の解像度で重ねる。
二つの動作の流れ (wlroots の `wl_event_loop` と FreeRTOS の協調のスレッド群)
の受け渡しが成り立つことを確かめる。kernel の window manager への組み込みは
L9 (ここではしない)。

## 作業の内容

### 1. ビルドの場所

- `native/docker/` に Ubuntu 26.04 (日付で固定したタグ) のイメージを足し、
  amd64 のビルドと `.deb` をそちらで作る。wlroots 0.19 の開発用ファイル、
  wayland-protocols、試しのアプリ (foot など軽い端末)、画面を取る道具
  (grim) も入れる。`rake native:deb ARCH=amd64` と deb_check amd64 を
  Ubuntu 26.04 に合わせる (Depends を 26.04 の名前に)。arm64 は変えない。
- 手元の開発 (`rake native:core`) は WSL2 の Ubuntu 22.04 で動いており、そこ
  には wlroots 0.19 が無い。コンポジタの部分はコンテナでビルドし、コンテナの
  中で、WSLg の Wayland の口 (`/mnt/wslg/runtime-dir` の `wayland-0`) を渡して
  入れ子 (`WLR_BACKENDS=wayland`) で動かす。手元のビルドは今までどおり
  (wlroots 無しで通る形に、ビルドの切り替えで分ける)。

### 2. 試作のコンポジタ

- 新しい画面と入力の受け口 `wlroots` を足し、機種の定義 `linux_pc_wl` で選ぶ
  (`linux_pc` (cage) と `linux_pc_drm` は残す)。L1 の display・input の受け口の
  作りに合わせ、上の層に `#ifdef` を入れない。
- 動作の流れ: wlroots は自分のスレッドで `wl_event_loop` を回す。受け渡しは
  2 つだけ: (a) Family mruby の合成済みの絵 (426x240) → コンポジタ、
  (b) コンポジタ → Family mruby の入力 (と、L9 で使う窓の通知の入れ物)。
  ロックの持ち方と起こし方 (eventfd など) を決め、report に書く。
- 描画: Family mruby の絵を出力の大きさに整数倍で拡大して敷き、余りは黒
  (L2 の共通の部品の決まりと同じ)。外の窓はその上に本来の解像度で重ねる。
  置き場所は試作なので固定でよい (例: 画面の中央)。描画は wlroots の
  レンダラに任せ、`WLR_RENDERER=pixman` でも動くこと (VM のため)。
- 入力: 既定では Family mruby が受ける。外の窓を押すとその窓にキーボードの
  フォーカスが移り、Family mruby の絵の上を押すと Family mruby に戻る。
  ポインタは押した所の物へ。キーボードは wlroots の xkb を通して Linux の
  キーコードで受け、L2 のキーの表で Family mruby に渡す。
- 外の窓を起こす口: 試作なので、起動時の設定 (例 `--set compositor.autostart=foot`)
  で 1 つ起こせればよい。子の環境の `WAYLAND_DISPLAY` は自分の口にする。
  L10 の Shell の `!` からも起こせる (`WAYLAND_DISPLAY` を引き継ぐ) と良い。
- 確かめのために wlr-screencopy を出し、grim で合成後の画面を撮れるようにする。
  Family mruby の `shot` (自分の絵だけ) と別に、合成後の絵を取る手段として
  report に手順を書く。
- 終わり: Family mruby が終わったら、起こした外のアプリも終わらせる (L9 で
  決め直す。試作ではこれでよい)。

## 試験

- 受け渡しの部分 (絵の受け渡し・入力の行き先の決め方) は、wlroots 無しで
  動く host 試験にできる所を切り出して試す。
- コンテナの中の入れ子で、grim の画面を基準と比べる確認を 1 つ作る
  (回帰の道具に載せるかは report で提案し、載せるのは親が決める)。

## 触ってよい範囲

- fmruby-core: `native/`、`hosted/`、`config/machines/`、`rakelib/native.rake`、
  `test/native/`、`doc/linux_native/report/`。
- family-mruby (ルート): `tools/` (native の道具に合成後の画面を撮る口を足すなら)。
- 触らないもの: `main/` (kernel・デスクトップ)、fmruby-graphics-audio、
  sdkconfig、submodule の中、`flash/`、plan.md、端末・sim・ブラウザ版の動き、
  arm64 の作り方。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- sudo は使わない。コンテナのイメージは手元で作るだけで、どこにも push しない。
- 試験で利用者のホームに書かない (HOME は一時ディレクトリ)。確かめのために
  利用者の WSLg の画面に窓が出るのは構わないが、出しっぱなしにしない。

## 受け入れ条件

1. Ubuntu 26.04 のコンテナで amd64 の program と `.deb` が作れ、deb_check amd64
   が通る (コンテナの Ubuntu 26.04 に入れて起動する確認を含む)。arm64 の
   deb_check も今どおり通る。
2. コンテナの中で `--machine linux_pc_wl` を WSLg に入れ子で起こすと、Family
   mruby のデスクトップが整数倍で出て、foot の窓がその上に本来の解像度で出る
   (grim の画面で示す)。
3. foot を押してから打った文字が foot に入り、Family mruby の絵を押してから
   打った文字が Family mruby (例: エディタ) に入る。
4. `WLR_RENDERER=pixman` でも 2 が通る。
5. 二つの動作の流れの受け渡しについて、絵が欠けない・止まらないこと、CPU の
   使用率と絵の更新の間隔を測って report に書く (Family mruby が忙しいときに
   foot の打鍵が遅れないか)。
6. 手元の `rake native:core` (wlroots 無し) と `rake native:test`、`rake regress`、
   `rake regress:screens ONLY=native` が通る。

## 止まる条件

- 上の決定と食い違う作りが要るとき (例: 0.19 で必要なものが足りない)。
- `main/` を変えないとできないとき (L9 の範囲に入るとき)。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l8.md) に書くこと

- ビルドの場所の変更 (イメージ・Depends)、手元とコンテナの分け方
- 受け口と機種の定義、動作の流れの受け渡し (ロック・起こし方)
- 入力の行き先の決め方、合成後の画面の撮り方
- 受け入れ条件ごとの結果と、確かめたコマンド・数値
- L9 に向けた見立て (窓の通知を kernel に入れるときの形、Xwayland)、撤回した
  仮説、踏んだ罠、残件
