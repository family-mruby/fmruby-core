# 実装指示書 F1b: F1 の残り (基準の表を小さく、回帰の通しに入れる、止める入口を増やす)

`doc/checkpoint/plan.md` (決まったこと 2026-11-03、F1 の検収)、`doc/checkpoint/report/f1.md` (特に §3.1 基準の
内訳)、`doc/checkpoint/instruction_f1.md`、mruby-checkpoint の `doc/plan.md` (D1〜D16) を先に読む。

## 1. 基準の表を小さくする (mruby-checkpoint の gem)

F1 の計測で、申告したアプリは基準だけでネイティブ版 (64 bit) の 1 VM あたり約 1 MB (1,018,640 B、名前
4,041 個) をアプリのプールから使った。調査の見積もり (数十〜100 KB) より一桁大きい。

- まず内訳を測る: 何に何バイト使っているか (名前の文字列、表の項目、生かしておく Array、snapshot の写し、
  ハッシュ表の空き、allocator の端数)。推測で直さない。
- そのうえで小さくする (候補: 名前の文字列を 1 つの塊にする、表の空きを減らす、生かしておく Array と
  表を兼ねる、32 bit で効く形にする)。正しさ (mruby-checkpoint の `rake test`・`test:asan`・
  `test:stress`、fuzz) は落とさない。
- 目標は数字で report に書く (前後、64 bit と 32 bit)。flash に置く形 (D12 の (c)) は F6 の前に別の段で
  作るので、ここではやらない。ただし、flash に置く形に移りやすい作りにする (名前の表がファームごとに
  固定のデータで作れること)。

## 2. `rake regress:checkpoint` を `rake regress:full` に入れる

- `doc/regression/usage.md` の一覧と所要時間も直す。

## 3. 止める入口を増やす

- **開発の道具**: `tools/fmrb_native.rb` に `checkpoint` (と、イメージから起動し直す操作) を足し、MCP の
  `native_app` (family-mruby のリポジトリ直下の `tools/mcp/`) からも使えるようにする。MCP のツールの
  description に使い方を書く。
- **Shell のコマンド**: 端末の Shell から、動いているアプリを止めてイメージにし、イメージから起動し直せる
  コマンド (名前と形は既存の Shell のコマンドに合わせて決めてよい)。
- **アプリが自分で頼む API**: アプリの Ruby から「自分を止めてイメージにして」と頼める API
  (`FmrbApp` の側。`on_event` の中では書き出せないので、要求として受けて `_spin` の外で書く形)。
  rbs (`sig/fmrb_app.rbs`) と文書も更新する。
- どの入口も、F1 と同じ道筋 (カーネル → アプリの `_spin` → セーフポイント) に乗せる。入口ごとに別の
  仕組みを作らない。
- 試験: `rake regress:checkpoint` に、新しい入口からの場面を足す (少なくともアプリ自身の API)。

## 確かめること

- `rake regress:checkpoint` (標準と全部 mruby の両方)、`rake regress`、`rake regress:run`、
  `rake regress:screens ONLY=retro,modern,native,native-keys` (ブラウザ版は流さない)、共有のコードを
  変えたら `rake regress:size` (内蔵 RAM +0)。
- mruby-checkpoint の `rake test`・`test:asan`・`test:stress`。

## 触ってよい範囲・止まる条件・守ること

- fmruby-core は本体の checkout の `feature/linux-native` にコミット (push しない)。mruby-checkpoint は
  worktree のブランチにコミット (push しない。固定点は親が push してから合わせる。開発中は
  `MRUBY_CHECKPOINT_DIR`)。family-mruby のリポジトリ直下 (`tools/mcp/`) も触ってよい (コミットまで)。
- 止まる条件: 利用者から見た動きが変わる直し方 (既存の Shell のコマンドや `FmrbApp` の API の意味を
  変えるなど)、D1〜D16 と食い違う事実、スケジューラの作りを変える必要。
- 文書は常体、定着したカタカナ語を使う。コミットする文書にローカルの絶対パスを書かない
  (コミットの前に、差分をローカルのパスで検索して確かめる)。
