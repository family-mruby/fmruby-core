# P3 指示書: 使う側を protocol 2 に入れ替える

> 親: plan.md の P3。決まりの正は design.md (決定 1〜17)、本体の API と注意は report/p2.md (9・10 章)、部品は report/p1.md。

## 目的

protocol 2 の asterism (手元の checkout、P2 のコミット済み) を使うように、使う側をすべて入れ替える。protocol 1 と 2 は混ぜられないので、
入れ替えた後は全部が protocol 2 で話す。公開 (push・gem・VPS・ブラウザ版の配置) は P4 なので、ここではしない。

P3 は 3 つに分ける。**P3a と P3b は別のサブが並べて進め、P3c は両方が終わってから**。

## 共通

- 手元の checkout をつなぐ: asterism は `/home/kishima/fmrb/family-mruby/asterism`、picoruby-asterism-zenoh は隣の checkout。
  使う側の Gemfile が rubygems の版を指しているときは、**開発中だけ手元を指す方法** (環境変数で path に切り替える Gemfile の分岐など) を
  選び、その方法と、P4 で版を上げたときにどう戻すかを report に書く。手元の絶対パスをコミットしない。
- 値を流しているところは `Asterism.publish` / `topic.put` に、受けているところは `Asterism.subscribe { |v, info| }` に。
  素の `session.put` / `session.subscribe` を Asterism のデータのキーに使っているところは残さない (Zenoh を直接使う理由があるものは report に理由)。
  トピックには型を付ける (組み込みで足りなければ `define_type`)。
- `@status` に問い合わせているところは `@profile?status` (`Asterism.status_of`) に。自己紹介の `protocol` が 2 であることを前提にしてよい。
- 決まりの穴・仕様が変わる選択 (画面に出るもの・操作・既定値が変わるもの) は止まって返す。
- コミットは各リポジトリの main (fmruby-core は develop) に直接、件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。
  **版・CHANGELOG は上げない。push しない。**
- 実在の IP・ドメイン・SSID・パスワード・鍵を書かない。`~/asterism_keys/` の中身を出さない。

## P3a: asterism の CLI・MCP と asterism-games

- asterism: `lib/asterism/cli/` (survey・nouns・mcp)。`asterism_values` が encoding と attachment を読み、JPEG のような生の値も扱えるように。
  CLI と MCP で、アプリの流しているトピック (`pubs_of`) と型を見られるように (コマンド・道具の名前は既存の流儀で。新しい名前が要るなら案を書いて
  そのまま進めてよいが report に明記)。
- asterism-games: 卓の状態・観戦・お絵かきの put と、端末の側・遊ぶ側 (play)・実況 (commentate) の受け取り。卓の状態の型を `define_type` で定義する。
  asterism-games の試験を通す。
- 確認: asterism の `rake test` 一式、asterism-games の試験、ゲームホストと `asterism-games play` (CRuby どうし) で 1 局の通し。

## P3b: asterism-console

- 状態の読み取り (`lib/bridge/statuses.rb` ほか) を `@profile` に。図にアプリの流しているトピックと型を出す (`@pubs` の put を購読し、
  後から来たときは `@profile?pubs`。出し方は今の図の流儀で)。
- 記録 (`lib/bridge/recorder.rb`・`lib/bag/`): MCAP の message_encoding に encoding (無ければ `msgpack`)、sequence に通し番号、
  publish_time に送り手の時刻 (無ければルータのタイムスタンプ)、schema に宣言の `t` と型の定義 (design.md 5 章)。再生も合わせる。
- ACL (`lib/relay/cloud_config.rb` ほか): `@profile` の問い合わせと queryable、`@pubs` の put と購読を、今の `@status` の規則と同じ考えで足す。
  `@status` の queryable の規則は不要になる。
- console の試験を通す (Docker が要る試験は Docker で。VPS には触らない)。

## P3c: fmruby-core (P3a・P3b の後)

- `ASTERISM_DIR`・`PICORUBY_ASTERISM_ZENOH_DIR` で手元を指してビルドする (linux sim・ネイティブ版・ブラウザ版、esp32 は S3 と Tab5 のビルドが通るまで)。
- 端末のアプリ・サービス・ライブラリのうち、Asterism のデータのキーに素の put/subscribe を使っているもの (例: `flash/app/demo/walk_scroller.app.rb`、
  お絵かき `flash/app/game/board/board.app.rb`、卓のクライアント、転送のアプリ、checkpoint の移動、`main/lib` 側の Asterism の包み) を入れ替える。
  端末内のアプリどうしの pub/sub (`@app.publish`・`ctx.publish`) は Asterism ではないので対象外。
- 回帰の偽の網 (`tools/regress/fake_asterism.rb`) を protocol 2 の API に合わせる。MCP (`tools/mcp/`) とルートの tools/ に Asterism を読むものがあれば合わせる。
- 確認: `rake regress`、`rake regress:size` (内蔵 RAM の増分 0)、`rake regress:live`、`rake regress:transfer:quick`、`rake regress:checkpoint`
  (Asterism の場面を含む)、`rake regress:screens ONLY=web` (ブラウザ版)。基準の更新が要る変化は report に書き、更新は親がする。
- 通し: ネイティブ版 2 つ・sim・ブラウザ版・CRuby (CLI) の間で、宣言が `@profile?pubs` で見え、値が型・通し番号つきで届き、
  欠けが数えられることを確かめる (手元のルータ。sim の zenohd か、regress の自前のルータ)。

## 止まる条件

- 決まりの穴・決まりどうしの食い違い。仕様が変わる選択。部品・本体に API を足す必要 (asterism の不具合の直しは試験つきで別コミットにしてよい)。
- 内蔵 RAM の静的な増分が 0 にならないとき。
- ユーザの作業 (sudo・実機・VPS・鍵) が要るとき。

## report

`report/p3a.md`・`report/p3b.md`・`report/p3c.md`: 変えた所の一覧 (リポジトリ・ファイル・何を)、開発中の手元のつなぎ方と P4 での戻し方、
試験の数と結果、通しの結果、見立てと違った点、撤回した仮説、残件 (P4 で公開するときの順序の注意: どの gem をどの順に上げるか、
console と VPS のルータの ACL を同時に変える必要など)。
