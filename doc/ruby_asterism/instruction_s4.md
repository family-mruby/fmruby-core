# Asterism S4 指示書: 端末 2 台 (P4-Nano が台車の頭脳、ブラウザ版が操縦機)

> 状態: 指示 | 更新: 2026-10-12 | P4-Nano が台車の頭脳になり、Asterism のオブジェクト `brain/rover` を公開して、ROS 2 の台車への命令と制御の輪を受け持つ。ブラウザ版の Family mruby が操縦機になり、頭脳のオブジェクトを呼び、台車のカメラを見る。asterism-console で全体を見て記録する

前提: plan.md の S の節 (特に「S4: 端末 2 台」)、report/s1.md・s2.md・s3.md・b1.md、doc/ruby_asterism/browser.md (ブラウザ版を網に入れる手順)、親の `docker/mujoco/README.md` (S3 の通しの手順)、asterism の README (portable API)、asterism-console の docs (v2_1-v2_4)。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。ロボットは「台車」。

## 1. 決まっていること (2026-10-11・10-12 ユーザ)

| 役 | 端末 | すること |
|---|---|---|
| 頭脳 | P4-Nano (今の構成のまま) | `brain/rover` を公開する。ROS 2 の `/cmd_vel` を出すのは頭脳だけ。`/odom` を見て「進む」「回る」の制御の輪を回す。画面に位置・速さ・今の仕事・呼んできた相手を出す |
| 操縦機 | ブラウザ版の Family mruby (B1) | キーで頭脳を呼ぶ。台車のカメラ (ROS 2 のトピックを直接受ける) と頭脳の状態を出す |
| 見る | asterism-console | 網の図、呼び出しの記録、線グラフ、記録と巻き戻し |

頭脳のオブジェクト (叩き台で進める。アプリ `brain`、オブジェクト `rover`):

- `rover.drive(v, w)`: 直接の速さ (m/s、rad/s)。0.5 s 呼ばれなければ止まる。
- `rover.go(m)` / `rover.turn(deg)`: 仕事を頼んですぐ戻る (仕事の番号を返す)。頭脳が `/odom` で止める。
- `rover.stop`: 仕事も速さも止める。
- `rover.state`: 位置・向き・速さ・今の仕事 (番号・種類・進み具合・終わったか)。
- 仕事の途中で別の仕事や `drive` が来たら、新しいものが勝つ (今の仕事は取り消し)。
- 速さと加速の上限は頭脳が持つ (S1・S2 の値に合わせる)。

## 2. 作るもの

1. **頭脳のアプリ** (`flash/app/test/rover_brain.app.rb` と `.app.toml`):
   - S3 の `rover_cam.app.rb` を土台にしてよい (ROS 2 の `/cmd_vel` を出す・`/odom` を受ける部分)。カメラは頭脳では受けなくてよい (受けるなら画面に小さく)。
   - `brain/rover` を portable API で公開する。`go` / `turn` は `/odom` から距離と角度を測り、目標に近づいたら速さを落として止める。
   - 画面: 位置・向き・速さ、今の仕事と進み具合、最後に呼んできた相手 (呼び出しの送り手が分かるなら)。
   - sim と P4-Nano で動かす。
2. **操縦機のアプリ** (`flash/app/test/rover_pilot.app.rb` と `.app.toml`):
   - 頭脳を見つける: `brain/rover` を公開しているノードを、生存の監視 (on_join など) で見つける。見つからない間は画面にそう出す。複数あれば最初のものでよい (画面に名前を出す)。
   - キー: 矢印で `drive` (押している間だけ送る、離したら止まる・または S3 と同じ「押すと速さが段で変わる」形。操作しやすい方を選んで docs に書く)、`g` で `go(1.0)`、`t` で `turn(90)`、スペースで `stop`。
   - 画面: 台車のカメラ (ROS 2 の `/camera/image/compressed` を直接受ける)、頭脳の `rover.state` (一定の間隔で呼ぶ)、呼び出しの往復時間。
   - ブラウザ版で動かす (B1 の手順)。同じアプリが sim と P4 でも動くこと (P4 で操縦機を試すのは任意)。
3. **通しの手順書**: 親の `docker/mujoco/README.md` に S4 の節を足す (台車・zenohd の LAN と ws・console・頭脳 (P4-Nano)・操縦機 (ブラウザ版) の起動、操縦、記録と巻き戻し、片付け)。本当の IP・端末の名前は書かない (`192.0.2.x`、`fmruby-aaaaaa`)。

## 3. 受け入れ条件

1. 頭脳 (P4-Nano) が `brain/rover` を公開し、asterism-console の図に頭脳のノードとオブジェクトが出る。console から `rover.state` を呼べる。
2. 操縦機 (ブラウザ版) が頭脳を見つけ、`drive` で台車が動く。`go(1.0)` と `turn(90)` で、`/ground_truth/odom` と比べて距離と角度の誤差を測る (目安: 5 cm・5 度以内。届かなければ実測を書く)。
3. 仕事の途中の `stop` と、新しい仕事による取り消しが効く。操縦機が呼ばなくなったら (0.5 s) 止まる。
4. 操縦機の画面に台車のカメラが出る (頻度と遅れを測る)。
5. console で、ブラウザ版 → P4-Nano の呼び出しと、P4-Nano → 台車のトピックが同時に見える。走りを記録して巻き戻せる。
6. P4-Nano の内蔵 RAM が S3 のときと比べて増えていない (周期のダンプの値)。
7. 手順書のとおりに上から打てば再現できる。

## 4. 範囲・決まり

- **触ってよいもの**:
  - fmruby-core: `flash/app/test/rover_brain.*`、`rover_pilot.*`、`doc/ruby_asterism/report/s4.md`。ブランチは develop から `feature/asterism-s4`。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` にユーザの手元の変更がある)。
  - 親リポジトリ: `docker/mujoco/README.md` に節を足す。コミットのみ。
  - asterism-console: 要る直しがあれば main にコミット・push してよい (呼び出しの矢印の見せ方など。図の既定の見え方を変える選択が要るなら止まって返す)。
  - asterism の examples: 足してよい (main にコミット・push してよい。gem の版は上げない)。asterism の gem の本体 (mrblib・lib) を直す必要が出たら、コミットせずに差分と理由を report に書いて返す。
- **触らないもの**: `.env`、sdkconfig、ファームの C、fmruby-graphics-audio、公開済みの gem。
- **端末**:
  - P4-Nano。IP は親が渡す (遠隔の画面とファイルの道具 tab5_* に ip を渡して使う)。ミュート中。`serial_start` は呼ばない。焼き直しは要らない見込み。
  - ブラウザ版は web_* ツールで動かす。ブラウザ版の bundle は B1 で作ったもの (develop にマージ済み) を使い、要ればビルドし直す。
- **終わったら**: 起動したものを全部止める (compose、sim、ブラウザ、rails、bridge、P4 のアプリ)。`build/` は Linux の標準構成 (x86-64)。
- **止まる条件**:
  - 頭脳のオブジェクトの形 (1 章) で足りない・変えたい点が出た (仕様の選択)。案を書いて返す。
  - gem の本体の直しが要る。
  - 端末が応答しなくなり、遠隔の道具で戻せない (シリアルのログが要る)。
- **report**: Japanese plain style (常体)。書くこと: 頭脳の制御の輪の作り (止め方・上限)、操縦機の頭脳の見つけ方とキーの形、受け入れ条件の結果と実測、見立てと違った点、残った課題。
