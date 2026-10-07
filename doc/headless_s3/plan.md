# ヘッドレス Family mruby (ESP32-S3、AtomS3R)

> 状態: 構想 | 更新: 2026-10-07 | 画面と操作卓を持たない市販の小型機 (最初は M5Stack AtomS3R) を、単一のアプリだけを動かす Family mruby として Asterism の網に参加させる。開発は WiFi 越し (遠隔のファイル転送・起動) と Asterism の system オブジェクト

## 目的

- 画面の無い・小さい市販品を、Asterism の網 (doc/ruby_asterism/) の機体として使えるようにする。Asterism は機体の数が
  増えるほど価値が出るので、安くて小さい機体が入れることに意味がある。
- Family mruby の「アプリを Ruby で書いて、再 flash なしで送って動かす」開発の仕方を、画面の無い機体でも使えるようにする。

## 位置づけ

- doc/ruby_asterism/node_variants.md 3 章の「ヘッドレス Family mruby」は P4 (M5Stamp-P4 + C6、doc/stamp_p4/) を想定した
  「頭脳つき子機」(多重 VM・サービスホスト・遠隔画面のエディタ)。本件はそれより一段軽い **S3 の単一アプリ機**。
  3 層 (親機 / 頭脳つき子機 / 末端子機) の中では、頭脳つき子機と末端子機の間に入る。
- Family mruby の作り (描画をリンクの先に出す、IDF の継ぎ目 (archive/idf_seam)、wasm の stub) を使い、新しい移植ではなく
  「描画の相手のいない S3 の構成」として作る。
- Asterism の Z4 (S3 で zenoh) は、Retro より先にこの機体で行う (2026-10-07 ユーザ決定。Retro は優先度を下げる)。

## 対象: M5Stack AtomS3R (公式の仕様、2026-10-07 時点)

| 項目 | 内容 |
|---|---|
| SoC | ESP32-S3-PICO-1-N8R8 (Xtensa LX7 2 コア 240 MHz) |
| flash / PSRAM | 8 MB / 8 MB (Octal) |
| 画面 | 0.85 型 IPS 128x128 (ST7735)。**画面はある** (小さい) |
| 入力 | ボタン 1 個 |
| センサ | BMI270 (6 軸) + BMM150 (地磁気、BMI270 の補助 I/F の先)。システムの I2C |
| その他 | RGB LED (LP5562)、赤外線の送信、HY2.0-4P (Grove)、底面に GPIO 6 本 |
| USB | Type-C、チップ内蔵の USB-Serial-JTAG |
| 大きさ | 24.0 x 24.0 x 12.9 mm、6.8 g |

- ピンの割り当ては公式の資料で確かめてから `fmrb_pin_assign.h` に起こす (読んだ表に重複があったため、実物と回路図で照合する)。
- 小さい画面があるので「完全な画面なし」ではない。画面は**アプリが描画する**か、**system_desktop のミニ版**
  (ID・接続・動いているアプリ・エラーを出す小さいデスクトップ) を作るか、のどちらか (2026-10-07 ユーザ。どちらにするかは
  計画のときに決める)。画面の無い製品 (AtomS3 Lite 系など) にも同じ構成が使える形にする。

## 方針 (案)

### 単一アプリで動かす

- カーネルの上で、デスクトップ・窓の管理・ランチャーを起動しない。設定ファイル (system_conf) で起動するアプリを 1 本指定し、
  それだけを動かす。アプリが落ちたら決めた回数まで起動し直す (サービスホストの自動再 spawn の考え方)。
- 描画は 128x128 の小さい画面へ (上の 2 案)。音 (APU) は無し。audio_backend を空にする形は wasm で確認済み。
- 浮いた内蔵 RAM と PSRAM を、zenoh と 1 本のアプリの VM に回す。

### 開発の入口

- **S3 では WiFi と BLE を同時に使わない** (今の S3 の構成は RAM の都合で排他)。
  Asterism には WiFi が要るので、開発の入口は WiFi 越しを主にする。
  1. **WiFi 越しの遠隔のファイル転送とアプリの起動** (dev_remote_ctl の `/fs/*`・`/app/*`。Retro でも使えている)。
     put → 起動し直し の、再 flash なしの開発ループ。MCP の tab5_fs / tab5_app 相当で操作できる。
  2. **Asterism の system オブジェクト**: 機体が `asterism/<ID>/system/...` に、ファイルを置く・アプリを起動し直す・
     ログを流す・状態を返すオブジェクトを公開する。PC や Tab5 のエディタから、どこからでも (外のルータ越しでも) 管理できる。
  3. **USB のシリアル**: ログと、最初の WiFi の設定。
- BLE は使わないか、最初の WiFi の設定を入れる 1 回にだけ使う (BLE のコンソールで開発する案は、WiFi との排他のため主に
  しない)。

### 画面の代わり

- doc/ruby_asterism/remote_window.md の「窓 1 枚の転送」で、アプリの画面を Tab5 や PC の窓に出す。後の段階。

## 確かめること・気をつけること

- **flash 8 MB**: Retro (N16R8) のファームは factory 6M の区画で約 3 分の 2 を使っている (約 4 MB)。zenoh と Asterism で
  さらに増える。8 MB に「アプリの区画 + storage」を収めるには、ヘッドレスで要らないもの (描画の資源、ランチャーの絵、
  BASIC・Lua・MicroPython などの言語、サンプル) をビルドから外す構成が要る。区画の表は新しく作る (n8r8 の表は factory 3M・
  storage 4900K で足りない見込み)。
- **内蔵 RAM**: S3 はいちばん苦しい機種。zenoh-pico の確保は PSRAM だけにする作り (Z2) をそのまま使う。WiFi を常時使う
  構成で、内蔵 RAM の残りを最初に測る。
- **ATOM_DISPLAY との関係**: AtomS3 (n8r8) 向けの構成は 2026-08 からサポートを中断していてビルドが通らない。これを直すの
  ではなく、新しい構成 (例: `FMRB_HW_TARGET=ATOMS3R`) として作る。ATOM を除外している WiFi まわりの条件は壊さない。
- **周辺**: BMI270 は既存の gem (picoruby-bmi270) が使える見込み。LED・ボタン・赤外線・Grove は Asterism のオブジェクトとして
  公開する候補 (例: `asterism/<ID>/atom/led`)。
- **電源と起動**: USB 給電のみ。起動の速さは譲れない要件 (長い黒画面で隠さない) を、画面が小さくても守る。

## 決定事項 (2026-10-07 ユーザ)

- 小さい画面: アプリが描画する / system_desktop のミニ版を作る、のどちらか (状態の表示だけ・使わない、は採らない)。
- S3 での zenoh (Asterism Z4) は AtomS3R から行う。Retro は優先度を下げる。
- アプリの言語: mruby を基本に、flash と RAM に余裕があれば MicroPython も入れる。

## 未確定事項

1. **単一アプリの指定と落ちたときの扱い**: system_conf のキーの名前、起動し直す回数、全部失敗したときの表示。
2. **小さい画面**: アプリが描画するか、system_desktop のミニ版か。ミニ版なら単一アプリとの関係 (アプリの切り替えを持つか)。
3. **開発の入口の順番**: 遠隔のファイル転送 → Asterism の system オブジェクト (推奨) / 最初から system オブジェクト。
4. **MicroPython を入れる余地**: flash 8 MB の中で、mruby + zenoh + Asterism の後に残る量を測ってから。
5. **実機の調達**: 手元に AtomS3R が届いてから計画にする。

## 関連

- doc/ruby_asterism/node_variants.md (ノードの種類、3 層)、design.md (gem・キー空間)、plan.md (Z4)
- doc/stamp_p4/ (P4 のヘッドレス機)、doc/dev_remote_ctl/ (遠隔のファイル転送・起動)
- doc/archive/idf_seam/ (継ぎ目)、doc/wasm/ (描画・音の stub)
