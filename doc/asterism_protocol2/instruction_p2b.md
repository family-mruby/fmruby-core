# P2b 指示書: 接続をオブジェクトにする (1 本 = 1 つのアプリ)

> 親: plan.md の P2b。決まりは design.md の決定 19・21 (と 1〜20)。今の API は report/p2.md・p3a.md。

## 目的

Asterism の接続をオブジェクトにし、1 つのプログラムが複数の接続 (= 複数の本物のアプリ) を持てるようにする。
モジュールの関数 (`Asterism.publish` など) は無くし、全部を接続のメソッドにする。

```ruby
game = Asterism.connect(node: "host", app: "game")
game.publish("t1/state", type: "g/state").put(v)
game.subscribe("other/app/imu") { |v, info| ... }
game.status = {"tables" => 2}
spec = Asterism.connect(node: "host", app: "spectate")
spec.publish("t1/state", type: "g/state")
game.close
```

P2b は 2 つに分ける。**P2b-1 (asterism) が終わってから、P2b-2 (asterism-games と asterism-console) を並べて**進める。

## P2b-1: asterism

1. `Asterism.connect(...)` が接続のオブジェクト (クラス名は既存の流儀で。案 `Asterism::Connection`) を返す。今モジュールが持っている状態
   (セッション・公開したオブジェクト・状態・自己紹介・トピック・型・購読・生存の印・呼び出しの待ち・資格情報・再接続) は全部接続が持つ。
   プロセス全体で共有するもの (MessagePack の表、型の定数、encoding の表など、状態でないもの) はモジュールに残してよい。
2. 今のモジュールの関数で、アプリが使うものは全部接続のメソッドにし、モジュールからは消す (互換の別名は残さない)。`poll` は接続ごと
   (`conn.poll`)。複数の接続を 1 回で回す手段 (`Asterism.poll_all` のような) が要るなら足してよい。
3. 1 つのプロセスで同じ `node` の別の `app` を 2 本つなげる。重複の確認 (protocol.md 6.3) は `node/app` の組ごと。`@profile`
   (ノードの自己紹介 `asterism/<node>/@profile`) は、同じノードの複数の接続のどれが答えても同じ中身になるように (案は report に)。
4. CRuby: `Asterism::CRuby` の `net` (LOCK・受信のスレッド) を接続のオブジェクトにそろえる。CRuby だけのものはそのまま CRuby の層に。
5. 端末 (picoruby): 同じ `mrblib/` で複数の接続が動くこと。部品 (picoruby-asterism-zenoh) のセッションは既にオブジェクトなので、
   部品に全体の状態が無いかを確かめる (あれば止まって返す)。端末で 1 本ごとに使う資源 (TLS・ルータの席・queryable と受け取りの列の RAM) を
   report に数字で書く (P1 の測り方で)。
6. `Asterism::ROS`・`lib/asterism/transfer`・CLI (`lib/asterism/cli`)・MCP を合わせる。
7. `docs/protocol.md` (アプリ = 接続 1 本、1 つのプログラムが複数の接続を持てること、端末での重さ) と README (全部の例) を直す。
8. 試験: 既存の全部を新しい書き方に。複数の接続を 1 プロセスで持つ試験 (CRuby・mruby、`node` が同じで `app` が違う 2 本が互いに
   呼べる、片方を閉じても片方が生きる、重複の確認) を足す。適合の試験・`test:cross` も。

## P2b-2: asterism-games と asterism-console (P2b-1 の後、別のサブが並べて)

- asterism-games: 新しい書き方に入れ替え、観戦を `spectate` の 2 本目の接続にして、観戦のトピックを宣言する (卓の型を使う)。
  ルームの観戦も同じ形で宣言できるなら入れる (P3a の report 9 章の穴)。`docs/install.md` の `games-host` の役割に `spectate` の
  `@profile`・`@pubs`・生存の印が要るなら足す。試験と 1 局の通し (`asterism topic list` に観戦のトピックが出る)。
- asterism-console: 新しい書き方に入れ替える。`spectate` がアプリとして図に出ること。台帳の自動の直し (P3b の 9 章) に `spectate` の
  行が要るなら足す。試験。

## 共通の決まり

- 各リポジトリの main に直接コミット、件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。版・CHANGELOG は上げない。push しない。
- `ASTERISM_CREDENTIALS` を外して試験する。実在の値を書かない。
- 止まる条件: 決まりの穴、仕様が変わる選択 (API の名前のうち、design.md の例に無い大きなもの・既定の動きが変わるもの)、部品に全体の状態が
  見つかったとき、ユーザの作業が要るとき。

## report

`report/p2b.md` (P2b-1)、P2b-2 は `report/p3a.md`・`report/p3b.md` に章を足す。消した関数と移した先の対応表、1 本ごとの資源の数字、
試験の数、見立てと違った点、P3c (fmruby-core の端末のアプリに接続をどう渡すか) への材料。
