# ヘッドレス Family mruby (ESP32-S3、AtomS3R)

> 状態: 進行中 (H2 まで完了) | 更新: 2026-10-12 | 画面と操作卓を持たない市販の小型機 (最初は M5Stack AtomS3R) を、単一のアプリだけを動かす Family mruby として Asterism の網に参加させる。開発は WiFi 越し (遠隔のファイル転送・起動) と Asterism の system オブジェクト

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

- 実機で読んだ値 (2026-10-11、esptool): ESP32-S3-PICO-1 (rev v0.2)、内蔵 flash 8 MB (GD、quad、3.3 V)、内蔵 PSRAM 8 MB (AP、3.3 V)、
  USB-Serial/JTAG。USB の VID:PID は Tab5 と同じ `303a:1001` なので、ポートは明示して取り違えない。
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

- **ヘッドレス専用のコードはなるべく書かない**。新しく要る仕組みは、どの機体でも使える一般の機能か設定として作り、
  AtomS3R はその組み合わせ (ボード分岐・sdkconfig・区画の表・system_conf) で成り立たせる。例:
  - 単一アプリでの起動は、全機種で使える system_conf の設定 (起動するアプリ 1 本、デスクトップを出さない) にする。
  - 小さい画面は、画面の大きさの設定 (128x128) として既存の描画・窓の仕組みに載せる。ミニ版のデスクトップを作るなら、
    system_desktop の設定 (小さい画面向けの配置) として作り、別のデスクトップを書かない。
  - 開発の入口 (遠隔のファイル転送・Asterism の system オブジェクト) は既存の機能と、全機種で使える gem のまま使う。
  - 外すもの (言語・資源) は、既存のビルドの選択肢で外す。外すための専用の分岐を増やさない。
  - ボード固有として書くのは、ピンの割り当てと周辺 (画面のドライバ・LED・ボタン・センサ) の初期化だけ。

- 小さい画面: アプリが描画する / system_desktop のミニ版を作る、のどちらか (状態の表示だけ・使わない、は採らない)。
- S3 での zenoh (Asterism Z4) は AtomS3R から行う。Retro は優先度を下げる。
- アプリの言語: mruby を基本に、flash と RAM に余裕があれば MicroPython も入れる。

## 決定事項 (2026-10-11 ユーザ、おすすめを採用)

1. **単一アプリ**: system_conf に起動するアプリを 1 本書き、デスクトップ・ランチャーを出さない。落ちたら 3 回まで起動し直し、
   それでも落ちたら止めて、画面とシリアルに理由を出す。キーの名前は既存の system_conf の流儀に合わせて H1 で決め、全機種で
   使える設定として作る。
2. **小さい画面**: まずは**アプリが描く**。system_desktop のミニ版は後に回す (H5)。
3. **開発の入口の順番**: **遠隔のファイル転送とアプリの起動**から始め (H2)、次に Asterism の system オブジェクト (H4)。
4. 実機は届いた (2026-10-11)。MicroPython の余地は H3 の後に flash と RAM を測ってから決める。

## 決定事項 (2026-10-12 ユーザ、H0 の後。report/h0.md 8 章のおすすめを全部採用)

1. 区画は A 案: factory 5M + storage 3008K。
2. 保存領域 (storage) に入れないファイルの一覧を、端末ごとに持てる一般の仕組みを足す。何を外すかは H1a で大きさを測ってから。
3. BLE は外す。外すためのビルドの条件は全端末で使える一般の形にする (今は ble_task が常にコンパイルされる)。
4. Lua・BASIC・MicroPython などを外す一般のビルドの選択肢 (`FMRB_LANG_*` など) は、H1a の大きさを見てから決める。
5. system_conf のキー: `boot_mode = "desktop" | "app"`、`startup_app_retries = 3` (既存の `startup_app` と組み合わせる)。全端末で使える。
6. フラグ: 「端末の中で描く」`FMRB_HAS_LOCAL_DISPLAY`、「P4 の描画回路がある」`FMRB_HAS_PPA` を、`FMRB_HW_MODERN` から切り分ける。`FMRB_HW_FAMILY` の新しい値は `mini`。
7. 画面のピンは新しく `FMRB_PIN_LCD_SPI_*`。
8. H4 の「LED」は、画面の背光の明るさと、画面そのものをオブジェクトにする (RGB LED は無かった)。
9. 人の目の確かめ (ボタンの押下、背光の反応、緑の LED のちらつき) は H1b でユーザが行う。
- sdkconfig の defaults は report/h0.md 5 章の案 (n16r8 との差分) で作ってよい (承認済み)。

- 5 MB に収める (2026-10-12 ユーザ、A + B): (A) `FMRB_APP_ENGINE_EDITOR=mruby` と `FMRB_FFT_SPINEL=0` を ATOMS3R の既定にする。(B) 日本語のフォントを外す一般のビルドの選択肢 `FMRB_FONT_JA` を足し、ATOMS3R は 0 (外すと `set_font(:ja, …)` は美咲 8 ドット)。言語を外す選択肢 (C) は足りなくなったら。

## 段階

| 段階 | 内容 | 確かめること |
|---|---|---|
| H0 (完了 2026-10-12、report/h0.md) | 実機の確認 (画面は GC9107 だった・RGB LED は無く LP5562 は背光だけ・出荷時の flash は端末の外に保存済み): 回路図と実物でピンの割り当て (画面 ST7735、LED、ボタン、I2C、赤外線、Grove、底面の GPIO) を照合し、`fmrb_pin_assign.h` の案を作る。出荷時のファームでの起動の様子を記録する | ピンの表の食い違いが解消している |
| H1 (完了 2026-10-12、report/h1.md。画面の目視はユーザが合格) | 新しい構成 `FMRB_HW_TARGET=ATOMS3R` でビルドして起動する。区画の表 (8 MB、新規)、sdkconfig の defaults (新規。**中身はユーザの承認を得てから**)、単一アプリの設定 (全機種で使える system_conf の設定)、ヘッドレスで要らないものを既存のビルドの選択肢で外す。描画は S3 の上で行う (Retro は描画を子マイコンに出すが AtomS3R には無い。wasm 版の描画の経路 `display_backend.cpp` のように、機体の中で描く経路を使えるかを最初に調べる)。128x128 の画面へ出す | 起動の速さ、M1 行の内蔵 RAM、flash の残り、試しのアプリが画面に描ける |
| H2 (完了 2026-10-12、report/h2.md。H1e を含む) | WiFi と開発の入口: WiFi につなぎ、dev_remote_ctl の `/fs/*`・`/app/*` で put → 起動し直しの開発ループを回す (MCP の tab5_fs / tab5_app に ip を渡して使う)。ログは USB のシリアル | 再 flash なしでアプリを差し替えられる |
| H3 | Asterism (Z4): picoruby-asterism-zenoh と asterism を S3 のビルドに入れる。zenoh-pico の確保は PSRAM だけ (Z2 の作り)。`asterism_demo` を動かし、asterism-console の図に ▣ のノードとして出す | 内蔵 RAM の残り (WiFi 常時 + zenoh)、flash の残り、呼び出しの往復時間 |
| H4 | 周辺と system オブジェクト: LED・ボタン・6 軸センサ (BMI270)・赤外線を Asterism のオブジェクトとして公開する。system オブジェクト (ファームの状態、アプリの起動し直し、ログ) を、全機種で使える gem の機能として作る | console から LED を点け、ボタンの押下とセンサの値が届く |
| H5 (後で) | system_desktop のミニ版 (小さい画面向けの配置の設定として)。MicroPython を入れる余地があれば入れる | |

- どの段も「ヘッドレス専用のコードはなるべく書かない」(上の決定事項) を守る。ボード固有として書くのはピンと周辺の初期化だけ。
- 既存の機種 (Retro・Modern) の内蔵 RAM を増やさない。共通の部分を変えたら、Retro と Tab5 のビルドが通ることを確かめる。
- 進め方は Asterism と同じ (段ごとに指示書 → サブエージェントが実装と report → 親が検収)。仕様 (system_conf のキー、
  system オブジェクトの名前と形) はユーザが決める。

## 関連

- doc/ruby_asterism/node_variants.md (ノードの種類、3 層)、design.md (gem・キー空間)、plan.md (Z4)
- doc/stamp_p4/ (P4 のヘッドレス機)、doc/dev_remote_ctl/ (遠隔のファイル転送・起動)
- doc/archive/idf_seam/ (継ぎ目)、doc/wasm/ (描画・音の stub)
