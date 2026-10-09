# Asterism S1 指示書: MuJoCo の 2 輪の台車を ROS 2 経由で動かす

> 状態: 指示 | 更新: 2026-10-10 | mujoco_ros2_control で自作の 2 輪の台車を MuJoCo の中で動かし、Asterism の側 (CRuby・sim・P4-Nano) から ROS 2 の `/cmd_vel` で操縦し、`/odom`・`/joint_states`・IMU・カメラ (160x120 JPEG 5 Hz) を受け取る。asterism-console で見て記録する

前提: plan.md の S の節 (決定事項)、report/r1.md-r3.md、asterism-console の docs/v2.md-v4.md、親リポジトリの `docker-compose.ros2.yml`。

## 1. 決まっていること (2026-10-09 ユーザ)

- つなぐ部品は mujoco_ros2_control (ros-controls の公式、`ros-jazzy-mujoco-ros2-control`)。
- 台車は自分で書く: 箱の車体、2 輪、補助輪、カメラ、IMU。
- カメラは 160x120 の JPEG を 5 Hz (sensor_msgs/CompressedImage)。

## 2. 作るもの

1. **docker の像**:
   - ROS 2 Jazzy の像に、次を足した重ね合わせを作る (例: `docker/mujoco/Dockerfile` と `docker-compose.mujoco.yml`)。既存のファイルの意味は変えない。
     - mujoco_ros2_control
     - ros2_controllers (diff_drive_controller、joint_state_broadcaster、imu_sensor_broadcaster など)
     - robot_state_publisher
     - カメラの圧縮に要るもの
   - zenohd には、rmw_zenoh でつなぐ (今の ros2 の重ね合わせと同じ)。
2. **台車のモデル**:
   - URDF (ros2_control の記述つき) と、mujoco_ros2_control が要る形 (MJCF など。使う版の文書と例で確かめる)。
   - 車輪の半径・間隔は、diff_drive_controller の設定と合わせる。床、照明、目印になる物 (箱や色の柱) を少し置き、カメラに写るものを用意する。
   - ファイルは親リポジトリの `docker/mujoco/` などに置く。自作なので MIT (Family mruby と同じ) と書く。
3. **カメラ**:
   - mujoco_ros2_control のカメラの部品で画像を出す。
   - 160x120、5 Hz の `sensor_msgs/CompressedImage` (JPEG) にして出す (圧縮と間引き)。トピック名は例えば `/camera/image/compressed`。
   - 画面の無い描画 (MUJOCO_GL=egl か osmesa) が docker の中で動く方を使う。GPU を docker に渡す仕組み (nvidia の container toolkit) が無ければ osmesa でよい。
   - 描画が重すぎて 5 Hz が出ない場合は、実測を書いて止まる。
4. **Asterism の側**:
   - CRuby の操縦の例: asterism-console か parent の tools に置く。キーボードや一定の動き (前進・旋回) で `/cmd_vel` を送り、`/odom` の位置と IMU を表示する。
   - Family mruby の試しのアプリ (`flash/app/test/ros2_drive.app.rb` など): 矢印キーで `/cmd_vel` を送り、`/odom` の位置・向きと速さを画面に出す。sim と P4-Nano で動かす。カメラの画像を機体に出すのは S3 (この段階ではしない)。
5. **asterism-console**:
   - `sensor_msgs/CompressedImage` を、値を眺める欄・時間の帯 (V4)・トピックの詳細で、画像として出せるようにする (JPEG はブラウザがそのまま表示できる)。
   - 記録 (V4) で、台車の走り (cmd_vel、odom、imu、camera) を MCAP に記録し、時間の帯で画像と線グラフを見られる。

## 3. 受け入れ条件

1. docker の重ね合わせで MuJoCo の台車が起動し、`ros2 control list_controllers` で diff_drive_controller と joint_state_broadcaster が active。
2. CRuby の例と、sim / P4-Nano の試しのアプリから `/cmd_vel` を送ると台車が動き、`/odom` の位置がそれに合って変わる (前進 1 m・その場で 90 度旋回 などで確かめる)。
3. `/joint_states` と IMU が出る。カメラが 160x120 の JPEG で 5 Hz 前後 (実測の頻度と 1 枚の大きさを書く)。
4. asterism-console のグラフに MuJoCo 側のノード・トピックが出る。カメラの画像が画面で見える。線グラフで odom の x・y が動く。走りを記録して、時間の帯で画像と値を見返せる。
5. 画面付きの MuJoCo の viewer (WSLg) を出す手順を README に書く。目で見る確かめはユーザが行う。
6. 起動から止めるまでの手順が README にある。本当の IP・機体の名前・MAC はコミットしない。

## 4. 範囲・決まり

- **触ってよいもの**:
  - 親リポジトリの `docker/mujoco/`、compose の重ね合わせ (足すだけ)、`tools/`。
  - fmruby-core の `flash/app/test/ros2_drive.*`、`doc/ruby_asterism/report/s1.md`。fmruby-core のブランチは develop から `feature/asterism-s1` を切る。
  - asterism-console (main にコミットと push を許す)。
  - asterism の examples (main にコミット、push は許す。gem の版は上げない)。
- **触らないもの**: `.env`、sdkconfig、機体のファームの C、fmruby-graphics-audio。
- **公開**: gem の公開はしない。
- **機体**:
  - P4-Nano。本当の IP は親が渡す。ミュート中。
  - 親のシリアルの capture が向いている (`serial_start` を呼ばない)。
  - アプリは tab5_fs で送り、tab5_app で起動する。焼き直しは要らない見込み。
- **終わったら**: compose を `down` し、sim を止める。`build/` は Linux の標準構成 (x86-64)。
- **止まる条件**:
  - mujoco_ros2_control が Jazzy の像に入らない、動かない (代わりの案を書く)。
  - 描画で 5 Hz が出ない。
  - sudo やホストへのパッケージの導入が要る。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - 構成 (像・モデル・制御器・カメラの流れ)。
  - 受け入れ条件の結果と実測 (頻度・大きさ・CPU)。
  - 見立てと違った点。
  - S2・S3 への申し送り。
