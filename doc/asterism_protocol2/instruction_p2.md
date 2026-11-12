# P2 指示書: Asterism 本体に protocol 2 を入れる

> 親: plan.md の P2。決まりの正は design.md (本文と「決定」「細部の決定」の表 1〜13)。部品の API は report/p1.md。

## 目的

asterism (CRuby と端末で同じ `mrblib/`、CRuby だけの `lib/asterism/cruby/`) に protocol 2 を入れ、`docs/protocol.md` と
適合の試験を protocol 2 に書き換える。使う側 (asterism-games・console・fmruby-core のアプリ) の入れ替えは P3 なので、ここではしない。

## やること

1. 送る側:
   - `Asterism.publish(name, type: nil, unit: nil, desc: nil, ts: false, encoding: nil)` → 宣言したトピック。`pub.put(value)` で送る。
     MessagePack の値は encoding を付けずに pack して送る。`encoding:` を指定したトピックは、値 (String) をそのまま送る。
   - 部品の `session.publisher` で宣言し、キーは `asterism/<node>/<app>/<name>` (`name` に `/` を許す)。
   - 通し番号を全部のトピックに付ける: attachment は MessagePack の配列 `[seq]`、`ts: true` のトピックは `[seq, 送り手の時刻 (1970 年からのミリ秒の整数)]`。
     seq はトピックごとに 0 から 1 ずつ、2^31 未満で 0 に戻す (design.md の 12)。
   - 宣言の一覧 (`t`・`e`・`u`・`d`・`ts` の短いキー、design.md 3 章) を、変わるたび (宣言・取り下げ) に `asterism/<node>/<app>/@pubs` へ put する。
     つなぎ直したときも put する (今の `@status` の put と同じ時機)。
   - 組み込みの型は番号 (1 number 〜 7 text) を定数で持つ。`type:` にはシンボル (`:vector3`)・番号・独自の型の名前 (文字列) を取る。
   - 独自の型の定義の登録: `Asterism.define_type(name, fields, desc: nil)` のような形 (名前は既存の API の流儀に合わせてよい)。
     形は design.md の 10 (`{"f" => [[名前, 型], ...], "d" => ...}`)。
2. アプリの自己紹介 `asterism/<node>/<app>/@profile` の queryable:
   - 今の `@status` の queryable を置き換える (アプリごとの queryable は `**` とこれの 2 つ。状態が無くても立ててよいか、今の
     「状態があるときだけ」を保つかは、RAM と作りの簡単さで決め、report に理由を書く)。
   - 引数なし → `{"status" => ..., "pubs" => ...}`。`?status`・`?pubs`・`?types` → その 1 つだけ (design.md の 9)。
     値が無いもの (状態を設定していない等) はキーごと省く。
   - `@status` の put は今どおり。
3. 受ける側:
   - `Asterism.subscribe(path) { |s| ... }` (path は `<node>/<app>/<name>`、ワイルドカード可)。包み `s` は `value` (encoding が無ければ
     MessagePack を解いた値、あれば String)・`seq`・`time` (送り手の時刻、無ければ nil)・`encoding` (無ければ `"msgpack"`、design.md の 13)・
     `key`・`node`・`app`・`name`、ルータの時刻 (部品が読めれば)、`lost` (前の値から欠けた数。折り返しとつなぎ直しを design.md 2 章の
     読み方で区別) を持つ。購読の取り消しもできるように。
   - 宣言の一覧を引く: `Asterism.pubs_of("<node>/<app>")` (今の `status_of`・`profile_of` の流儀に合わせる)、型の定義も同様に。
4. 自己紹介 (`asterism/<node>/@profile`) の `protocol` を 2 にする (`Asterism::PROTOCOL`)。
5. `docs/protocol.md` (英語) を protocol 2 に書き換える: 新しい章 (データ・トピックの宣言・アプリの自己紹介・型・時刻)、
   8 章 Status を `@profile` に合わせて直す、7 章の「Room left for later」の記述も。protocol 1 の記述は残さない (互換を考えない方針)。
   `mrblib/profile.rb` などのコメントも合わせる。
6. 試験: `rake test` (CRuby) と `host_test` (picoruby) の両方に、publish → subscribe (seq・time・encoding・lost)、`@profile` の答え分け、
   `@pubs` の put、型の定義、JPEG のような生の値を足す。適合の試験 (`test/protocol/`、samples.json) を protocol 2 に作り直す。
   CRuby と picoruby の間の通しも 1 本 (P1 の `rake test_cruby` の作りが参考になる)。
7. 部品は手元の checkout を使う: asterism の host_test が picoruby-asterism-zenoh をどこから取るかを確かめ、手元 (P1 のコミット済み) を指す。
   CRuby 側の asterism-zenoh は今の gem のままで足りる (P1 の調べ)。

## 触ってよい範囲

- asterism (全体)。picoruby-asterism-zenoh は、P2 で部品の不具合が見つかったときの直しだけ (試験を足して別コミット)。
- fmruby-core は report (`doc/asterism_protocol2/report/p2.md`) だけ。**fmruby-core のソース・ピン・build/ は触らない**。
- 触らない: asterism-games・asterism-console・asterism-zenoh (足りないと分かったら止まって返す)。

## git

- asterism の main に直接コミットしてよい (件名は `<種類>(<場所>): <要約>`、英語、Co-Authored-By)。版と CHANGELOG はまだ上げない (P4)。
- push・gem の公開はしない。

## 止まる条件

- design.md の決まりで書けない所・決まりどうしが食い違う所が出たとき (直し方の候補を書いて返す。自分で決めない)。
- Ruby の API の名前・形のうち、design.md と指示書に無いもので、利用者に見える選択が要るもの (`define_type` の名前程度は既存の流儀で決めてよい。
  引数の意味や既定の動きが変わるものは返す)。
- 部品 (asterism-zenoh・picoruby-asterism-zenoh) に API を足す必要が出たとき。
- ユーザの作業が要るとき。Docker は今この WSL から見えない (fmruby-core の regress:size などは使えない)。

## report (fmruby-core/doc/asterism_protocol2/report/p2.md)

足した API の表 (CRuby・picoruby)、`@profile` の queryable の立て方の判断と理由、試験の数と結果、protocol.md の変更の要約、
1 値あたりに送るバイト数の実測 (キー以外: payload と attachment)、見立てと違った点、撤回した仮説、残件 (P3 で使う側を入れ替えるときの注意)。
