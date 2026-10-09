# Asterism S3 指示書: 機体で台車を操縦し、カメラの画像を機体の画面に出す

> 状態: 指示 | 更新: 2026-10-10 | S1 の ROS 2 の台車 (MuJoCo、カメラつき) を Modern の機体から操縦し、台車のカメラの JPEG と位置を機体の画面に出す。同じ走りを asterism-console で見て MCAP に記録し、巻き戻す。機体・CRuby・ROS 2・シミュレータが 1 つの網でつながる見せ場を、手順書として残す

前提: plan.md の S の節、report/s1.md・report/s2.md、asterism-console の docs/s1.md・docs/s2.md、親リポジトリの `docker/mujoco/README.md`。

## 1. 決まっていること

- 使う台車は **S1 の ROS 2 の台車** (カメラがあるのはこちらだけ)。S2 の Ruby の台車は別の見せ方として残し、S3 では使わない。
- カメラは S1 のまま: `/camera/image/compressed` (sensor_msgs/CompressedImage、160x120 JPEG、5 Hz、1 枚約 2.8 KB)。
- 機体は Modern (P4)。今つながっているのは P4-Nano。Tab5 での確かめは、つながったときにユーザが行う。
- 操縦者は一度に 1 つ (S2 の申し送り)。手順書もそう書く。

## 2. 作るもの

1. **機体で JPEG を画面に出す方法の調べ**:
   - 今ある経路を先に調べる。
     - 動画の再生 (`fmrb_spx_gfx_video_open`、`display_p4_video.cpp`、`flash/app/modern/video_play.app.rb`): ファイルを入口にする P4 の JPEG の回路。
     - `load_image` / `create_image` (PNG だけ)。
     - 遠隔画面の JPEG (`rd_encoder_jpeg.c`、こちらは符号化)。
   - 受けた JPEG を、ファイル (例: `/tmp` などの RAM の上) に書いて今ある経路で出せるかを確かめる。1 枚ごとの時間と、画面の更新の頻度を測る。flash への書き込みでの往復はしない (flash を傷めるので、繰り返しの書き込みは RAM の上だけ)。
   - **今ある公開の API で出せない場合は、新しい API (Ruby から見える名前・引数) の案を書いて止まる**。名前は使う人から見える仕様なので、ユーザが決める。案には、名前と引数、置く場所 (Modern だけか)、使う記憶 (内蔵 RAM は増やさない。要る場所は PSRAM)、他の経路との関係を書く。止まる場合も、3 と 4 の画像以外の部分は先に仕上げる。
2. **機体の試しのアプリ** (`flash/app/test/rover_cam.app.rb` など、S1 の `ros2_drive.app.rb` を土台にしてよい):
   - 矢印キーで `/cmd_vel` を送る (動かしている間だけ送り、止めたら 1 回止めを送る)。
   - `/odom` の位置・向き・速さを出す。gem の課題 14 (大きな型の読み解きが遅い) の回避は S1 のアプリのやり方に合わせる。
   - カメラの画像を最新の 1 枚だけ持ち、画面に出す (古い画像は捨てる。処理が追いつかなければ間引く)。画像の頻度と遅れを画面に出す。
   - sim と P4-Nano で動かす。sim で JPEG を出す経路が無ければ、sim では画像の大きさと頻度の表示だけにしてよい (その差を report に書く)。
3. **見せ場の手順書**:
   - 親リポジトリの `docker/mujoco/README.md` に節を足す (または `doc/` に別の 1 枚)。ROS 2 の台車の起動、asterism-console の起動、機体のアプリの起動、記録、巻き戻し、片付けまでを、上から順に打てば再現できる形で書く。
   - 画面付きの MuJoCo の viewer (WSLg) を並べる場合の手順も、S1 の手順への参照で足す。
   - 本当の IP・機体の名前・MAC は書かない (`192.0.2.x`、`fmruby-aaaaaa`)。
4. **通しの確かめ**: 機体で操縦しながら、asterism-console のグラフ (機体・ROS 2 のノード・トピックが 1 つの絵に出る)、カメラの画像、odom の線グラフを同時に見る。走りを MCAP に記録し、時間の帯で巻き戻して、画像・線グラフ・グラフの巻き戻しが合うことを確かめる。

## 3. 受け入れ条件

1. 機体 (P4-Nano) から台車を操縦でき、odom の表示が動きに合って変わる。前進 1 m・90 度の旋回で、S1 と同じ程度に合う。
2. カメラの画像が機体の画面に出る。画像の頻度 (目標は 5 Hz、出せなければ実測) と遅れ、更新の 1 周の時間を測る。内蔵 RAM が増えないこと (周期のダンプの値を S1 のときと比べる)。または、1 の止まる条件に当たって API の案を返す。
3. sim で同じアプリが動く (画像の扱いの差は書く)。
4. asterism-console で、機体の操縦中に、グラフ・画像・odom の線グラフが同時に動く。記録して巻き戻せる。
5. 手順書のとおりに上から打てば、見せ場が再現できる。

## 4. 範囲・決まり

- **触ってよいもの**:
  - fmruby-core の `flash/app/test/rover_cam.*`、`doc/ruby_asterism/report/s3.md`。ブランチは `feature/asterism-s2` の先 (S2 がまだ develop に入っていないため) から `feature/asterism-s3` を切る。
  - 親リポジトリの `docker/mujoco/` (足すだけ)。コミットのみ、push しない。
  - asterism-console (main にコミット・push してよい)。
  - asterism の examples (main にコミット・push してよい。gem の版は上げない)。asterism の gem の本体 (mrblib・lib) を直す必要が出たら、コミットせずに差分と理由を report に書いて返す。
- **触らないもの**: `.env`、sdkconfig、機体のファームの C (新しい API が要るなら 2.1 のとおり案を書いて止まる)、fmruby-graphics-audio。
- **公開**: gem の公開はしない。
- **機体**:
  - P4-Nano。本当の IP は親が渡す。ミュート中。
  - 親のシリアルの capture が向いている (`serial_start` を呼ばない)。
  - アプリは tab5_fs で送り、tab5_app で起動する。焼き直しは要らない見込み。
- **終わったら**: compose を `down` し、sim・rails・bridge を止める。`build/` は Linux の標準構成 (x86-64)。
- **止まる条件**:
  - 機体で JPEG を出すのに新しい API が要る (2.1)。
  - sudo やホストへのパッケージの導入が要る。
  - 仕様の選択が要るとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - JPEG を出す経路の調べ (試した経路と実測、選んだ経路)。
  - 受け入れ条件の結果と実測。
  - 見立てと違った点。
  - 残った課題。
