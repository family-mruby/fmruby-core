# Asterism R3 指示書: メッセージ型の変換器

> 状態: 指示 | 更新: 2026-10-07 | `.msg` / `.srv` から Ruby のクラス・型名・型のハッシュ・CDR の変換を作る PC の変換器を作り、よく使う型を同梱する。手書きの std_msgs/String と AddTwoInts を作ったものに置き換え、入れ子と配列のある型で ROS 2 と往復する

前提:
- plan.md の「ROS 2 の続き」と R3 の節
- design.md 2 章・5-6 章
- report/r1.md・r2.md
- `lib/add/picoruby-asterism/` (`Asterism::CDR`・`Asterism::ROS`) の README とソース
- 親リポジトリの `docker-compose.ros2.yml`

## 1. やること

### 1.1 変換器 (PC、CRuby、標準ライブラリだけ)

- 入力は `.msg` / `.srv`。ROS 2 Jazzy の像の中の `/opt/ros/jazzy/share/<pkg>/msg|srv/` と、ユーザの手元のファイル。
  型が別の型を参照するとき (入れ子)、その型もたどって作る。
- 出力は純 Ruby のファイル。内容は型ごとに次のもの:
  - フィールド (既定値つき)
  - 型名 (`<pkg>::msg::dds_::<Name>_`)
  - 型のハッシュ (RIHS01)
  - CDR への変換と CDR からの変換
  - サービスなら Request / Response と、サービスの型のハッシュ
- **型のハッシュは変換器が計算する** (SHA-256。Ruby の標準の Digest でよい)。
  - 計算の仕方は、使う版 (Jazzy) の型の記述の決まりに合わせる。
  - 像の中に入っている型の記述の JSON (`share/<pkg>/msg/*.json` など) のハッシュと、全部の同梱の型で一致することを確かめる。
  - R1・R2 の定数 (std_msgs/String、AddTwoInts) とも一致すること。
- CDR で扱う範囲:
  - 基本の型 (bool、byte、char、整数 8-64 bit、float32/64)
  - string
  - 固定長の配列、長さの上限ありとなしの列 (sequence)
  - 入れ子の型
  - 揃え (R1 で確かめた、先頭の 4 バイトの後から数える決まり)
  - wstring は後回しでよい (出てきたら例外)。
- 置き場所: 変換器は `lib/add/picoruby-asterism/tools/` などに置く。使い方を README に書く。Family mruby に依存しない。

### 1.2 値の形

- mruby (アプリの VM) に `Data` (`Data.define`) があるかを確かめる。無ければ Struct、それも無ければ普通のクラスにする。
  CRuby でも同じ書き方で動く形にする。
- どれでも次ができること:
  - Hash から作れる (`pub << { linear: { x: 0.1 } }`)。足りないフィールドは既定値。
  - `to_h` で Hash に戻せる。

### 1.3 同梱する型と、読み込み方

- 同梱する型 (Jazzy の定義から作る): std_msgs (String、Bool、整数・浮動小数の基本の型、Header、
  MultiArray の類)、builtin_interfaces (Time、Duration)、geometry_msgs (Vector3、Point、Quaternion、Pose、Twist とそれらの
  Stamped)、sensor_msgs の一部 (Imu、BatteryState など、Header を含むもの)、example_interfaces (AddTwoInts)。
- **アプリが使う型だけを読み込む**。flash のファームに全部を入れない。
  - 例: storage (`/usr/share/asterism/msgs/<pkg>/<Name>.rb`) に置き、`Asterism::ROS.require_type("geometry_msgs/msg/Twist")`
    で読む。
  - picoruby の `require` / `load` の実際の振る舞いを確かめてから決める。決めた形と、flash・storage・VM のメモリの量を
    report に書く。
- R1・R2 の手書きの型 (std_msgs/String、AddTwoInts) は、作った型に置き換える。
  - 試しのアプリ (ros2_talker、ros2_service) が、置き換えた後も動くこと。

### 1.4 試しのアプリ (`flash/app/test/ros2_types.app.rb` など)

- geometry_msgs/Twist を購読して画面に出す (PC から `ros2 topic pub /cmd_vel ...`)。同じ値を返す、などで往復を見られるようにする。
- sensor_msgs/Imu を Header 付きで出す (値は作り物でよい。Header の stamp に機体の時刻)。
  PC の `ros2 topic echo` で全部のフィールドが正しく読めること。
- 列 (sequence) を含む型を 1 つ往復させる (例: std_msgs/Float32MultiArray)。

### 1.5 試験

- host のテスト (`rake test` に入る形、CRuby):
  - 変換器が作った型の CDR の往復
  - 型のハッシュの一致 (同梱の全部の型)
  - 境界 (空の列、最大長、入れ子の揃え)

## 2. 受け入れ条件

1. 同梱の全部の型で、変換器が計算した型のハッシュが、Jazzy の型の記述の JSON のハッシュと一致する
   (host のテスト)。std_msgs/String と AddTwoInts は R1・R2 の定数とも一致する。
2. P4-Nano と sim で次が通る:
   - PC の `ros2 topic pub /cmd_vel geometry_msgs/msg/Twist ...` が機体の画面に出る。
   - 機体が出す sensor_msgs/Imu と Float32MultiArray が PC の `ros2 topic echo` で正しく読める。
3. ros2_talker と ros2_service が作った型で動く (R1・R2 の受け入れ条件 1-2 をそれぞれ 1 回)。
4. 型を読み込む形が「使う型だけ」になっている。flash (ファーム)・storage・アプリの VM のメモリの量を書く。
   起動時の内蔵 RAM の増分 0。
5. `rake test` が通る (新しい host のテストを含む)。asterism_demo、zenoh_echo が sim で今も動く。標準構成でエディタを起動して
   1 打鍵。

## 3. 触ってよい範囲

- fmruby-core:
  - `lib/add/picoruby-asterism/` (と、型を置く新しい gem か storage の場所)
  - 同梱する型を置く `flash/usr/share/asterism/` など
  - `flash/app/test/ros2_*`
  - host のテストの追加と Rakefile / rakelib の test の部分
  - `doc/ruby_asterism/report/r3.md`、gem の README
- 親リポジトリ: `docker-compose.ros2.yml` (要るなら)、`tools/` の確かめ用のスクリプト (Ruby)。
- 触らないもの:
  - `.env` (TAB5 のまま。P4-Nano は `FMRB_HW_TARGET=NARYAv4`)
  - sdkconfig / sdkconfig.defaults*、パーティションの表
  - vendor/、submodule の中、fmruby-graphics-audio
  - S3 / wasm の構成、既存の docker compose のサービスとポート
  - `picoruby-asterism-zenoh` の C (要るなら止まる)

## 4. 止まる条件

- 型のハッシュが Jazzy の JSON と一致しない (計算の仕方が分からない) とき。
- picoruby で、型のファイルを後から読み込めないとき (案を書いて返す)。
- 内蔵 RAM が増えるとき、P4 の区画に収まらないとき。
- 範囲の外の変更、利用者から見た動きの選択が要るとき。

## 5. report (`report/r3.md`) に書くこと

- 変換器の使い方、型のハッシュの計算の仕方、CDR の扱う範囲。
- 値の形 (Data / Struct / クラスのどれになったか、理由)。
- 型の読み込み方と、flash・storage・VM のメモリの量。
- 受け入れ条件 1-5 の結果と証拠。
- 見立てと違った点、撤回した仮説、踏んだ罠、申し送り (知らない型の受け取り、QoS、CRuby 版での使い方)。

## 6. 作業の決まり

- ブランチ: fmruby-core と親リポジトリの develop から `feature/asterism-r3` を切ってコミットする。
  - コミットは英文で `<領域>: <要約>` の形にし、Co-Authored-By を付ける。
  - develop へのマージと push はしない。
- 機体: **P4-Nano (192.168.10.15、/dev/ttyACM0)**。
  - R2 のファームが入っていて、ミュート中 (確かめる)。
  - 親のシリアルの capture がこの機体を向いている (`serial_start` を呼ばない)。
  - 焼くのは MCP の `flash` (`app_only`)。
  - storage に置く型のファイルは `tab5_fs` で送る (app_only は storage を更新しない)。
  - tab5_* には ip を渡す。
- zenohd を LAN に開けるのは作業の間だけ。終わったら ROS 2 の重ね合わせを `down` する (network まで片付ける)。
- 終わったら: sim は sim_down、`build/` は Linux の標準構成 (x86-64) に戻す。
