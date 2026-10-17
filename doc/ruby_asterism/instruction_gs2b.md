# GS2b 指示書: console の図で、部屋のゲームホストと常設の卓のホストを見分けられるようにする

> 状態: 指示 | 更新: 2026-10-17 | 図に `games` が 2 つ並び、どちらが部屋のホストでどちらが常設の卓のホストか分からない。ラベルを分ける

前提: asterism-console の lib/bridge/graph.rb (部屋の枠、`add_profiles`)、lib/bridge/profiles.rb、app/javascript/controllers/console_controller.js の `nodeElement` (プロフィールの名前があれば「名前 (ID)」)、lib/relay/settings.rb の `room_hosts` (ASTERISM_ROOM_HOSTS)、docs/rooms.md。asterism-games の lib/asterism_games/host.rb (`net.profile(name: "asterism-games") unless @config["namespace"]`)。report/gs2.md、report/zh1.md (console は部屋の中では見張りしかできず、問い合わせはできない)。

言葉: 文書は常体。asterism-console・asterism-games の文書とコードのコメントは英語。

## 1. 今の様子 (2026-10-17、VPS の図)

- 部屋 05 (Opens: Othello) の枠の中に `games` (卓 `othello1`)、枠の外に `games` (卓 `mahjong1`・`mahjong2`・`othello1`)。どちらも証明書 `games` でつなぐので、ノード ID が同じ。
- 常設の卓のホストはプロフィール (`name: asterism-games`) を出しているはずだが、図は `games` としか出していない (「名前 (ID)」になっていない)。部屋のホストはプロフィールを出していない。

## 2. 作るもの (ユーザの要望: 「games (room 05)」と「games (permanent)」のように分ける)

1. **部屋のホスト**: 部屋の枠の中にいて、ノード ID が ASTERISM_ROOM_HOSTS のものに当たるノードは、図のラベルを `<ID> (room <番号>)` にする (例: `games (room 05)`)。console の側の直し (部屋の中のプロフィールは console が読めないため)。
2. **常設の卓のホスト**: ゲームホストのプロフィールの名前を、常設の卓のホストでは `permanent tables` にする (図は今の決まりで `permanent tables (games)` と出る)。まず、今のプロフィール `asterism-games` が図に出ていない理由を調べる (bridge がプロフィールを取りに行けていない、ACL、取りに行く時機など)。直すのが console の側なら直す。
3. 試験: console の図の試験 (部屋のホストのラベル、部屋の外の同じ ID はそのまま)、ゲームホストの試験 (常設のプロフィールの名前)。
4. 文書: console の CHANGELOG (次の版、0.3.3)・docs/rooms.md の図の説明、asterism-games の README (プロフィール)。VERSION を console 0.3.3、asterism-games 0.7.1 に。

## 3. 受け入れ条件

1. console の試験全部と rubocop、asterism-games の `MJ_GAMES=8 bundle exec rake test` が通る。
2. 手元の構成 (console の deploy/local、ゲームホストの image、部屋 1 つ + 常設の卓) で、図に `games (room NN)` (部屋の枠の中) と `permanent tables (games)` (枠の外) が出ることを写して確かめる。
3. ほかのノードのラベル (端末のプロフィールの名前、ブラウザ版、console 自身) が変わらない。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (main)、asterism-games (main)。fmruby-core は文書 (この指示書の report) だけ (develop に直接)。
- **触らないもの**: zenohd、ACL の作り方 (変更が要るなら止まる)、asterism・fmruby-core のコード、VPS、端末。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。
- 終わったら起こしたもの (deploy/local、試しの image と volume) を名前で止めて消す。前からある image は残す。docker の prune はしない。
- **止まる条件**: ACL・網の決まり・asterism の変更が要る。ほかのノードのラベルの決まりを変える必要がある。
- **report** (`doc/ruby_asterism/report/gs2b.md`、常体): プロフィールが図に出ていなかった理由、直したこと、受け入れ条件の結果と画面、残ったこと。
