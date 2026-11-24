# S5b 指示書: Asterism の残り・ROS 2 の橋・卓のゲームの部品を sig に書く

> 2026-11-25 | 親セッションからサブ (Sonnet) への指示。計画は plan.md (5 章の S5、7 章の決定 9・13・14・22)。
> 書き方の正は `sig/README.md`。前の一括は report/s5a.md (同じ進め方。報告の形はこれに合わせる)。S5 の後半で、
> sig を書く最後の一括。

## 目的

通信 (Asterism) の API のうち、実装にあって sig に無いものを埋め、A6b で書いた型を今の決まりに合わせる。
今ある型は `sig/fmrb_asterism.rbs` (A6b。アプリが使う側の 61 メソッド。手本でもある)。

1. **Asterism の gem の、印の無い公開メソッド** (決定 14: 全部載せる): `Asterism::Connection` の残り (明細で 29)、
   `Asterism::Topic`・`Subscription`・`SampleInfo`・`Future`・`Proxy`・`AsyncProxy` の残り、`Asterism::Credentials` (19)、
   `Asterism::Types`・`Pubs`・`Sequences`・`Status`・`SharedSession`・`Profile` ほか、明細
   `tmp/sig_coverage/report.md` の「2. implemented」に `Asterism` で始まるクラスとして残っているもの全部と、
   `Asterism` モジュール自身の関数。
   - **`@api private` と書いてあるメソッドは書かない** (決定 9。数える道具が印を読んで除いている。明細に出ない)。
   - 例外のクラス (`Asterism::Error`・`RemoteError`・`TimeoutError` ほか) は、今の宣言を見て、足りないものを足す。
2. **ROS 2 の橋** (決定 13: 載せる): `Asterism::ROS` と、その下の `Node`・`Publisher`・`Subscription`・`Service`・`Client`・
   `Message`・`Call` ほか (約 70)。`@api private` のものは書かない。
3. **`FmrbAsterism` のモジュール関数** (17。`config`・`read_config`・`hostname`・`credentials_for`・`system_conf` ほか) と
   **`FmrbAsterism::Link` の残り** (7)、`FmrbAsterism::Settings` の残り (決定 14)。
4. **卓のゲームの部品 `FmrbAsterism::TableClient`** (51。麻雀・オセロのアプリが使う)。アプリが呼ぶもの、アプリが
   上書きするもの (呼ばれる側の決まり) を分けて書く。内側 (アプリが呼ばず上書きもしないもの) は載せない候補へ。
5. **今ある型を決定 22 の形に直す**: A6b の時点では「キーワードと省略できる引数は必須引数の形で書き、長文で
   説明する」決まりだった。今は**シグネチャに実装のとおりに書く** (`?` と `name:`)。`sig/fmrb_asterism.rbs` の
   全メソッドを実装と照らし、キーワードと省略できる引数をシグネチャに出す (`Link.new` の `tag:`・`retry_ms:`、
   `expose` の `methods:`、`[]` の `timeout:` など)。長文の「キーワード: ...」の説明は残してよい (省略したときの値)。
   要約 (1 行目) は変えない。直したものは報告に、前の形 → 後の形と根拠の行を並べる。
   - `Proxy` の上の相手のメソッドは method_missing なので型に書けない (A6b の報告のとおり)。そのまま。

## やること (fmruby-core、ブランチ `sig-s5b`)

1. `rake sig:coverage` を流し、`tmp/sig_coverage/report.md` から上のクラスの行を控える (前の数)。
2. **実装を読む** (推測で書かない): `vendor/asterism/mrblib/` (Asterism の gem の Ruby。`asterism.rb`・`ros.rb` ほか)、
   `lib/gems/picoruby-fmrb-asterism/mrblib/` (FmrbAsterism。`link.rb`・`table_client.rb` ほか)、C は
   `vendor/picoruby-asterism-zenoh/` と gem の `ports/`・`src/`。使われ方は `flash/app/` (チャット・AI ヘルパー・麻雀・
   オセロ・アバター・見本の `talk_*`)。`flash/app/test/` は読んでよいが、数える対象ではない。
   - つまずく所: **呼べる場面** (`link_up` の中か、`update` の中で動くブロックか)、**待つ呼び出しと待たない呼び出し**
     (同期の呼び出しがアプリを止める長さ、`async`)、**失敗のしかた** (どの例外か、タイムアウトの既定)、
     **送れる値** (MessagePack で運べるもの)、つなぎ直した後に何をやり直すか。実装とコメントに書いてあることだけ書く。
   - ROS 2 の橋は、相手 (ROS 2 の側) の用意が要ること、メッセージの型の決まり、端末のメモリで扱える大きさを、
     実装に書いてある範囲で書く。
3. **sig に書く**。Asterism と FmrbAsterism は `sig/fmrb_asterism.rbs` (大きくなりすぎるなら、ROS 2 の橋は
   `sig/asterism_ros.rb` ではなく `sig/asterism_ros.rbs`、卓の部品は `sig/fmrb_table_client.rbs` に分けてよい。
   足したファイル名を報告に並べる)。
   - 要約は `日本語 <<en>> English` で 111 バイトまで。素直なものは「例だけ」、つまずくものは型紙の順。
   - 例は同梱のアプリでの実際の使い方に合わせる (39 桁以内)。強調 `**` は 1 行の中で閉じる。
   - 例の中の端末の名前・アドレスは仮のもの (`fmruby-aaa001`、`tcp/192.0.2.2:7447`、`router.example.org`) にする。
4. **確かめる**: `rake ti:test`、`rake regress`、`rake sig:coverage` (対象の「無い」が載せない候補だけになる。
   「3. doc gaps」は全部 0 のまま)。`rake sig:coverage:update` で基準を更新し、別コミットにする。型データベースの
   各領域の大きさの前後を控える (シグネチャと要約の領域は 24 ビットになったので、上限は気にしなくてよい。
   クラスの数は上限 255 なので、200 を超えそうなら報告の先頭に書く)。**ビルドと sim の確認はしない** (親が検収で行う)。

## 報告に必ず書くこと (doc/sig_coverage/report/s5b.md)

1. **根拠の表**: 書いたメソッドごと (クラスが大きければ仲間ごと) に、読んだ実装の `パス:行`。失敗のしかた・
   呼べる場面を書いたものは、その根拠の行も。
2. 書いた数 (クラスごと)、素直 / つまずくの内訳。足した sig のファイル名。
3. **直した既存のシグネチャ** (前 → 後、根拠)。5 の作業の全部。
4. **載せない候補** (内側と判断したもの。クラス#メソッドと理由)。迷ったものは別に並べる。
5. アプリが使っているのに実装に見つからなかったもの。
6. 数える道具の前後の数、型データベースの大きさの前後 (クラスの数も)。
7. **親が sim で確かめるとよい項目 10〜15 個**。sim にはルータ (zenohd) が上がっているので、つなぐ所まで確かめられる。
   シェルの irb に打つ式 → 期待の値か例外、または、エディタに打つ行 → Ctrl+E で指摘されない / される。
   1 行 60 文字以内。相手の端末が要るものは、要ると書く。
8. 読んでいて見つけた実装の不具合の疑い (直さずに書く)。確かめていないこと。迷った所。

## 範囲と止まる条件

- 触ってよい: `sig/fmrb_asterism.rbs` と、分けるなら `sig/asterism_ros.rbs`・`sig/fmrb_table_client.rbs` (新規)、
  `sdk/` (境界の作り直しが要るときだけ)、`tools/sig_coverage/baseline.json`、`doc/sig_coverage/report/s5b.md`、
  `rake docs:index` による `doc/README.md`。
- 変えない: 実装 (`vendor/` を含む)・アプリ・上の一覧に無い sig・`sig/README.md`・plan.md・`tools/` の道具と ignore.yml・
  型エンジンの試験の期待値 (試験が落ちたら止まって報告)。既にある宣言の要約。
- ビルドをしない (sim・ESP32・ブラウザ版・ネイティブ版とも。`rake clean`・`rake build:*` を流さない)。
- 量が多い (300 メソッド前後)。まとまりごと (Asterism の核 / ROS / FmrbAsterism / TableClient / 既存の直し) にコミットする。
  時間が足りなくなりそうなら、済んだまとまりまでをコミットして、残りを報告に書いて返す (中途半端な編集を残さない)。
- push しない・マージしない。ユーザに質問しない。実在の値 (ドメイン・IP・ノード名・鍵) を書かない。
  コメントとコミットは英語、報告は日本語 (常体)。
