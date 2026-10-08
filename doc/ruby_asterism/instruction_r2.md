# Asterism R2 指示書: ROS 2 のサービス

> 状態: 指示 | 更新: 2026-10-07 | 機体がサービスを提供し PC の `ros2 service call` から呼べる。逆に、機体が ROS 2 のサービスを呼んで答えを受け取る。例は example_interfaces/srv/AddTwoInts

前提: report/r1.md (特に 2 章のワイヤの形と 10 章の申し送り)、design.md の 5 章、`lib/add/picoruby-asterism-zenoh/` と
`lib/add/picoruby-asterism/` (`Asterism::CDR`・`Asterism::ROS`) の README とソース。

## 1. やること

1. **ワイヤの形を確かめる** (R1 と同じく PC の中で観察し、使う版の rmw_zenoh で確かめる):
   - サービスのキー、型名、型のハッシュ (要求と応答)
   - 要求と応答の CDR
   - 問い合わせの attachment と応答の attachment (シーケンス番号・時刻・GID の対応)
   - get の設定 (target、consolidation、時間制限)
   - サービスの生存のしるし (`SS` / `SC`)
2. **gem (`picoruby-asterism-zenoh`)**:
   - get に attachment と get の設定を渡せるようにする。
   - 答え (reply) の attachment を取り出せるようにする。
   - queryable 側でも、届いた問い合わせの attachment を取り出し、答えに attachment を付けられるようにする。
   - 今の API に足すだけにし、ポーリングで取り出す形を守る。起動時の内蔵 RAM を増やさない。
3. **純 Ruby (`Asterism::ROS`)**:
   - `node.service(name, type) { |req| ... }` (提供) と `node.client(name, type)` + 呼び出しを作る。
   - 呼び出しは A1 と同じく、答えを待つ形 (時間制限つき) と、待たない形の両方を用意する。
   - example_interfaces/srv/AddTwoInts の型を持つ (型のハッシュは R1 と同じく定数でよい)。
   - 名前は design.md 6 章の案 (`node.call(...)` など) を参考に、Ruby らしくする。README に一覧を書く。
4. **PC 側**: `docker-compose.ros2.yml` の像に example_interfaces と demo_nodes_cpp (`add_two_ints_server`) が無ければ
   足す。既存のサービスとポートは変えない。
5. **試しのアプリ** (`flash/app/test/ros2_service.app.rb` など):
   - `/<node名>/add_two_ints` (または `/add_two_ints_fmrb`) を提供し、届いた要求と答えを画面に出す。
   - キーを押すと、PC の `add_two_ints_server` の `/add_two_ints` を呼び、答えと時間を画面に出す。待たない形も 1 か所で使う。

## 2. 受け入れ条件

1. PC の `ros2 service call /<機体のサービス> example_interfaces/srv/AddTwoInts "{a: 2, b: 3}"` が `sum: 5` を返す
   (P4-Nano と sim)。
2. 機体から PC の `/add_two_ints` (`ros2 run demo_nodes_cpp add_two_ints_server`) を呼ぶと、正しい和が画面に出る。
3. `ros2 service list -t` に機体のサービスが出て、アプリを閉じると消える。
4. 相手のサービスが無いとき、機体からの呼び出しは時間切れになり、アプリは止まらない。
5. R1 の ros2_talker、A1 の asterism_demo、zenoh_echo が今も動く (sim で 1 回ずつ)。標準構成でエディタを起動して 1 打鍵。
   `rake test` が通る。
6. P4 の起動時の内蔵 RAM の増分 0。flash の増分と区画の残りを書く。

## 3. 触ってよい範囲

- fmruby-core:
  - `lib/add/picoruby-asterism-zenoh/`、`lib/add/picoruby-asterism/`
  - `flash/app/test/ros2_*`
  - `doc/ruby_asterism/report/r2.md`、gem の README
  - design.md 5 章の、サービスの記述の事実の訂正だけ
- 親リポジトリ: `docker-compose.ros2.yml` とその像の定義、`tools/` の確かめ用のスクリプト (Ruby)。
- 触らないもの:
  - `.env` (TAB5 のまま。P4-Nano は `FMRB_HW_TARGET=NARYAv4`)
  - sdkconfig / sdkconfig.defaults*、パーティションの表
  - vendor/、submodule の中、fmruby-graphics-audio
  - S3 / wasm の構成、既存の docker compose のサービスとポート

## 4. 止まる条件

- zenoh-pico 1.10.1 で必要な get の設定や attachment が使えず、版を上げる必要があるとき (上げずに案を書く)。
- 内蔵 RAM が増えるとき、P4 の区画に収まらないとき。
- 範囲の外の変更、利用者から見た動き (切断の扱いなど) の選択が要るとき。

## 5. report (`report/r2.md`) に書くこと

- 観察したワイヤの形 (キー、型のハッシュ、CDR、attachment、get の設定、トークン) の表。
- API の一覧。受け入れ条件 1-6 の結果と証拠、呼び出しの往復の時間 (機体から PC、PC から機体)。
- 見立てと違った点、撤回した仮説、踏んだ罠、申し送り (ほかの型の作り方、asterism-msgs への育て方)。

## 6. 作業の決まり

- ブランチ `feature/asterism-r2` を fmruby-core と親リポジトリの develop から切ってコミットする。
  - コミットは英文で `<領域>: <要約>` の形にし、Co-Authored-By を付ける。
  - develop へのマージと push はしない。
- 機体は **P4-Nano (192.0.2.15、/dev/ttyACM0)**。
  - R1 のファームが入っていて、ミュート中 (作業の前に確かめる)。
  - 親のシリアルの capture がこの機体を向いている。`serial_start` は呼ばない。
  - 焼くのは MCP の `flash` (`app_only`)。
  - tab5_* のツールには ip を渡す。
- zenohd を LAN に開けるのは作業の間だけ。終わったら ROS 2 のコンテナも止める (`down` で network まで片付ける)。
- 終わったら sim は sim_down、`build/` は Linux の標準構成 (x86-64) に戻す。
