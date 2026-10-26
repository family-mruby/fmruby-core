# R3 指示書: step_sensor を整理する

> 状態: 指示 | 更新: 2026-10-25 | plan.md の R3。R1 (Link)・R2 (FmrbToml・app_settings) の後の `flash/app/test/step_sensor.app.rb` (627 行) を、本筋 (センサを読む・一歩を数える・網に出す・画面) が読める大きさにする。網に出す値・オブジェクトのメソッド・検知の結果は変えない (表示だけは下の 1-4 のとおり変える)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 作るもの

1. **生の値を 1 つの組に**: `@raw_last` の配列と `r[5]` のような番号をやめ、名前のある組 (小さなクラスか Hash) にする。`m_peak` (アプリ) と `dyn_peak` (検知のクラス) の持ち主をそろえる。網に出す accel の map の鍵と値は今と同じ。
2. **調べるための記録を小さく**: 直前 8 標本の dyn を覚えておく `@ring`、処理時間の集計 (`@busy_*`)、10 秒ごとの stats の行。残す価値のあるもの (一歩のログ、10 秒ごとの 1 行) は残してよいが、本筋の処理から外に出す。
3. **一歩の検知を考え直す**: 平均の 3 段 (BASE_TAU / BASE_SLOW_TAU / STEADY)、静止の判定、構え直し (arming)、最短の間隔の扱いを、より少ない仕組みで同じ振る舞いにできないか検討する。**合成の波形の試験 (tool/imu/test/step_detector_test.rb) の数値 (歩行 677 歩の取りこぼし・余分 0、静止 0 歩、振ったあとの戻り、振った直後の歩行、遅れの中央値・90% 点、dyn_peak の試験) を前後で並べ、悪くならないこと**。簡単にできなければ、無理に変えずに理由を report に書く。
4. **歩数の表示**: 矩形で描く 7 セグメント (`draw_digit`・`SEGMENTS`) をやめ、既存のフォントの拡大 (text size の上限、今は 4) で出す (2026-10-25 ユーザ。StickS3 の画面はあまり見ないので小さくてもよい)。上の段の WiFi / AST の表示と下の段の歩調はそのまま (2 倍)。240x135 に収まる配置にする。
5. 冒頭のコメントを今の作りに合わせて短くする (長い経緯は doc/headless_s3/report/s1.md にある)。

## 2. 確認

- `rake test` (step_detector_test、asterism:link_test、toml:test を含む)。合成の波形の試験の数値の前後の表を report に。
- sim (Modern) で step_sensor が起動し、手元の zenohd に繋がり、値を出す (センサが無いので歩数は増えない)。
- **StickS3 の実機** (ファームの焼き直しは要らない。tab5_fs で `/app/test/step_sensor.app.rb` を置き換えて tab5_app で起動し直す。ip は LAN の住所を渡す。コミット・doc に書かない): 画面 (新しい歩数の表示) を tab5_screenshot で撮り、「WiFi ok / AST ok」になる。asterism の MCP (VPS 側) で `step` と `accel` の値が前と同じ鍵で届く。保存してあるしきい値 (今は 100 mg / 250 ms) を読めている (`saved: true`)。しきい値は変えない。
- 内蔵 RAM・ファームは変わらないはず (アプリだけの変更)。

## 3. 範囲・決まり

- **触ってよいもの**: `flash/app/test/step_sensor.app.rb`・`.app.toml`、`tool/imu/test/`、report。
- **触らないもの**: ほかのアプリ、gem (Link・FmrbToml)、sdkconfig、Tab5、VPS、`~/asterism_keys/`、StickS3 の `/home` (config.toml・証明書・step_sensor.toml)。StickS3 を焼かない、シリアルを開かない。
- push・タグはしない。`git commit -a` は使わない。件名 `app:`、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc (この指示書・report) はコミットしない (親がする)。
- 終わったら sim を片付ける。StickS3 は新しい step_sensor で VPS に繋がった状態で残す。
- **止まる条件**: 網に出す値やメソッドを変えないと整理できない、合成の波形の試験の数値が悪くなる。
- **report**: `doc/app_refactor/report/r3.md` (常体): 変えたこと、行数の前後、検知を変えたか (変えたなら試験の数値の前後)、表示の画面、実機の確認、残件、コミット。
