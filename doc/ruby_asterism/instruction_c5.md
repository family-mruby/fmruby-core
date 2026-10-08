# Asterism C5 指示書: CRuby らしい API

> 状態: 指示 | 更新: 2026-10-08 | CRuby 版に、ブロック・スレッド・Enumerator・パターンマッチを使う Ruby らしい API を足す。機体 (mruby) と同じポーリングの API はそのまま残し、その上に CRuby だけの層として載せる

ユーザの依頼 (2026-10-08): 「CRuby らしい使い方ができる API などを足してほしい」。形は design.md 6 章の案を基にする。

## 0. 方針

- **今の API は変えない**: `poll`、`each_pending`、`Asterism.poll` など。機体と同じコードが CRuby でも動くことは保つ。
- **新しい API は CRuby だけの層**: asterism リポジトリの `lib/asterism/` の CRuby 専用のファイルに置く。
  - `mrblib/` (機体と共有) には足さない。
  - 機体側でも使える小さな追加があれば、別に提案として report に書く (C5 では入れない)。
- **スレッドの形**: セッションごとに受け取りのスレッドを 1 本持つ (使う人が始めたときだけ)。
  - ブロックの呼び出しはそのスレッドで行う。
  - zenoh-c のコールバックで Ruby に触らない作り (C1) は変えない。受け取りのスレッドは、今の `each_pending` で取り出して配る。
  - ブロックの中で重い処理をすると、ほかの受け取りが遅れることを README に書く。
- **待つ呼び出しとの関係**: 受け取りのスレッドが動いている間は、答えを待つ呼び出し (`proxy.method`、`client.call`) は自分で poll せず、受け取りのスレッドが届けるのを待つ (Queue / ConditionVariable)。
  - 動いていないときは今どおり自分で poll する。
  - 2 つのスレッドが同時に poll しないよう、錠で守る。
- 版は `0.2.0`。asterism-zenoh を直す必要があれば同じく `0.2.0` にし、asterism の依存を合わせる。公開 (`gem push`) はしない。

## 1. 足す API (目安。名前は Ruby の慣習に合わせて決めてよい。report に一覧を書く)

### 1.1 Zenoh の層 (`Asterism::Zenoh`)

```ruby
Asterism::Zenoh.open("tcp/192.0.2.2:7447") do |s|   # ブロックを抜けたら閉じる
  s.subscribe("home/**") { |sample| puts "#{sample.key} = #{sample.payload}" }  # 受け取りのスレッドで呼ばれる
  s.queryable("home/pc/status") { |q| q.reply("ok") }
  s.put("home/pc/hello", "hi")
  s.get("home/**/status").each { |reply| p reply }   # 全部届くか時間切れまでの Enumerator
  sub = s.subscribe("sensor/temp")                    # ブロック無しなら今どおり
  sub.each.lazy.map { _1.payload.to_f }.first(3)      # 届くのを待つ Enumerator
  s.liveliness_watch("asterism/**") { |key, alive| ... }
  s.run                                              # 受け取りのループをこのスレッドで回す (Ctrl-C で抜ける)
end
```

- `Sample` (`key`、`payload`、`attachment`) と `Reply` は小さな値オブジェクト。`deconstruct_keys` でパターンマッチできる。
- `s.start` / `s.stop`: 受け取りのスレッドを裏で回す。`run` は前で回す。
- 例外: ブロックの中で起きた例外は受け取りのスレッドを止めない。`on_error` で受け取れる。何もしなければ警告を出す。

### 1.2 オブジェクトの網 (`Asterism`)

```ruby
Asterism.connect("tcp/192.0.2.2:7447", node: "mypc", app: "demo") do |net|   # 抜けたら閉じる
  net.expose("screen", Screen.new, methods: [:say])
  net.on_join  { |node| puts "joined #{node}" }       # 機体が現れた (生存の監視)
  net.on_leave { |node| puts "left #{node}" }
  board = net["fmruby-aaaaaa/demo/screen"]
  board.say("hello")
  net.each("*/demo/info").map(&:status)               # Enumerable
  net.run                                             # 公開したものに答え続ける
end
```

- 今の `Asterism.connect` (ブロック無し) と `Asterism.poll` はそのまま動く。

### 1.3 ROS 2 の層 (`Asterism::ROS`)

```ruby
Asterism::ROS.connect("tcp/192.0.2.2:7447", domain: 0) do |ros|
  node = ros.node("ruby_talker")
  chatter = node.publisher("/chatter", "std_msgs/msg/String")
  chatter << { data: "hello from Ruby" }
  node.subscribe("/odom", "nav_msgs/msg/Odometry") { |odom| puts odom.pose.pose.position.x }
  node.every(0.1) { chatter << { data: Time.now.to_s } }           # タイマー
  node.service("/add_ruby", "example_interfaces/srv/AddTwoInts") { |req| { sum: req.a + req.b } }
  p node.call("/add_two_ints", "example_interfaces/srv/AddTwoInts", a: 1, b: 2).sum
  node.topic("/scan", "sensor_msgs/msg/LaserScan").each.lazy.first(1)   # Enumerator
  ros.spin                                                          # Ctrl-C で抜ける
end
```

- メッセージの値オブジェクトに、CRuby でだけ `deconstruct_keys` を足す。`case msg in {linear: {x:}}` が書ける。
  - 共有のクラスは書き換えず、CRuby 専用のファイルで足す。
- 型は名前の文字列でも渡せる (R3 と同じ)。同梱に無い型は、変換器で作ったファイルの置き場所を指定して読む方法を README に書く。

## 2. 試験と確かめ

1. CRuby の試験 (asterism リポジトリの `rake`) に、新しい API の試験を足す。
   - 2 つのセッションの間でブロックの受け取り、Enumerator、`run` / `start` / `stop`、例外の扱い。
   - 受け取りのスレッドが動いているときの、待つ呼び出し (オブジェクトの網と ROS のサービス)。
   - スレッドの後始末 (ブロックを抜けたら止まる、残らない)。
2. 今の試験 (`test:msgs`、`test:objects`、asterism-zenoh の `rake`) が通る。
3. 実機と ROS 2 で 1 回ずつ:
   - 新しい API で書いた例から、P4-Nano の asterism_demo の `screen.say` を呼ぶ。機体からの呼び出しに `net.run` で答える。
   - ROS 2 の `ros2 topic pub` を `node.subscribe { }` で受け、`ros2 topic echo` で `chatter <<` が読める。
4. `examples/` を新しい API で書き直す (今のポーリングの例も 1 つ残す)。README に「Ruby らしい使い方」の節を書く。

## 3. 範囲・決まり

- **触ってよいもの**: asterism と asterism-zenoh のリポジトリ (`main` にコミット、push は親が行う)、fmruby-core の `doc/ruby_asterism/report/c5.md`。
  - fmruby-core のブランチは `feature/asterism-c4` から `feature/asterism-c5` を切る。
- **触らないもの**: `mrblib/` (共有の層)。直さないと成り立たない場合は止まって返す。fmruby-core のビルドの仕組み、`.env`、sdkconfig。
- **公開・配布**: rubygems.org への公開はしない。
- **機体**: P4-Nano。IP は親が渡す。文書には本当の IP・機体の名前を書かない。
  - ミュート中。
  - 親の capture が向いている (`serial_start` を呼ばない)。
  - 焼き直しは要らない見込み。
- **zenohd・ROS 2**: 重ね合わせのファイルで上げ、終わったら `down`。
- **止まる条件**:
  - 共有の層や C 拡張に大きな変更が要るとき。
  - 利用者から見た今の動きを変える必要があるとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - API の一覧。
  - スレッドの形と錠の扱い。
  - 試験と確かめの結果。
  - 機体側 (mruby) に持っていける部分の提案。
