# ヘッドレス S3 M3s 指示書: M5StickS3 を `FMRB_HW_TARGET=STICKS3` として起動する

> 状態: 指示 | 更新: 2026-10-24 | ヘッドレスのファームの種類に M5StickS3 を足す。AtomS3R の構成 (ATOMS3R、H1-H3) を土台に、StickS3 に固有のピンと周辺 (画面 135x240、ボタン、電源の保持、6 軸センサ) だけを書き、WiFi の開発の流れと Asterism まで通す。急ぎ (ユーザの来週の用事に使う) なので、plan.md の M3 にある「M5 の機種をまとめる口 (M5GFX の自動判別)」は今回は作らず、ATOMS3R と同じ書き方で STICKS3 を並べる (まとめるのは後)

言葉: 文書は常体、「確かめ」ではなく「確認」。端末と呼ぶ (「機体」は使わない)。コードのコメントは英語。

前提として読むもの: doc/headless_s3/plan.md、report/h0.md (AtomS3R でのピンの調べ方)、report/h1.md・h2.md・h3.md (ATOMS3R の構成、WiFi・devctl・`/fb`、Asterism)、report/m1_m2.md (`board/board_display.h` の口)、fmruby-core/CLAUDE.md、ルートの CLAUDE.md (MCP の serial_* / flash / tab5_* の使い方)、.claude/skills/device-flash。

## 1. 実機 (2026-10-24 に親が esptool で読んだ)

- ESP32-S3-PICO-1 (LGA56) rev v0.2、内蔵 flash 8 MB (GD、quad、3.3 V)、内蔵 PSRAM 8 MB (AP、3.3 V)、40 MHz、USB-Serial/JTAG。AtomS3R と同じチップ。
- いま WSL に `/dev/ttyACM0` として繋がっていて、**ボタンで入れた書き込み待ち (ROM のダウンロードモード)** にいる。USB の名乗りは `303a:1001` (Tab5・AtomS3R と同じ)。焼く前に、ポートが StickS3 であることを確認する (`/sys/class/tty/ttyACM*/device/../` の busnum・devpath、または esptool の `Chip is ESP32-S3-PICO-1`。Tab5 は ESP32-P4)。
- 出荷時の flash の控えは**とらない** (ユーザ決定)。中身は UiFlow 2.0。
- 公開されている仕様 (plan.md の表): 1.14 型 135x240 (ST7789P3)、ボタン、ES8311 の音・マイク・スピーカ、BMI270、赤外線の送受、250 mAh の電池、Hat2・Grove。

## 2. 作るもの

1. **ピンと周辺を調べる**: 画面 (SPI のピン、DC・CS・RST・背光、向きとずれ)、ボタン、電源の保持 (電池で動かし続けるのに要る出力やチップ。電源の管理の IC があればその初期化)、システムの I2C (SDA・SCL) とそこにいる石 (BMI270 のアドレス、電源の管理、ES8311 など)。公開の資料 (M5Stack の文書・回路図、M5Unified / M5GFX の上流の StickS3 の定義) を読み、実物で照合する (I2C の走査など)。表を report に (h0.md の書き方)。**資料と実物が食い違ったら実物を正とし、report に書く**。
2. **構成 `STICKS3`**: ATOMS3R の構成 (Rakefile の表、rakelib/build.rake、main/CMakeLists.txt、components/fmrb_hal、`fmrb_pin_assign.h`、区画の表、system_conf、storage に入れないファイルの一覧) に並べて足す。中身は ATOMS3R と同じにし、違うのはピン・画面・周辺の初期化だけにする。画面は `board/board_display.h` の口で、135x240 (向きは縦でも横でもよい。どちらにしたかを report に)。
   - **sdkconfig の defaults**: `config/sdkconfig.defaults.atoms3r` を写して `config/sdkconfig.defaults.sticks3` を作るのはよい。**中身を 1 行でも変える必要が出たら止まる** (sdkconfig は編集禁止。案を report に書いて返す)。
3. **電源の保持**: USB を抜いても電池で動き続けること。ボタンでの電源の入り切りの決まりも調べて report に (実装は保持だけでよい)。
4. **ボタン**: Ruby のアプリから押したことが分かる形 (AtomS3R のボタンと同じ口)。
5. **6 軸センサ**: BMI270 が I2C にいてアプリから読めることを確認する (ブランチ `feature/imu-bmi270` に Tab5 向けの未コミットの Ruby の gem がある。今回は取り込まず、I2C で WHO_AM_I (0x24) が読めるところまででよい。取り込みは次の段)。
6. **WiFi・devctl・Asterism**: H2・H3 と同じに動くこと。`asterism_demo` を startup_app にして、手元の zenohd (親の compose、`docker-compose.zenoh-lan.yml`) に TCP で繋がる。WiFi の設定の入れ方は H2 と同じ。`/fb` で画面が撮れる。

## 3. 確認 (受け入れ)

- `FMRB_HW_TARGET=STICKS3 rake clean_all && rake build:esp32` が通り、StickS3 で起動する (シリアルのブートログに `Guru|abort` が 0)。
- 画面: `/fb` (tab5_screenshot に ip を渡す) で `asterism_demo` の画面が撮れる。**実物の画面に正しく出ているか (色・向き・ずれ・背光) はユーザの目視**: そこで止まらず、`/fb` の画像と「ユーザに見てほしい点」を report に書いて先へ進む。
- ボタンを押したことがアプリに届く (ログで確認。押すのはユーザなので、確認の手順を report に書き、自分で押せない部分は残件に)。
- 電池: 電源の保持の出力が立っていることをログか読み値で確認。抜いて動き続けるかはユーザの確認 (手順を report に)。
- BMI270 の WHO_AM_I = 0x24 を読めた記録。
- Asterism: asterism-console の図か `asterism` の CLI で StickS3 のノードが見え、console から呼べる (H3 と同じ確認)。内蔵 RAM の定常 (`IRAM free:`) を H3 の値と並べて report に。
- 既存の構成を壊さない: ATOMS3R・TAB5・NARYAv3 のビルドが通る (共通の部分を変えた場合。変えていなければ ATOMS3R だけでよい)。Retro・Tab5 の内蔵 RAM を増やさない。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) のビルドの設定・ボード固有の部分・区画の表・config/ (sdkconfig.defaults.sticks3 は上の条件つき)・`flash/app/test/asterism_demo.app.rb` (画面の大きさに合わせる程度)、report。
- **触らないもの**: 既存の端末の sdkconfig.defaults と sdkconfig、`.env` の既定 (`FMRB_HW_TARGET` は環境変数で渡す。.env を変えたら最後に戻す)、Tab5 (別の USB にいる。焼かない・リセットしない)、VPS、`~/asterism_keys/`、ユーザの VOICEVOX、submodule の直接編集 (lib/add・lib/patch の決まりで)。
- 実機: StickS3 に限り、serial_* / flash を使ってよい (/home が消えてよい新しい端末)。ポートは明示する。焼いたあとに USB の名乗りが変わって WSL から外れたら、`powershell.exe -Command "usbipd attach --wsl --busid 1-5"` で繋ぎ直してよい (bind はユーザ済み)。
- 実装は「ヘッドレス専用のコードはなるべく書かない」(plan.md 決定事項) を守る。
- push・タグはしない。`git commit -a` は使わない。作業は develop の上で、件名は `board:` / `build:` など履歴に合わせ、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。fmruby-core の doc (この指示書・report) はコミットしない (親がする)。
- 端末固有の値 (MAC、mDNS の名前の後半、WiFi の SSID・鍵、LAN の IP) を report やコミットに書かない (`fmruby-XXXXXX` のように伏せる)。
- **止まる条件**: sdkconfig を変える必要がある、既存の端末の内蔵 RAM が増える、範囲外 (共通の部分の大きな変更、submodule) が要る、焼いても起動せず原因が見えない、ユーザの操作 (ボタンで書き込み待ちに入れ直す、など) が無いと先へ進めない。止まったら report に状況と次の一手を書いて返す。
- **report** (`doc/headless_s3/report/m3s.md`、常体): ピンと周辺の表 (資料と実物の照合)、構成の作り方、画面の向き、電源の保持、ボタン、BMI270、WiFi・Asterism の確認、内蔵 RAM、ユーザに目視・操作してほしい点、残件、コミット。
