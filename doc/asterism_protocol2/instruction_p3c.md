# P3c 指示書: fmruby-core を protocol 2 に入れ替える

> 親: plan.md の P3c。決まりの正は design.md (本文が今の設計、決定 1〜33)。英語の仕様は asterism の `docs/protocol.md`。
> 本体の API と引き継ぎは report/p2b.md・p2c.md・p2d.md (10 章)。レビューの P3c で確かめることは report/review.md 4 章。

## 目的

fmruby-core (ファームウェア・ネイティブ版・sim・ブラウザ版・回帰の道具) を、手元の asterism と picoruby-asterism-zenoh
(protocol 2・接続のオブジェクト・共有の置き場・回の識別子) で動くようにし、端末で成り立つことを数字で確かめる。

## 手元のつなぎ方

`ASTERISM_DIR=/home/kishima/fmrb/family-mruby/asterism`、`PICORUBY_ASTERISM_ZENOH_DIR=/home/kishima/fmrb/family-mruby/picoruby-asterism-zenoh`
を付けてビルドする (Rakefile の `ASTERISM_PIN` まわりの説明)。**ピンのファイル (`lib/add/*_PIN`) は書き換えない** (P4 で公開した版に上げる)。
lib/ を触ったら `rake clean`、ターゲットの切り替えは `rake clean_all`、Linux のビルドは `file build/fmruby-core.elf` で x86-64 を確かめる。

## やること

1. **Link が接続を持つ** (決定 22): `lib/add/picoruby-fmrb-asterism/mrblib/link.rb` が `Asterism.connect` の返す接続を持ち、
   `link.conn` で渡す。つなぎ直すたびに `link_up(link)` が呼ばれ、アプリはそこで `link.conn` に expose・publish・状態・型を置き直す。
   端末の自己紹介は `Asterism.profile_defaults` (fmrb-asterism.rb)。
2. **モジュールの関数を使っている所を全部入れ替える**: 少なくとも次のファイル (grep で漏れを探す)。
   `lib/add/picoruby-fmrb-asterism/mrblib/` (fmrb-asterism.rb・link.rb・table_client.rb)、Shell のコマンド (`shell_commands.rb` の `Asterism.profile` など)、
   `flash/usr/share/asterism/transfer/transfer.rb`・`flash/usr/share/transfer/transfer.app.rb` (`Sender.new(..., net:)`)、
   `flash/app/tool/receiving.app.rb`、`flash/app/game/{board,othello}`、`flash/app/demo/walk_scroller.app.rb`、`flash/app/test/` の Asterism を使うアプリ、
   checkpoint の Asterism での移動 (`amove`)、`tools/regress/` (fake_asterism.rb・checkpoint_asterism.rb・counterparts.rb・transfer_quick.rb)、
   MCP (`tools/mcp/`) と直下の `tools/` で Asterism を読むもの。
3. **値を流しているアプリはトピックにする**: 素の `session.put` でデータのキーに書いている所 (walk_scroller の進み具合、お絵かきなど) は
   `link.conn.publish` に、受けている所は `subscribe { |v, info| }` に。型を付ける (組み込みで足りなければ `define_type`、フィールドの型は名前)。
   卓のクライアントは、欠け・新しい回で呼び出しで取り直す (決定 31)。観戦を読む所があれば `*/@shared/spectate/**` に。
4. **乱数**: 回の識別子は `Kernel#rand` (picoruby-rng)。ESP32・ネイティブ版・sim・ブラウザ版のビルドに入っていることを確かめる。
5. **偽の網** (`tools/regress/fake_asterism.rb`) を接続のオブジェクトの API に合わせる。

## 確かめること (数字で)

- `rake regress` (落ちたものだけ見る)。
- `rake regress:size` (S3・P4): **内蔵 RAM の静的な増分 0**。増えたら `esp_idf_size --files` で増えたファイルを突き止めて report に (P1 の 212 B の例)。
- `rake regress:live`、`rake regress:transfer:quick`、`rake regress:checkpoint` (Asterism の場面を含む)、`rake regress:screens ONLY=web`。
  基準の更新が要る変化は report に書く (更新は親がする)。
- 端末の 1 値あたりの大きさの実測 (ネイティブ版か sim の送り手で、p2・p2d と同じ測り方。見積もりは約 28 B)。
- 受け取れる宣言の大きさ (review 3 章の 3): 卓の多いホストの `@profile?pubs;types` を、ネイティブ版・ブラウザ版が受け取れる上限を測る。
- zenoh-pico で source info が読めるか (読めるかだけ。使わない)。
- NARYAv3 (S3) が TLS 1.3 のルータにつながるか: 実機は無いので、ビルドの設定 (mbedTLS の TLS 1.3) を読んで見込みを書く。
- 通し: ネイティブ版 2 つ・sim・ブラウザ版・CRuby (CLI) の間で、宣言が `asterism topic list` で見え、値が型・通し番号・回の識別子つきで届き、
  欠けが数えられる。手元のルータ (sim の zenohd か regress の自前のルータ)。

## 触ってよい範囲

- fmruby-core (develop)。asterism・picoruby-asterism-zenoh は部品・本体の不具合の直しだけ (試験つき・別コミット。直す前に report に書く)。
- asterism-games・asterism-console は触らない。fmruby-graphics-audio は要るときだけ (要るなら止まって返す)。
- 作業ツリーの既存の未コミットの変化 (`components/` のサブモジュール 2 つ、`wasm/vendor/`、直下のリポジトリの README.md・README.ja.md) には触らない。

## git

- fmruby-core の develop に直接コミット、件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。ピンは変えない。
- 終わったら `rake clean_all && rake build:linux` (手元の asterism を指して) で Linux のビルドに戻し、`file build/fmruby-core.elf` を確かめる。

## 止まる条件

- 内蔵 RAM の静的な増分が 0 にならない。
- 仕様が変わる選択 (アプリの見た目・操作・既定値、Link の使い方の大きな変更)。
- 部品・本体に API を足す必要。
- ユーザの作業 (実機・sudo・usbipd)。

## report

`report/p3c.md`: 入れ替えた所の一覧、Link の形、確かめた数字 (regress の要約、内蔵 RAM、1 値の大きさ、宣言の大きさ、source info、TLS 1.3 の見込み)、
通しの結果、見立てと違った点、撤回した仮説、P4 への注意。
