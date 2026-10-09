# Asterism S2 指示書: Ruby で MuJoCo を回し、台車を Asterism のオブジェクトとして公開する

> 状態: 指示 | 更新: 2026-10-10 | CRuby から MuJoCo の C の API を FFI (標準の Fiddle) で直接呼んで物理を回し、S1 の台車を Asterism のオブジェクトとして網に公開する。機体 (P4-Nano・sim) と CRuby がそのオブジェクトを呼んで操縦する。ROS 2 は使わない

前提: plan.md の S の節、report/s1.md (S2 への申し送り)、asterism の README (portable API / CRuby API)。

## 1. 作るもの

1. **MuJoCo の Ruby の薄い皮** (FFI):
   - Ruby の標準の Fiddle で `libmujoco.so` を読み、要る関数だけを包む。
     - モデルの読み込み (`mj_loadXML`)、データ (`mj_makeData`)、`mj_step`、`mj_forward`。
     - 名前から番号 (`mj_name2id`)。
     - qpos / qvel / ctrl / sensordata / xpos / xquat の読み書き。
     - 解放。
   - 構造体の中の位置 (`mjModel` / `mjData` の各配列の先頭) は、使う MuJoCo の版のヘッダから求める。版を固定し、版が違えば止まる検査を入れる。
   - **libmujoco の取り方**: 公式のリリースのビルド済み (Linux x86_64 の tar) を、版と sha256 を固定して取る。asterism-zenoh の zenoh-c の取り方と同じ考え方。docker の像に入れるか、手元で取るかを決めて書く。
   - 置き場所: asterism のリポジトリの `examples/mujoco/` (gem には入れない)。README とライセンスの表示 (MuJoCo は Apache-2.0) を付ける。
     将来 gem に切り出す余地を残す。
2. **物理を回すプロセス** (CRuby): S1 の `rover.xml` / `scene.xml` を読み、実時間に合わせて `mj_step` を回す。
   - 車輪は速度で動かし、S1 と同じく加速の上限を Ruby の側で付ける。
   - Asterism のオブジェクトとして公開する (例: ノード `mujoco`、アプリ `rover`):
     - `drive.cmd(v, w)`: 並進と回転の速さ。差動の 2 輪に直す。一定時間 (0.5 s) 命令が来なければ止まる。
     - `drive.stop`。
     - `state.pose` (x, y, 向き、本当の位置)、`state.odom` (車輪から計算した位置)、`state.imu`、`state.speed`。
     - `world.reset`。
     - `world.objects` (柱と箱の位置)。
   - 値は MessagePack で渡せる形 (Hash / Array / 数)。
   - 状態は、一定の頻度 (例: 10 Hz) で `asterism/mujoco/rover/state` などのキーにも put して、asterism-console の線グラフ・記録で見られるようにする。キーの形は V3 の線グラフが読める MessagePack の Hash にする。
   - カメラは S2 ではしない (S3 で S1 の ROS 2 のカメラを使う)。
3. **操縦する側**:
   - CRuby の例: キーボードで `drive.cmd`、状態を表示する。
   - Family mruby の試しのアプリ (`flash/app/test/asterism_rover.app.rb` など): portable API で `mujoco/rover/drive` を呼び、`state.pose` を画面に出す。矢印キーで操縦する。sim と P4-Nano で動かす。
4. **asterism-console**: 公開されたオブジェクトがグラフの入れ子に出て、画面から `drive.cmd` を呼べることを確かめる (許可表に足す)。状態のキーを線グラフで見る。

## 2. 受け入れ条件

1. CRuby だけ (ROS 2 なし) で MuJoCo が実時間で回る。1 秒あたりの `mj_step` の数と、Ruby の側の負荷を測る。版の検査が効く。
2. 機体 (P4-Nano と sim) と CRuby から `drive.cmd` で台車を操縦でき、`state.pose` が動きに合って変わる。
   - 前進 1 m と 90 度の旋回で、本当の位置と計算した位置を比べる。
3. 命令が途絶えると止まる。2 つの側から同時に命令が来たときの振る舞いを決めて書く (例: 最後の命令が勝つ、または握っている側だけ)。仕様の選択になるなら、案を書いて止まる。
4. asterism-console のグラフに `mujoco` のノードが入れ子で出る。画面から `drive.cmd` を呼べる。状態のキーの線グラフが動く。走りを記録して見返せる。
5. MuJoCo の取り方・版・ライセンスの表示が README にある。本当の IP・機体の名前・MAC はコミットしない。

## 3. 範囲・決まり

- **触ってよいもの**:
  - asterism の `examples/mujoco/` (main にコミット・push してよい。gem の版は上げない)。
  - 親リポジトリの `docker/mujoco/` と compose の重ね合わせ (足すだけ)。
  - asterism-console (main にコミット・push してよい)。
  - fmruby-core の `flash/app/test/asterism_rover.*` と `doc/ruby_asterism/report/s2.md`。fmruby-core のブランチは develop から `feature/asterism-s2` を切る。
- **触らないもの**: `.env`、sdkconfig、機体のファームの C、fmruby-graphics-audio。
- **公開**: gem の公開はしない。
- **機体**:
  - P4-Nano。本当の IP は親が渡す。ミュート中。
  - 親のシリアルの capture が向いている (`serial_start` を呼ばない)。
  - アプリは tab5_fs で送り、tab5_app で起動する。
- **終わったら**: compose と sim を止める。`build/` は Linux の標準構成 (x86-64)。
- **止まる条件**:
  - libmujoco を FFI で安全に呼べない (構造体の扱いが版に強く依存して危ない) と分かったとき。Python の mujoco を挟む案と比べて、案を書いて返す。
  - 仕様の選択が要るとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - FFI の包み方。
  - 受け入れ条件の結果と実測。
  - S3 への申し送り。
