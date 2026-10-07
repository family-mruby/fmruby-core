# Asterism R1 指示書: ROS 2 (rmw_zenoh) との最小の疎通

> 状態: 指示 | 更新: 2026-10-07 | P4-Nano のアプリが std_msgs/String を出し、PC の ROS 2 (rmw_zenoh) の `ros2 topic echo` で見える。逆向き (ROS 2 から出して機体が受ける) も確かめる

前提: design.md の 5 章 (ROS 2 との通信) と 2-4 章、plan.md の A1 の節、report/z1.md-z3.md・a1.md、
`lib/add/picoruby-asterism-zenoh/` と `lib/add/picoruby-asterism/` の README。

## 1. やること

1. **PC 側の ROS 2**: docker で ROS 2 と rmw_zenoh の入った環境を用意する。
   - ディストリビューションは、rmw_zenoh が Zenoh 1.x を使っていて、手に入れやすいものを選ぶ (Jazzy か Kilted など)。
     使う Zenoh の版を report に書く。
   - 環境は親リポジトリに docker compose の重ね合わせのファイルとして置く (例: `docker-compose.ros2.yml`)。
     既存のサービスの設定は変えない。
   - ルータ: 既存の zenohd (1.10.1、LAN 用の重ね合わせ) を使うか、rmw_zenohd を使うかを調べて決める。版の違いで
     通じない場合は、rmw_zenohd を使う。
2. **ワイヤの形を確かめる**: rmw_zenoh の実際の通信を PC の中だけで観察し、std_msgs/String の次のものを確かめる。
   - キー
   - 型名
   - 型のハッシュ (RIHS01)
   - CDR の形 (先頭の 4 バイト、文字列の長さ、揃え)
   - attachment の中身と要否
   - liveliness のトークンの形 (`ros2 node list` / `ros2 topic list` に出るため)

   観察の方法は問わない (zenoh の購読で生のバイトを見る、など)。分かったことは report に表で残す。正確な仕様は、
   使う版の rmw_zenoh で確かめる。
3. **gem**:
   - attachment が要るなら、`picoruby-asterism-zenoh` の put に attachment を渡せるようにする。受け取る側でも取れるように
     する。API は今の形に足すだけにする (例: `put(key, payload, attachment: bytes)`、`each_pending` の 3 つ目の値)。
     zenoh-pico の機能の有効化で内蔵 RAM が増えないこと。
   - CDR と ROS のキーとトークンの組み立ては、**純 Ruby** で書く。置き場所は `lib/add/picoruby-asterism/` の中
     (例: `Asterism::ROS`、`Asterism::CDR`) か、別の小さな gem。std_msgs/String に必要な分だけでよい。
     Family mruby に依存しない。
4. **試しのアプリ** (`flash/app/test/ros2_talker.app.rb` など):
   - 1 秒ごとに `/chatter` に `"hello from <ID> N"` を出す。
   - `/chatter_back` などを購読して、ROS 2 から来た文字列を画面に出す。
   - ノードとトピックのトークンを出し、`ros2 node list` / `ros2 topic list` に見えるようにする。
5. **PC 側の確かめ方の手順**を report と、親リポジトリの重ね合わせのファイルのコメントに書く。

## 2. 受け入れ条件

1. P4-Nano のアプリが出した文字列が、PC の `ros2 topic echo /chatter std_msgs/msg/String` で読める。
2. PC の `ros2 topic pub /chatter_back std_msgs/msg/String ...` が、P4-Nano の画面に出る。
3. `ros2 node list` と `ros2 topic list -t` に P4-Nano のノードとトピックが出る。アプリを閉じると消える。
4. sim (標準構成) でも 1-3 が通る (sim のアプリと、同じ PC の ROS 2)。
5. P4 の起動時の内蔵 RAM の増分 0。flash の増分と区画の残りを書く。
6. A1 の asterism_demo と zenoh_echo が今も動く (sim で 1 回ずつ)。標準構成でエディタを起動して 1 打鍵。
   `rake test` が通る。

## 3. 触ってよい範囲

- fmruby-core:
  - `lib/add/picoruby-asterism-zenoh/`、`lib/add/picoruby-asterism/` (または新しい純 Ruby の gem とその登録)。
  - Linux と P4 のビルド構成への gem の登録だけ。
  - `flash/app/test/ros2_*`。
  - `doc/ruby_asterism/report/r1.md`、gem の README。
- 親リポジトリ: ROS 2 用の docker compose の重ね合わせのファイル、`tools/` の確かめ用のスクリプト (Ruby)。
- 触らないもの:
  - `.env`: TAB5 のまま。P4-Nano のビルドは環境変数 `FMRB_HW_TARGET=NARYAv4` で渡す。
  - sdkconfig / sdkconfig.defaults*、パーティションの表、vendor/、submodule の中、fmruby-graphics-audio。
  - S3 / wasm の構成、既存の docker compose のサービスとポート。

## 4. 止まる条件

- rmw_zenoh と zenoh-pico 1.10.1 の版が合わず、zenoh-pico の版を上げる必要があるとき (上げずに案を書く)。
- attachment などで内蔵 RAM が増える、P4 の区画に収まらないとき。
- ROS 2 の環境に sudo やホストへのパッケージの導入が要るとき (docker の中で済ませる。済まなければ止まる)。
- 範囲の外の変更、利用者から見た動きの選択が要るとき。

## 5. report (`report/r1.md`) に書くこと

- 使った ROS 2 のディストリビューション・rmw_zenoh・Zenoh の版。
- 観察したワイヤの形 (キー、型のハッシュ、CDR、attachment、トークン) の表。
- 受け入れ条件 1-6 の結果と証拠。
- 見立てと違った点、撤回した仮説、踏んだ罠。
- 申し送り: asterism-cdr / asterism-msgs / asterism-ros (design.md 2 章) に育てるときのこと、サービスと QoS。

## 6. 作業の決まり

- ブランチ `feature/asterism-r1` を fmruby-core と親リポジトリの develop から切ってコミットする (英文、`<領域>: <要約>`、
  Co-Authored-By)。develop へのマージと push はしない。
- 機体は **P4-Nano (192.168.10.15、/dev/ttyACM0)**。今は 6fe8f050 のファーム (A1 の前) が入っている。
  - ミュートを確かめる。
  - 親のシリアルの capture がこの機体に向いている (`serial_start` を呼ばない)。
  - 焼くのは MCP の `flash` (`app_only`)。Tab5 は外されている。
  - tab5_* には ip を渡す。
- zenohd などを LAN に開けるのは作業の間だけ。終わったら ROS 2 のコンテナも止める。
- 終わったら sim は sim_down、`build/` は Linux の標準構成 (x86-64) に戻す。
