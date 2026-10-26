# L5a 指示書: (1) の形の WSL2 でできる部分と L4 の残件

> 計画: doc/linux_native/plan.md (段取りの L5a、「キーボード専用のデスクトップ」
> 「パッケージ版の決まり」)。前段の結果: report/l3c.md、report/l4.md

## 目的

L4 の残件を片付け、Cardputer の APPLaunch から本体の画面 (320x170) の
Family mruby を起動できる `.deb` にする。実機での確認は L5b。

## ユーザの決定 (変えない)

- 切り替えの一覧のキーの既定は Ctrl+Tab (設定で変えられる。実機で押せる組は L5b)。
- Esc: 短押しはアプリへ。約 1 秒の長押しは、アプリの中ではアプリを閉じて
  メニューへ、メニューでは Family mruby を終える (APPLaunch へ戻る)。

## 作業の内容

### 1. 一覧・File Manager を開いているときの Esc の長押し

今は、デスクトップがキーボードを持っていると (メニュー・切り替えの一覧・
File Manager・ファイル選択) どれでも終了になる。**終了はメニューだけ**にする。
一覧・File Manager・ファイル選択を開いているときの長押しは、それを閉じて
メニューへ戻る (短押しの Esc と同じ戻り先でよい)。host_task はデスクトップの
中のどの画面かを知らないので、デスクトップが今の画面を kernel か host に
知らせるか、長押しをデスクトップに渡してデスクトップが決めるか、作りを読んで
選ぶ (理由を report に)。

### 2. 一覧からアプリを終えた後に前へ出すもの

今は一覧で前にいるアプリを終えるとメニューに戻る。次のようにする。

- 終えたのが前にいたアプリなら、その前に使っていたアプリ (使った順の
  記録で一番新しいもの) を前に出す。動いているアプリが無ければメニュー。
- 終えたのが後ろのアプリなら、前にいるアプリはそのまま。
- アプリが自分で終わったとき (エディタの終了など) も同じ規則にする
  (今の `keys_after_exit` の「キーボードが渡った先」を優先する動きは残す)。

### 3. Zenoh のルータが空のとき

配布用の設定は `zenoh_router = ""`。今は Asterism のアプリが sim の既定の
名前 (`tcp/zenohd:7447`) に落ちる。native では sim の名前に落ちず、
「ルータが設定されていない」と分かるエラーにする (アプリの画面かログで分かる
こと、固まらないこと)。sim・端末・ブラウザ版の既定は変えない。

### 4. APPLaunch の 320x170 用の入口

- L3a の HDMI 用の入口 (`family-mruby-hdmi`) と同じ形で、本体の画面用の
  小さな起動用の実行ファイル (`--machine cardputer_zero`) と `.desktop` を足す。
  名前の付け方 (どちらを「Family mruby」と呼ぶか) は、本体の画面を主にして
  HDMI 側に HDMI と付ける。
- arm64 の `.deb` に 2 つの入口が入っていることを確認の道具 (deb_check) で確かめる。
  コンテナでは fbdev が無いので、本体の画面の入口は理由つきで断る
  (終了コード 3) のが正しい結果。

### 5. `.app.toml` の 2 つの欄の手引き

`screens` と `needs_pointer` の書き方を、既存のアプリの手引きの置き場所
(`.app.toml` の項目を説明している文書や skill の資料を探す) に足す。
既存の `flash/` のアプリのうち、宣言を足してよいもの (320x170 で読めて、
マウスの要らないもの) を調べ、候補の一覧を report に書く。**宣言の追加は
しない** (対応アプリの組は L5b で実機を見て決める)。

## 触ってよい範囲

- fmruby-core: `main/prebuild_scripts/kernel/` (kernel・system_desktop・
  system_desktop_keys)、`main/kernel/host/host_task.c` の keys の部分、
  Asterism の Zenoh の接続先を決めている所 (native だけに効く形で)、`native/`、
  `config/`、`rakelib/`、`test/`、`doc/` の手引き、`doc/linux_native/report/`。
- family-mruby (ルート): `tools/`、手引きが skill にあるならその資料。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/` の
  アプリ本体と `.app.toml`、plan.md、mouse・端末・sim・ブラウザ版の動き。

## 作業場所と git

- 本体の checkout、両リポジトリとも `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は ESP32-P4 のビルド。自分で `rake clean_all` はしない。
- kernel の Ruby を変えたら、標準 (Spinel のカーネル) と全部 mruby の両方で確かめる。

## 受け入れ条件

1. `--machine sdl_keys` で、切り替えの一覧・File Manager を開いて Esc を長押し
   すると、メニューに戻りプロセスは残る。メニューでの長押しでは今どおり終わる。
2. エディタ → Shell の順に起動し、一覧で Shell (前) を終えるとエディタが前に出る。
   エディタが前のまま一覧で Shell (後ろ) を終えると、エディタが前のまま。
3. native でルータが空のとき、Asterism のアプリが分かるエラーで止まり、全体は
   固まらない。sim の Asterism (`rake regress:live`) は今どおり。
4. `rake native:deb ARCH=arm64` に入口が 2 つあり、deb_check が通る
   (本体の画面の入口は理由つきの終了コード 3)。
5. 1・2 を標準と全部 mruby の両方で。
6. 回帰: `rake regress`、`rake regress:screens` (keys の基準 3 枚を含む) が通る。
   keys の画面が変わったら、変化を report に書き、基準の更新は別のコミットにする。
   kernel を触ったら `rake regress:size` で内蔵 RAM が増えていないこと。

## 止まる条件

- 上の決定と食い違う動きにしないと作れないとき。
- mouse・端末・sim・ブラウザ版の動きが変わる直し方が要るとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l5a.md) に書くこと

- 1 の作りの選び方と理由、2 の使った順の記録の置き場所
- Zenoh の接続先の決め方の変更点
- 入口の名前と中身、deb_check の結果
- 手引きを書いた場所と、宣言の候補のアプリの一覧
- 受け入れ条件ごとの結果と、確かめたコマンド・数値
- 見立てと違った点、撤回した仮説、踏んだ罠、残件
