# Asterism: 全体の設計 (gem の構成・環境ごとの接続・キー空間・ROS 2)

> 状態: 計画済 | 更新: 2026-10-07 | Ruby から Zenoh を使い、マイコン・ロボット・サーバ・ブラウザを一つの Zenoh の空間でつなぐ。gem の分け方、環境ごとの接続層、キーの付け方、ROS 2 との通信、ブラウザ版の識別をまとめる。段階と実装の記録は plan.md

凡例: **決定** = 方針として確定、**案** = 検討中 (推奨案)。

## 1. 背景

- Zenoh の公式の言語の対応は Rust (本体)、C (zenoh-c / zenoh-pico)、C++、Python、Kotlin、Java、TypeScript。
  Ruby の公式のバインディングは無い。実用的な非公式の gem も見当たらない (公開の前に `gem search zenoh` で再確認する)。
- Zenoh はプロトコルで相互に通じるので、Ruby 版も他の言語のノードや ROS 2 (rmw_zenoh) とそのまま話せる。
- Asterism と Family mruby は別のプロジェクト (**決定**、2026-10-07)。Asterism の gem は Family mruby に依存せず、
  Family mruby は Asterism を使う側の一つ。

## 2. gem の名前と構成

### 名前

- RubyGems.org の `asterism` は取得済み (**決定**、2026-10-06。0.0.0 は名前の確保だけ)。
- Zenoh のバインディングは `asterism-zenoh`、モジュールは `Asterism::Zenoh` (**決定**、2026-10-07)。
  - 商標や、将来の公式の `zenoh` gem とぶつからない。非公式であることが名前から分かる。
  - `require "asterism/zenoh"`。短く書きたい人のために、任意の `require "asterism/zenoh/global"` で
    `Zenoh = Asterism::Zenoh` を定義する (既定では定義しない)。
  - mruby / PicoRuby 版も同じ `Asterism::Zenoh` を使い、最上位の `Zenoh` は作らない (**決定**。まだ公開して
    いないので互換は残さない)。
  - 将来 Eclipse Zenoh 側と合意できれば、`zenoh` という名前を改めて考える。
- 以前、通信の層の名前として「Silk」を予約していた (naming.md) が、名前は `asterism-zenoh` に置き換える。

### gem の分け方 (案)

| gem | 役割 | 実装 |
|---|---|---|
| `asterism-zenoh` | Zenoh のバインディング。セッション、put / subscribe、get / queryable、liveliness、attachment。ROS は知らない | 環境ごと (3 章) |
| `asterism` の本体 (オブジェクトの層) | 遠くのオブジェクトの代理 (A1)、公開、一覧。値は MessagePack | 純 Ruby |
| `asterism-cdr` | CDR の符号化 (先頭の 4 バイトの印、揃え、バイト順) | 純 Ruby |
| `asterism-msgs` | `.msg` / `.srv` から Ruby のクラスと CDR の変換を作る。型のハッシュ (RIHS01)。よく使う型は作って同梱 | 純 Ruby |
| `asterism-ros` | rmw_zenoh 互換の層。キー、attachment、liveliness、サービスの対応付け、ROS らしい API | 純 Ruby |
| `asterism` (全部入り) | 上の全部を依存に持ち、`require "asterism"` でまとめて読む。文書とサンプルの置き場 | - |

- 共通の部分が大きくなったら `asterism-core` に切り出し、依存の循環を避ける。
- CDR・メッセージの型・ROS の層・オブジェクトの層を純 Ruby にしておくと、PicoRuby やブラウザ版でもそのまま使える。
  そのために、**環境ごとの `asterism-zenoh` は Ruby から見える API をそろえる** (**決定**、3 章)。
- 使い分け: Zenoh だけなら `asterism-zenoh`、CDR だけなら `asterism-cdr`、ROS とつなぐなら `asterism`。

## 3. 環境ごとの接続層

接続層 (`asterism-zenoh`) だけを差し替え、上の層は共通にする。

| 環境 | 接続層 | 状態 |
|---|---|---|
| mruby / PicoRuby (Family mruby の機体・sim) | zenoh-pico の mrbgem (`lib/add/picoruby-zenoh`) | Z1-Z3 で実装済み (P4 実機・sim) |
| ブラウザ (Family mruby の wasm 版) | 同じ mrbgem + zenoh-pico (Emscripten) | 案 (8 章) |
| CRuby | Rust 本体を rb-sys + Magnus で包む、または zenoh-c を C 拡張で包む | 案 |

### Ruby から見える API をそろえる (決定)

- 受信は**ポーリングで取り出す**形: `session.poll`、`subscriber.each_pending { |key, payload| }`、
  `get.each_reply`、`queryable.each_pending { |q| q.reply(...) }`、`liveliness_watch.each_pending`。
  コールバックの中で Ruby の VM に触らない。
- 切れたら閉じる (`poll` が false、`closed?` が true、`put` が `Asterism::Zenoh::Error`)。自動の再接続はしない。
- CRuby 版でもこの形を基本にする。加えて、CRuby らしいブロックの API (`subscribe { |s| ... }` を内部のスレッドで
  回すなど) を上に足すのは自由。

### CRuby (案)

- Rust を包む案: zenoh-python (PyO3) と同じ構成で参考にしやすい。購読のコールバックは Zenoh のスレッドから
  呼ばれるので、Rust 側の FIFO に溜めて Ruby のスレッドから取り出す。待つ API は待っている間 GVL を手放す。
  rb-sys の cross-gem で機種ごとのビルド済みの gem を配る。
- zenoh-c を包む案: zenoh-pico と API が同じなので、mruby 版と作りをそろえやすい。
- どちらにするかは、CRuby 版に着手するときに決める。

### マイコン

- ESP32-P4 (Tab5、P4-Nano): zenoh-pico で動作を確認済み (Z2・Z3)。ESP32-S3 は Z4。
- Raspberry Pi Pico W / Pico 2 W は zenoh-pico が公式に対応 (WiFi の UDP / TCP、シリアル、USB CDC)。
  PicoRuby と組み合わせるなら RAM に余裕のある Pico 2 W (520 KB)。使わない機能をビルドで切る、シリアルや
  USB で親機につなぐと RAM を節約できる。

## 4. キー空間 (決定、2026-10-07)

- 最上位は `asterism/` (Family mruby とは別のプロジェクトなので `fmrb/` にしない)。
- 形: `asterism/<ID>/<アプリ>/<データ>`。1 台 = 1 ノード。ブラウザも機体も同じ形。
  - 例: `asterism/fmruby-90bce8/clock/time`、`asterism/b-3f9a2c/sensor/temp`。
  - VM ごとに分けるときは `<ID>/<VM>/...` と一段増やす。
- オブジェクト (A1): `asterism/<ID>/<アプリ>/<オブジェクト>/call` (呼び出し)、`.../meta` (公開の一覧)。
- 生存: `asterism/<ID>` (ノードの参加・離脱)、`asterism/<ID>/<アプリ>/<オブジェクト>` (公開中のオブジェクト)。
- Zenoh の `@` は管理用に予約されているので使わない。
- ACL で、ユーザごとに書けるキーを絞り、なりすましを防ぐ (ルータが外にあるとき。7 章)。

### ID

- 機体: 基板ごとの mDNS 名 (`fmruby-XXXXXX`、MAC から作る) を既定にする。上書きはアプリ側の設定 (Family mruby なら
  `/home` の設定ファイル)。gem は ID を作らず、`Asterism.connect(..., node: id)` で受け取る。
- ブラウザ: 9 章。
- 起動のときに、liveliness で同じ ID がいないかを確かめてから自分のトークンを出す。

## 5. ROS 2 (rmw_zenoh) との通信 (案)

- キー: おおむね `<ドメイン ID>/<トピック名>/<型名>/<型のハッシュ>`。購読はワイルドカード可、出すときは型名と型の
  ハッシュを正確に合わせる。
- 中身: CDR (先頭に 4 バイトの印)。
- attachment: シーケンス番号 (int64)、時刻 (int64 ns)、GID (長さ 1 バイト + 16 バイト) の 33 バイト。**トピックでも必須**
  (無いと rmw_zenoh が受け取りを捨てる。R1 で確認、2026-10-07)。mrbgem は R1 で put / subscribe の attachment に対応した。
  サービスでは要求 (get) と答え (reply) の両方に同じ形の attachment が付き、答えは要求の通し番号と GID を返す。
  mrbgem は R2 で get / queryable の attachment と、get の target (ALL_COMPLETE)・complete な queryable に対応した
  (report/r2.md)。
- liveliness のトークン: `ros2 node list` / `ros2 topic list` に出すために要る。
- 接続: rmw_zenoh は既定でローカルの `rmw_zenohd` につなぎ、マルチキャストでの発見は切ってある。
- QoS: transient local などは振る舞いを合わせる手間が大きい。
- 版: ROS のディストリビューションが使う Zenoh の版にそろえる。正確な仕様は rmw_zenoh 側の設計で確かめる。
- DDS の ROS 2 とつなぐなら zenoh-bridge-ros2dds も選択肢 (キーがトピック名だけで扱いやすい)。

### 進め方 (案)

1. 機体 (mrbgem) から `std_msgs/String` を手書きの CDR で出し、`ros2 topic echo` で見える最小の疎通 (**R1 で完了**、
   report/r1.md。ROS 2 Jazzy + rmw_zenoh 0.2.11 (Zenoh 1.8.0) と zenohd 1.10.1・zenoh-pico 1.10.1 で通じる)。
2. `asterism-cdr` と `asterism-ros` のトピックの部分。
3. サービス、liveliness、メッセージの自動生成は後。

## 6. Ruby らしい API (ROS の層、案)

```ruby
require "asterism"

Asterism.connect("tls/ros.example.com:7447", domain: 0) do |ros|
  node = ros.node("ruby_talker")

  chatter = node.publisher("/chatter", StdMsgs::String)
  chatter << { data: "hello from Ruby" }

  node.subscribe("/odom") do |odom|
    puts odom.pose.pose.position.x
  end

  res = node.call("/add_two_ints", a: 1, b: 2)
  puts res.sum

  ros.spin
end
```

- `connect` にブロックを渡すと、抜けたときにセッションを閉じる。
- `<<` や `call` に Hash を渡すと、型に合わせてメッセージに変える。
- 購読は liveliness の型情報から型を判定できる。出す側は型の明示を基本にする。
- メッセージは `Data.define` の値オブジェクトとし、`deconstruct_keys` でパターンマッチに対応する
  (mruby 側で使えるかは確かめる)。
- `node.topic("/scan").each.lazy` のように Enumerator としても扱える。タイマーは `node.every(0.1) { ... }`。
- 名前は Ruby 寄り (`publisher` / `subscribe`)。rclpy 風の別名を用意してもよい。
- mruby の機体では、`spin` の代わりにアプリの更新ごとに `poll` を呼ぶ形になる (3 章の API)。

## 7. 一覧 (接続中のもの)

- ROS のグラフ: liveliness のトークンを取り、購読して、ノード・トピック・サービスを型つきで一覧にする
  (`asterism-ros`)。
  ```ruby
  ros.nodes.each { puts _1.name }
  ros.topics.each { |t| puts "#{t.name} [#{t.type}]" }
  ros.on_graph_change { |ev| puts "#{ev.kind}: #{ev.name}" }
  ```
- Zenoh の網: ルータの管理の空間 (`@/**`) に問い合わせ、セッションやリンクを取る (`asterism-zenoh`)。
  PC の tools/fmrb_zenoh.rb の `alive` は、この空間からトークンを取っている。
- スカウティングは LAN の中だけ。インターネット上のルータでは、管理の空間を ACL で一般のクライアントから隠す。
- ESP32 から: zenoh-pico でも問い合わせできるが、答えの JSON が大きいので、キーを絞るか、サーバ側でまとめた要約
  (例: `asterism/<ID>/fleet/summary`) を購読する。グラフの情報は liveliness の方が軽い。

## 8. インターネット上のルータ (案)

- 各端末が外向きにつなぐだけで、NAT やポートの開放を気にせず一つの名前の空間でつながる。
- 使い方の想定: ロボットの遠隔の監視・操作、マイコンの参加、ライブと記録の取り出し、端と雲の処理の分担、
  複数の拠点の協調。
- 守り: TLS / QUIC、認証 (ユーザ名とパスワード、証明書)、キーごとの ACL。本格的に使うなら、1 台が止まって
  全部が止まらないよう複数台の構成も考える。
- **実機の現状 (zenoh-pico 1.10.1、2026-10-07 確認)**: ESP32 では TLS が使えず、ユーザ名とパスワードの認証は
  実装されていない (設定の名前だけある)。そのため当面は、PC やサーバが TLS と認証で外のルータにつなぎ、機体は
  LAN の中のルータにだけつなぐ。機体が直接外につなぐのは、ESP32 で TLS が使えるようになってから。
- 他の言語とつなぐときは中身の形式 (JSON / CBOR / MessagePack / CDR) をそろえ、Zenoh 1.x どうしで話す。

## 9. ブラウザ版 (Family mruby の wasm 版)

### 接続の方式 (案)

- zenoh-pico を Emscripten でビルドし、WebSocket でルータに直接つなぐ。
  - ルータは `ws/` のロケータで待ち受ける。remote-api のプラグインは要らない。
  - 機体と同じ zenoh-pico の mrbgem を使える。
- 代わりの案: zenoh-ts を Emscripten の JS 連携で呼ぶ (ブラウザ専用になり、機体とコードを共有できない)。
- 確かめること: zenoh-pico の Emscripten のシステム層 (vendor の `src/system/emscripten` にある) と WebSocket の
  リンクの対応、zenohd の WebSocket のトランスポートが使える版か。小さなサンプルで先に動かす。

### 注意

- シングルスレッドの設定でビルドし、Family mruby のスケジューラの刻みから poll を呼ぶ (機体と同じ)。
- ページの主スレッドを止めないよう、答えを待つ処理は待たない形 (後で取り出す) を使う。A1 の「答えを待つ
  呼び出し」がブラウザ版で画面ごと止めないかは、wasm 版のタスクの動き方で確かめる。
- HTTPS のページからは `wss://` が必須。リバースプロキシで TLS を終え、ルータの `ws/` に流す。

### インスタンスの識別 (案)

- 1 ブラウザ (同じ種類・同じプロファイル・同じオリジン) につき Family mruby は 1 つだけ起動する。種類・プロファイル・
  シークレットウィンドウが違えば別のマシンとして扱う。「同じ PC で 1 つ」までは保証しない。
- 排他は Web Locks API (`navigator.locks.request` + `ifAvailable: true`)。2 つ目のタブは起動せずに知らせる。必要なら
  `steal: true` で奪って起動し直せる。タブを閉じる・落ちるとロックは自動で外れる。
- ID は最初の起動でランダムに作り、localStorage に置いて以降は固定 (wasm 版の保存の仕組みと合うか確かめる)。
  起動のときに liveliness で同じ ID の重複を確かめる (4 章)。
- 開発で複数台を試すときは、別のプロファイルかシークレットウィンドウを使う。複数の画面が欲しいときは、
  Family mruby の窓の管理の中で開き、ブラウザのタブは増やさない。
