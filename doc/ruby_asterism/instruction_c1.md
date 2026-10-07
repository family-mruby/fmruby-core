# Asterism C1 指示書: CRuby 版

> 状態: 指示 | 更新: 2026-10-08 | CRuby から Asterism の網に入る。zenoh-c を C 拡張で包んだ `asterism-zenoh` (mruby 版と同じ Ruby の API) と、純 Ruby の層 (オブジェクトの代理・CDR・ROS・型) を CRuby の gem として動かし、機体・sim・ROS 2 と往復する

ユーザは「CRuby 版まで親の推奨で実装」と指示した (2026-10-08)。下の選択は親の推奨で決めたもの。報告で、あとから変えられる点として
挙げる。

## 0. 決めたこと (親の推奨)

1. **バインディングは zenoh-c を C 拡張で包む** (Rust の Magnus ではなく)。
   - 理由 1: zenoh-c の C の API は zenoh-pico と同じ形 (`z_open`、`z_declare_subscriber`、`z_get` …) なので、mruby 版の
     `zenoh.c` の作りと API をそのまま写せる。
   - 理由 2: 公式のビルド済みのライブラリがあり、Rust のビルドが要らない。
   - 配布 (rubygems に機種ごとのビルド済みの gem を出す) をどうするかは、後で決める (C2 以降)。
2. **zenoh-c の版は 1.10.1** (zenoh-pico・zenohd と同じ)。公式のリリースのビルド済みのもの (x86_64 Linux) を、rake の
   タスクで取ってくる。PIN の形は fmruby-core の `ZENOH_PICO_PIN` と同じにする。
3. **置き場所は新しい独立のリポジトリ** `/home/kishima/fmrb/family-mruby/asterism/`。
   - git init だけで、remote は作らない。親リポジトリの `.gitignore` に足す。fmruby-core と同じ「無視された独立の
     リポジトリ」の形。
   - Asterism と Family mruby は別のプロジェクトなので、CRuby の gem は fmruby-core に置かない。
4. **純 Ruby の層の正は、今は fmruby-core の `lib/add/picoruby-asterism/mrblib/`**。
   - CRuby の gem はその写しを持ち、写すタスク (`rake sync`) と「写しが一致する」試験を置く。
   - 正をどちらのリポジトリに移すかは、後でユーザが決める。
   - 写すときに CRuby だけで要る違いがあれば、写しを書き換えず、CRuby 側の小さな追加ファイルで吸収する。
5. **Ruby から見える API は mruby 版と同じ** (design.md 3 章): `Asterism::Zenoh::Session.open`、`put`、`subscribe` +
   `each_pending`、`get` + `each_reply`、`queryable` + `each_pending`、`liveliness`、`poll`、`closed?` など。
   - 受信は zenoh-c の FIFO のハンドラ (チャネル) に溜め、`poll` / `each_pending` で取り出す。
     コールバックの中で Ruby に触らない。
   - 待つ処理 (`Session.open` の接続など) は GVL を手放す。
   - CRuby らしいブロックの API (受信をスレッドで回すなど) は C1 では作らない。
6. gem の名前は `asterism-zenoh` (`require "asterism/zenoh"`) と `asterism` (`require "asterism"`、純 Ruby の層をまとめて読む)
   (design.md 2 章)。
   - C1 ではローカルでビルドして使う。rubygems には出さない。0.0.0 の名前の確保は済んでいる。
   - `asterism` の版を上げて push するのは、ユーザの指示まで行わない。

## 1. やること

1. `asterism/` リポジトリを作り、gem 2 つの骨組み、Rakefile (zenoh-c の取得、C 拡張のビルド、試験、sync)、README。
2. `asterism-zenoh`: mruby 版の `picoruby-asterism-zenoh/src/zenoh.c` の API を、zenoh-c で C 拡張として作る。
   - 対象は R1・R2 で足したもの全部: attachment、get の設定、queryable の complete、`Session#zid`。
   - client / peer / listen も対象。peer の上限の扱いは zenoh-c の設定に合わせる。
   - 例外は `Asterism::Zenoh::Error`。
3. `asterism`: 純 Ruby の層 (Asterism の代理・公開・一覧、`Asterism::CDR`、`Asterism::ROS`、同梱の型) を CRuby で読めるようにする。
   - MessagePack は CRuby の msgpack gem を使う。gem はユーザの rbenv の Ruby に入れてよい。`gem install` の範囲で、sudo は使わない。
   - 型は `require_type` (CRuby では require) で、fmruby-core の `flash/usr/share/asterism/msgs` の写しか、gem の中の写しを読む。
4. 試験 (CRuby、`rake test`):
   - 2 つの CRuby のセッションの間で put / subscribe、get / queryable、liveliness が通る。zenohd を使ってよい。
   - 純 Ruby の層の写しが一致する。
5. 確かめ用の例 (`examples/`):
   - `node.rb`: CRuby のノードがオブジェクトを公開し、機体のオブジェクトを呼ぶ。
   - `ros2_talker.rb`: CRuby から ROS 2 に Twist を出し、サービスを呼ぶ。

## 2. 受け入れ条件

1. `asterism/` で `rake` (取得・ビルド・試験) が通る。CRuby 3.2 (ユーザの rbenv)。
2. **CRuby ⇔ 機体 (A1)**:
   - CRuby から P4-Nano の asterism_demo の `info.status` と `screen.say` を呼べる (画面に出る)。
   - P4-Nano から、CRuby が公開したオブジェクトを呼べる。
   - sim でも同じ。
3. **CRuby ⇔ ROS 2 (R1-R3)**:
   - CRuby が出した geometry_msgs/Twist を、PC の `ros2 topic echo` と P4-Nano の ros2_types が読める。
   - CRuby から `ros2 service call` 相当で、機体の AddTwoInts を呼べる。
4. **CRuby ⇔ zenoh (Z1)**: CRuby の put を機体の zenoh_echo が受け、機体の put を CRuby が受ける。
5. 切断の扱い: zenohd を止めると、CRuby のセッションの `poll` が false、`closed?` が true になる (mruby 版と同じ)。
6. fmruby-core と親リポジトリに、範囲の外の変更が無い。

## 3. 触ってよい範囲

- 新しい `asterism/` リポジトリの全部。
- 親リポジトリ: `.gitignore` に `asterism/` を足すこと、確かめに要るなら `tools/` のスクリプト。
- fmruby-core: 読むだけ。
  - 例外 1: 純 Ruby の層に、CRuby で動かすための**小さな直し**が要る場合 (mruby でも正しく動くもの)。
  - 例外 2: report。
  - 直したら fmruby-core の `feature/asterism-c1` に分けてコミットし、sim で asterism_demo と ros2_types を確かめる。
- 触らないもの: `.env`、sdkconfig、パーティションの表、vendor/、submodule の中、fmruby-graphics-audio、既存の compose の
  サービスとポート。

## 4. 止まる条件

- zenoh-c 1.10.1 のビルド済みのものが手に入らず、Rust でビルドする必要があるとき。cargo はあるが時間とディスクを使うので、
  止まって案を書く。
- sudo やシステムへのパッケージの導入が要るとき。
- 純 Ruby の層に、mruby の動きを変える大きな直しが要るとき。
- 範囲の外の変更、利用者から見た動きの選択が要るとき。

## 5. report

- 書く場所: fmruby-core の `doc/ruby_asterism/report/c1.md`。fmruby-core の `feature/asterism-c1` にコミットする。
- 書くこと:
  - 作ったもの (リポジトリの構成、gem、API)、zenoh-c の取得の仕方、写しの仕組み。
  - 受け入れ条件 1-6 の結果と証拠、往復の時間 (CRuby ⇔ 機体)。
  - 見立てと違った点、撤回した仮説、踏んだ罠。
  - ユーザがあとで決めること: 配布の形 (ビルド済みの gem、Rust 版への切り替え)、純 Ruby の層の正をどちらに置くか、
    CRuby らしいブロックの API。

## 6. 作業の決まり

- `asterism/` リポジトリではブランチ `main` にコミットしてよい (新しいリポジトリ、remote なし)。
  - コミットは英文で `<領域>: <要約>` の形にし、Co-Authored-By を付ける。
- fmruby-core と親リポジトリでは `feature/asterism-c1` にコミットする。
  - fmruby-core の `feature/asterism-c1` は `feature/asterism-r3` から切る (R3 はまだ develop に入っていないため)。
  - 親リポジトリの `feature/asterism-c1` は `feature/asterism-r3` から切る。
  - develop へのマージと push はしない。
- 機体は P4-Nano (192.168.10.15、/dev/ttyACM0)。
  - R3 のファームが入っていて、ミュート中。
  - 親のシリアルの capture がこの機体を向いている (`serial_start` を呼ばない)。
  - 焼き直しは要らない見込み。焼くなら MCP の `flash` (`app_only`)。
  - tab5_* には ip を渡す。
- zenohd・ROS 2 を LAN に開けるのは作業の間だけ。終わったら `down` (network まで片付ける)。
- 終わったら、sim は sim_down、`build/` は Linux の標準構成 (x86-64) のまま。
