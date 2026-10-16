# GS1 指示書: asterism-games の枠組みを切り出し、麻雀を載せ直す

> 状態: 指示 | 更新: 2026-10-17 | games_showcase.md の GS1。ゲームに依らない部分を枠組みとして切り出し、麻雀をその上に載せ直す。Asterism らしい書き方のうち小さいもの (caller: :from、Net の書き方、プロフィール) を土台に入れる。見た目と動きは変えない

前提: games_showcase.md (目的・方針・Step)、asterism-games の README とコード全体 (host.rb、rooms.rb、store.rb、config.rb、mahjong/、bin/、deploy/vps/)、asterism の README (portable API、CRuby API の Net、Who called の `caller: :from`、Profiles)、report/mj3.md・mj4.md・mj5.md・g2b.md (部屋・常設の卓・席・見張り)。

言葉: 文書は常体、「端末」「確認」「Step」。技術の用語は例えにしない。asterism-games の文書とコードのコメントは英語。

## 1. 決まっていること (2026-10-16〜17 ユーザ)

- asterism-games は Asterism の見本。枠組みは将来 gem に、ゲームはアプリと image に分ける (GS7)。GS1 では同じリポジトリの中で、gem に切り出せる形 (ディレクトリと依存の向き) にしておく。
- 麻雀は riichi_engine の Fork に頼るので、枠組みからは riichi_engine に依存しない。
- 手牌のような席だけの情報は、今どおり呼び出しの答えで渡す。
- 部屋の守り (席の証明書、ゲームホスト自身の証明書 `games`)、常設の卓、家の LAN の卓は今どおり。

## 2. 作るもの

1. **枠組み** (例: `lib/asterism_games/` の下を、ゲームに依らない部分と `mahjong/` に分ける。名前は任せる): 部屋の監督、常設の卓、保存と再開 (Store)、状態の発行、見張り (`GET /status`)、設定、ゲームの基底 (出すメソッドとその引数の数、呼び手、席・観戦・公開の見え方、時間の刻み、保存の形)。ゲームは基底を継いで、ルールと出すメソッドだけを書く。枠組みのファイルは riichi_engine と麻雀を require しない。
2. **Asterism らしい書き方 (小さいもの)**:
   - 呼び手を `Asterism.expose(..., caller: :from)` の `from:` で受ける (今の `Asterism.current_call` を読む `caller_node` をやめる)。呼び出しの引数の数 (`methods:` の Hash) は今どおり守る。古い卓のアプリ (端末やブラウザ版の今の版) からの呼び出しがそのまま通ること。
   - CRuby の `Asterism.connect(...) do |net| ... end` (Net) で接続・expose・受け手を書く。ゲームの進行 (時間切れ・CPU・状態の発行) の輪は自分で持つ。つなぎ直し (`reconnecting?`・`lost_reason`) の扱いは今どおり。
   - `Asterism.profile` に卓の数など (見張りと同じ数だけ。名前・ノード・鍵は載せない) を載せる。
3. **麻雀を載せ直す**: 見た目・動き・見え方の鍵・1 通の大きさを変えない。
4. **文書**: README の冒頭を「Asterism の見本」の説明に。枠組みとゲームの関係、新しいゲームの書き方 (短い例: 何を継いで何を書くか)。

## 3. 受け入れ条件

1. asterism-games の `MJ_GAMES=8 bundle exec rake test` (今 141 件) が全部通る。足した試験も通る (枠組みの試験: 小さな試しのゲームを枠組みの上に書き、部屋・常設・保存と再開・見張りが効くこと)。
2. 呼び出しの互換: 今の卓のアプリ (fmruby-core develop の mahjong.app.rb) から、家の LAN の卓・常設の卓・部屋の卓のどれでも座って打てる (ブラウザ版の web_* か、CRuby の試しの客で。部屋は console の deploy/local と席の証明書で)。古い呼び方 (今の引数の形) がそのまま通る。
3. 1 通の最大とゲームホストのメモリ (RSS) が前と同じくらい (数字を前後で出す)。
4. 枠組みのファイルが riichi_engine と `mahjong/` に依存していないことを、試験か検査で示す (例: 枠組みだけを読み込んで試しのゲームが動く)。
5. ボット (`bin/mahjong-bots`) と、VPS 用の一式 (`deploy/vps/`) が今どおり動く (image を作って compose で起こし、healthy まで)。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core は文書 (この指示書の report) だけ (develop に直接、または feature/gs1。コードは変えない)。asterism は読むだけ (API が足りなければ止まって返す)。
- **触らないもの**: riichi_engine の Fork、asterism-console、VPS、端末。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。
- 公開の文書・コミットに実在の IP・ドメイン・端末 ID・鍵を書かない。コミットの前に鍵 (`~/.openai_key`) と実在の値の検査をする (どちらも 0)。
- 終わったら起こしたもの (deploy/local の一式、試しの image と volume、開発サーバ、ブラウザ、sim) を名前で止めて消す。docker の prune はしない。
- **止まる条件**: 麻雀の見た目や動きを変えないとできない。古い卓のアプリとの互換が保てない。asterism の API が足りない (`caller: :from` や Net で今の動きが書けない)。仕様を決める選択が要る。
- **report** (`doc/ruby_asterism/report/gs1.md`、常体): 枠組みの形 (ファイルと依存の向き)、書き方の変更 (前と後のコードの抜粋)、受け入れ条件ごとの結果と数字、GS2 (オセロ) に向けて分かったこと、残ったこと。
