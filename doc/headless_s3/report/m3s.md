# M3s 報告: M5StickS3 を `FMRB_HW_TARGET=STICKS3` として起動する

> 状態: 完了 (検収待ち、画面・ボタン・電池はユーザの確認待ち) | 更新: 2026-10-24 | instruction_m3s.md の記録。AtomS3R の構成に並べて STICKS3 を足し、StickS3 で起動した。画面 (ST7789P3、240x135 の横向き) は `/fb` で asterism_demo の絵が撮れ、WiFi・devctl・Asterism (手元の zenohd に TCP) が H2・H3 と同じに動く。システムの I2C の走査で ES8311・BMI270・M5PM1 の 3 個を見つけ、BMI270 の chip ID 0x24 をアプリから読めた。電源は M5PM1 (電源の管理の小さいマイコン) が持つので保持のピンは無い。sdkconfig の defaults は AtomS3R のものをそのまま使い (写しは作っていない)、1 行も変えていない。**端末はボタンで入れた書き込み待ちの状態から抜けておらず、リセットのたびに書き込み待ちに戻る** (8.1)。今は RAM から bootloader を起動して動かしている

## 1. 結果の要約

- **ビルド**: `FMRB_HW_TARGET=STICKS3 rake clean_all && rake build:esp32` が通る。ファーム 0x449fb0 (4,497,328 B)、5M の区画の残り 0xb6050 (745,552 B、14%)。静的な D/IRAM 114,863 B (同じコミットの ATOMS3R は 114,499 B、差 +364 B)。
- **起動**: `Running on ESP32-S3 (M5Stack StickS3)`、シリアルの `Guru|abort` は 0 (約 30 分の連続運転を含む)。
- **画面**: LovyanGFX の初期化が通り、パネルが RDDID に答えた (`0xd98181`。何もつながっていなければ 0 か全部 1)。`/fb` で 240x135 の asterism_demo の絵が撮れた。実物の見た目はユーザの確認 (9 章)。
- **電源**: M5PM1 (0x6E) の読み値で S3 の 3.3 V (DCDC) が入っている・USB から給電・充電中・電池 3.69 V を確認。画面の電源 (M5PM1 の G2) は起動時に切れていたのをファームが入れた。抜いて動き続けるかはユーザの確認。
- **ボタン**: KEY1 = G11、KEY2 = G12 (どちらも押すと Low)。アプリから GPIO で読め、待機時は両方 1。押したときの変化はユーザの確認。
- **BMI270**: アプリから I2C で 0x68 の 0x00 を読んで 0x24。
- **WiFi・devctl・Asterism**: H2 と同じ設定 (`config/wifi.toml` → `/etc/wifi.toml`) で WiFi がつながり、devctl で put・launch・kill・`/fb` が使えた。`/home/asterism/config.toml` に手元の zenohd を書くと asterism_demo が `connected` になり、PC の `asterism` CLI と `tools/fmrb_zenoh.rb` から見え、呼べた (往復 中央値 141 ms)。
- **内蔵 RAM**: asterism_demo + WiFi + zenoh の定常で `IRAM free: 132,064 B` (H3 の AtomS3R は 135,280 B)。
- **回帰**: TAB5・NARYAv3・ATOMS3R のビルドが通り、静的な D/IRAM とファームの大きさは変更の前と後で 1 B も違わない (6 章)。`rake test` も通る。

## 2. ピンと周辺

出典の種類: **回** = 公式の回路図 (K150 Stick S3、V0.6)、**表** = 公式の製品の文書のピンの表、**ラ** = M5GFX / M5Unified の上流の StickS3 の定義、**実** = 実物で確かめた (方法は 2.2)。

### 2.1 表

| 機能 | GPIO・アドレス | 出典 | 実物 | 備考 |
|---|---|---|---|---|
| 画面 SPI MOSI | G39 | 回・ラ・表 | 実 (ID 読み・描画) | 3 線式 SPI。MISO は無い |
| 画面 SPI SCK | G40 | 回・ラ・表 | 実 | |
| 画面 CS | G41 | 回・ラ・表 | 実 | 10 kΩ で 3V3_L3B へ |
| 画面 DC (RS) | G45 | 回・ラ・表 | 実 | G45 は VDD_SPI の strap。flash 3.3 V で起動している |
| 画面 RST | G21 | 回・ラ・表 | 実 | |
| 画面の制御 IC | ST7789P3、135x240 | 表・ラ | 実 (RDDID が `0xd98181`) | ラの値: 240x320 の中の (52, 40)、色の反転あり |
| 背光 | G38 (PWM) | 回・ラ・表 | 未 (目で見る) | G38 → FET (2N7002) → 背光。High で点く。ラは LEDC 7 番・256 Hz・下の 16 段を切る |
| 画面の電源 | M5PM1 の G2 (L3B) | 回・ラ | 実 (レジスタ) | 画面・背光・ES8311 の電源 (3V3_L3B / L3B_AU)。起動時は切れていた (G2 の mode・out = 0) |
| システムの I2C SDA | G47 | 回・ラ・表 | 実 (走査) | 2.2 kΩ で 3V3_L2 へ |
| システムの I2C SCL | G48 | 回・ラ・表 | 実 (走査) | 同上 |
| M5PM1 (電源の管理、PY32L020) | I2C 0x6E | 回・ラ・表 | 実 (走査・読み書き、device ID 0x50) | 常に電源が入っている。電源ボタン・充電・5 V 出力・電池の電圧の ADC・緑の LED を持つ |
| BMI270 (6 軸) | I2C 0x68 | 回・表 | 実 (chip_id 0x24) | 電源は 3V3_L1 |
| BMI270 INT1 | M5PM1 の G4 | 回 | — | **S3 には来ていない**。M5PM1 が受けて S3 を起こす |
| ES8311 (音の codec) | I2C 0x18 | 回・表 | 実 (走査) | 電源が L3B_AU なので、画面の電源 (G2) を入れたときだけ I2C に出る (レベル変換を挟む) |
| I2S MCLK / BCLK / LRCK / DOUT / DIN | G18 / G17 / G15 / G14 / G16 | 回・ラ・表 | 未 | 今回は音を出さない (audio_none.c のまま) |
| スピーカのアンプ (AW8737) の ON | M5PM1 の G3 | 回・ラ | 未 | |
| M5PM1 の割り込み出力 (G1) | G13 | 回 | 未 | 表には G13 が「L3B」と並んで書かれていて紛らわしい。回路図では PYG1_IRQ → G13 |
| ボタン KEY1 | G11 | 回・ラ・表 | 実 (待機時 1) | 10 kΩ で 3V3_L2 へ、押すと Low。M5Unified の BtnA |
| ボタン KEY2 | G12 | 回・ラ・表 | 実 (待機時 1) | 同上。BtnB |
| 電源ボタン | GPIO ではない | 回 | — | M5PM1 の BTN_PU。1 回押しで電源を入れる・リセット、2 回押しで切る、長押しで書き込み待ち (表) |
| リセット | GPIO ではない | 回 | — | SOC_RESET は M5PM1 と S2 (基板の上のスイッチ) から |
| G0 (BOOT) | G0 | 回・表 | 実 (下の 8.1) | M5PM1 の BOOT_OUT (open drain) と Hat2 の BOOT_OUT につながる。**書き込み待ちに入れると M5PM1 が Low に保ち続ける** |
| 赤外線の送信 | G46 | 回・表 | 未 (目に見えない) | G46 → FET → LED。FET のゲートは 10 kΩ で GND (strap の値を保つ) |
| 赤外線の受信 | G42 | 回・表 | 未 | 受けるときはスピーカのアンプを切る (表) |
| Grove (HY2.0-4P) | 黄 G9 / 白 G10 | 回・ラ・表 | 未 | ラは外の I2C を SDA=G9 / SCL=G10 とする。プルアップなし |
| Hat2 (2.54 mm 16 ピン) | G0〜G8、G43、G44、電池、3V3_L2、5 V | 回・表 | 未 | 表の G4 に「Boot」とあるのは回路図の EXT_GPIO4 と BOOT_OUT が隣にあるため。回路図の Hat2 の BOOT_OUT は G0 |
| USB D- / D+ | G19 / G20 | 回 | 実 (USB-Serial/JTAG) | |
| Octal PSRAM | G33〜G37 | — | 実 (Octal 80 MHz で起動) | パッケージの中 |
| RTC | 無し | 回 | 実 (走査で 0x32 は無い) | 時刻はネットワークから (AtomS3R と同じ) |

資料と実物の食い違いは見つからなかった。表 (製品の文書) の書き方が紛らわしい所 (G13、Hat2 の G4) は回路図に合わせた。

### 2.2 実物での確かめ方

AtomS3R の H0 のような試しのファームは作らず、STICKS3 のファームを焼いて、試しのアプリ (scratchpad の `sticks3_probe.app.rb`、コミットしない。端末の `/app/test/` に置いてある) を devctl で起動した。I2C は devctl の hw_proxy を通る (画面の側が持つ I2C の窓口)。

| 確かめたこと | 方法 | 結果 |
|---|---|---|
| チップ | esptool の chip_id (書き込み待ちのまま) | ESP32-S3-PICO-1 (LGA56) rev v0.2、flash 8 MB、PSRAM 8 MB。Tab5 (ESP32-P4) ではない |
| システムの I2C | 0x08〜0x77 を 1 B 読んで走査 (100 kHz) | **0x18、0x68、0x6E の 3 個** |
| BMI270 | 0x68 の 0x00 を読む | 0x24 |
| M5PM1 | 0x00・0x04・0x06・0x11・0x12・0x22 を読む | id 0x50、電源 0x05、PWR_CFG 0x17、GPIO_OUT 0x04、GPIO_IN 0x14、電池 3,696 mV |
| ボタン | G11・G12 を 30 ms ごとに読む | 待機時はどちらも 1。押した記録は無い (誰も押していない) |
| 画面 | init 後に RDDID (0x04) を読む | `0xd98181` (パネルが答えた) |

## 3. 構成 `STICKS3` の作り方

方針は「ATOMS3R と同じ書き方で並べる」(指示書)。違うのはピン・画面・電源の初期化だけにした。

| 場所 | 変更 |
|---|---|
| `components/fmrb_common/include/fmrb_pin_assign.h` | STICKS3 の分岐 (2 章の表)。ボタンは `FMRB_PIN_BUTTON_ENTER` = G11 (設定のモードの判定にも使われる)、`FMRB_PIN_BUTTON_DOWN` = G12 (pin manager がシステムのピンとして持つ)。I2S のピンは Tab5 と同じ名前 (`FMRB_PIN_I2S_SCLK` など) |
| `components/fmrb_common/fmrb_hw_defines.cmake` | `FMRB_HW_STICKS3` + `FMRB_HW_FAMILY_MINI` + `FMRB_HAS_LOCAL_DISPLAY` + `FMRB_HAS_WIFI`。mini の端末の一覧 `FMRB_HW_MINI_TARGETS` (ATOMS3R STICKS3) を置き、ローカルの画面・画面の口の選択 (`board/<端末名の小文字>/`) をそれで決める |
| `main/CMakeLists.txt`、`components/fmrb_hal/CMakeLists.txt` | `FMRB_HW_TARGET STREQUAL "ATOMS3R"` の 3 箇所を `IN_LIST FMRB_HW_MINI_TARGETS` に (描画の一式・USB host なし・local link) |
| `main/drivers/display_local/board/sticks3/` | 画面の口の実装 (下の 4・5 章)。`BOARD_DISPLAY_OWNS_I2C 1` |
| `rakelib/build.rake` | STICKS3 の行。sdkconfig は `config/sdkconfig.defaults.atoms3r` をそのまま指す |
| `Rakefile` | 端末の既定 (エディタ mruby、Spinel の FFT なし、日本語のフォントなし、Asterism あり) を ATOMS3R と共有 (`MINI_BUILD_DEFAULTS`) |
| `config/system_conf_sticks3.toml` | ATOMS3R の写しから 240x135、`startup_app = "/app/test/asterism_demo.app.rb"` |
| `config/storage_exclude_sticks3.txt` | ATOMS3R と同じ中身 (区画も同じ) |
| `cmake/check_storage_inputs.cmake`、`tool/spinel/gen_const_rb.rb`、`lib/add/picoruby-fmrb-const/ports/esp32/const.c` | STICKS3 の行。`FmrbConst::BOARD = "sticks3"`、`HW_FAMILY = "mini"` |
| `main/kernel/fmrb_rtc.c` | RTC を読まない端末に STICKS3 |
| `main/kernel/fmrb_kernel.c` | 設定のモードの既定 (true) を `FMRB_HW_ATOMS3R` から `FMRB_HW_FAMILY_MINI` に |
| `main/boot/boot.c` | 起動の 1 行のログ |
| `.github/workflows/build.yml` | `build-esp32 (STICKS3)`。`.env` に STICKS3 の行が無いので、選択の段で行を足す |

- **sdkconfig**: 区画の表も含めて AtomS3R と同じでよいので、`config/sdkconfig.defaults.sticks3` の写しは作らず、ATOMS3R の defaults を指した (写しを作ると同じ中身が 2 つになり、片方だけ直す事故が起きる)。defaults の中のコメントは AtomS3R の名前のまま。区画の表も `partitions_atoms3r.csv` を共有する。違いが要るようになったら、そのとき写しを作る。
- 「ヘッドレス専用のコードはなるべく書かない」: 新しいのは画面の口の実装 (board/sticks3) とピンの分岐だけ。共通の部分は端末名の分岐を mini の一覧に置き換えただけ。

## 4. 画面

- `lgfx_sticks3.hpp`: LovyanGFX の `Panel_ST7789` を `fmrb_pin_assign.h` から直接設定 (M5GFX の自動判別は使わない、AtomS3R と同じ流儀)。3 線式 SPI (SPI3、書き 40 MHz)、135x240、(52, 40) ずらし、色の反転あり。値は M5GFX の StickS3 の定義から。
- **向きは横** (`setRotation(1)` で 240x135)。asterism_demo の小さい画面の表示は 39 桁 x 14 行で読める。縦にしたいなら system_conf の大きさと rotation (0 か 2) を変えるだけ。
- 背光は `Light_PWM` (G38、LEDC 7 番、256 Hz、offset 16。M5GFX と同じ値)。
- `/fb` (devctl) で撮った絵: 黒地に白の 6x8 の字で、`fmruby-XXXXXX` / `connected` / `nodes 1 +0 -0` / `peer -` / `said hello from pc` / 接続のログ。240x135 の全体に余白なく出ている。

## 5. 電源

- 回路図の電源の図: M5PM1 は電池か USB から常に電源が入っていて (L0)、レールを自分で入れ切りする。L1 (BMI270 など) は M5PM1 の起動で入り、**L2 (S3 の 3.3 V、DCDC の JW5712) は電源ボタンの 1 回押しか IMU の割り込みで入り、2 回押しか S3 からの I2C の命令で切れる**。L3B (画面・背光・codec) は M5PM1 の G2 で入る。USB を抜くと VBUS_L0 は電池からダイオード越しに続くので、L2 は入ったまま。
- よって **S3 の側に保持のピンは無い** (StickC Plus2 の HOLD のような出力は無い)。ファームがすることは M5GFX・M5Unified と同じく:
  1. M5PM1 の I2C の休止を切る (0x09 = 0) と、見張りの時計を切る (0x0A = 0)。どちらも M5PM1 が電池で動き続ける間は前のファーム (UiFlow など) の値が残るため。見張りが残っていると、餌をやらないファームは電源を切られうる。
  2. M5PM1 の G2 を GPIO・push-pull・出力・High にして画面の電源を入れる (切れていたときだけ 100 ms 待つ)。
- 起動のログに電源の状態を 1 回出す: `M5PM1 power: src=0x05 cfg=0x17 (dcdc 1 ldo 1 boost 0 chg 1) vbat=3694 mV vin=5014 mV charging=1`。cfg の dcdc 1 が「S3 のレールが入っている」= 保持の状態。
- M5PM1 の GPIO の機能のレジスタ (0x16) は、上流の M5Unified の新しい版 (M5PM1_Class) の「1 本 2 bit」で書いた。M5GFX の StickS3 の初期化は `bitOff(0x16, 1 << 2)` (1 本 1 bit の読み) で、2 bit の読みでは G1 (割り込みの出力) の機能の下の bit を消すことになる。今回の端末では 0x16 は最初から 0x00 で、どちらでも同じ結果になった。
- 電源ボタンの決まり (表と回路図): 1 回押しで電源を入れる (入っていればリセット)、2 回押しで切る、長押しで書き込み待ち (緑の LED が点滅)。ファームは電源を切る命令を実装していない (上流では 0x0C への書き込み)。

## 6. ボタン・BMI270

- ボタンは AtomS3R と同じ口: アプリの `GPIO.new(11, GPIO::IN)` と `read`。ピンはシステムのピンとして pin manager が持っているので `GPIO_init` の reset は BUSY で断られるが、入力の設定と読みは通る (AtomS3R の G41 と同じ振る舞い)。起動時に KEY1 を押していると設定のモード (BLE) に入る (`setup mode: not entered (WiFi settings present, button up)` のログ)。
- BMI270 は既存の口 (`I2C.new(unit: :ESP32_I2C1, sda_pin: FmrbHw::PIN_I2C1_SDA, ...)`) で 0x68 の chip ID 0x24 が読めた。gem (picoruby-bmi270) の取り込み・初期化は次の段。

## 7. WiFi・devctl・Asterism、内蔵 RAM

- WiFi: `/etc/wifi.toml` で接続、mDNS の名前 `fmruby-XXXXXX.local`。アプリが動き出してから IP の取得まで約 2.6 秒 (H3 の AtomS3R は 2.40 秒)。
- devctl: `/fs/put` `/fs/mkdir` `/app/list` `/app/kill` `/app/launch` と `/fb` (MCP の tab5_fs・tab5_app・tab5_screenshot に ip を渡す) が使えた。`/fb` は 240x135 の PNG (frame_size 240x135)。
- Asterism: 親の compose の zenohd を `docker/asterism/docker-compose.zenoh-lan.yml` を重ねて起動し、端末の `/home/asterism/config.toml` に `router = "tcp/<PC の LAN の IP>:7447"` を置いて asterism_demo を起動し直すと、108〜123 ms で `connected`。
  - `asterism node info fmruby-XXXXXX --router tcp/127.0.0.1:7447` → `profile: kind=family_mruby protocol=1 host=sticks3`、アプリ demo、オブジェクト apu・info・screen。
  - `tools/fmrb_zenoh.rb call fmruby-XXXXXX/demo/info status` → `{"board":"sticks3","iram_free":132020,...}` (144 ms)、`screen say "hello from pc"` → 13、端末の画面に `said hello from pc`。
  - `info echo` を 30 回: 最小 111 ms・中央値 141 ms・最大 152 ms (H3 の AtomS3R は 99 / 146 / 185 ms)。
  - asterism-console の図は見ていない (console を起動していない)。CLI で代えた。
- 内蔵 RAM:

| | StickS3 (今回) | AtomS3R (H3) |
|---|---|---|
| 静的な D/IRAM | 114,863 B | 114,499 B (同じコミットの ATOMS3R のビルド) |
| app_main 直後 (`M1|boot_start`) | 326,276 B | — |
| devctl・debugd まで (`M1|debugd`) | 133,772 B (最大の塊 92,160 B) | — |
| 定常 `IRAM free:` (asterism_demo + WiFi + zenoh) | **132,064 B** | 135,280 B |

  定常の差 3.2 KB のうち、同じコミットで比べられるのは静的な 364 B (背光の LEDC と口の実装) だけ。残りは H3 (2026-10-12) から今日の develop までの変化と区別できていない (AtomS3R が手元に無く、同じファームで測れない)。

## 8. 気づいたこと

### 8.1 書き込み待ちから抜けない (G0 を M5PM1 が Low に保つ)

- 焼いた直後のリセットも、シリアルを開いたときのリセットも、`rst:0x15 (USB_UART_CHIP_RESET),boot:0x23 (DOWNLOAD(USB/UART0))` で書き込み待ちに戻った。G0 は M5PM1 の BOOT_OUT につながっていて、**電源ボタンの長押しで書き込み待ちに入れると、M5PM1 が G0 を Low に保ち続ける**と見られる。S3 の側のリセット (RTS、esptool の hard reset) では抜けられない。
- ユーザの操作 (電源ボタンの 1 回押しなど) を待たずに先へ進むため、**書き込み待ちのまま RAM に bootloader を読み込んで起動した**: esptool の stub を走らせ、`build/bootloader/bootloader.bin` の 3 つの区間を mem_begin / mem_block で RAM に書いて mem_finish で飛ぶ。bootloader が flash からアプリを読んで普通に起動する。ROM のまま (stub なし) で同じことをすると bootloader の初期化で IllegalInstruction になった (ROM の受け取りの作業域が bootloader の IRAM と重なるためと見られる)。stub を通すと通る。
  - 同じシリアルの接続を開いたまま読む (閉じて開き直すとリセットがかかり、書き込み待ちに戻る)。手順は fmruby-core/tmp/sticks3_ramboot.py (gitignore の下、コミットしない) で、ビルドの container の中で動かす。MCP の serial_start は使えない (開くとリセット)。
- **ユーザに確かめてほしい**: 電源ボタンを 1 回押す (または 2 回押しで切ってから 1 回押しで入れる) と、普通に flash から起動するようになるか。なれば今回のファームは RAM 起動なしで動く。

### 8.2 その他

- MCP の `flash` は端末とポートを選べない (`.env` の TAB5 を見る) ので、`FMRB_HW_TARGET=STICKS3 FLASH_BAUD=460800 rake flash` で書いた (ポートは `.serial_port` の `/dev/ttyACM0`、焼く前に esptool で ESP32-S3-PICO-1 を確かめた)。全体で約 1 分。`.env` は触っていない。
- `rake clean_all` を何度も使ったので、build/ は今 STICKS3 の esp32 のビルド (作業の前は Linux の sim のビルドだった)。wasm の libmruby も消えている (ブラウザ版をビルドし直す前に `rake wasm:mruby`)。
- ATOMS3R のファームの残りも 14% (0xb7c30) になっていた (H3 では 22%)。H3 から develop が育った分。

## 9. ユーザに見てほしい点・操作してほしい点

1. **書き込み待ちから抜けるか** (8.1): 電源ボタン (側面) を 1 回押す。シリアルを見ている場合は `boot:0x8 (SPI_FAST_FLASH_BOOT)` 相当で起動すればよい。抜けなければ 2 回押しで切って 1 回押しで入れる。
2. **画面** (起動すると asterism_demo が出る): 色 (白い字・黒い地。反転していれば白地になる)、向き (横長。字が上下逆なら rotation を 3 に)、ずれ (左上の字が欠けていないか、右端・下端にごみの列が無いか)、背光 (点いているか、明るさ)。
3. **ボタン**: devctl で試しのアプリ `/app/test/sticks3_probe.app.rb` を起動し (tab5_app に ip を渡すか `curl -X POST "http://<ip>/app/launch?path=/app/test/sticks3_probe.app.rb"`)、KEY1 (前面の大きいボタン) と KEY2 (側面のボタン) を押す。画面とシリアルに `KEY1 pressed (1)` / `released` が出ればよい。どちらが前面か (資料と逆でないか) も見てほしい。終わったら asterism_demo を起動し直す (または電源の入れ直し)。
4. **電池**: 電源が入った状態で USB を抜き、画面が出たまま asterism_demo が網に残るか (PC の `asterism node list --router tcp/127.0.0.1:7447` に出続けるか) を見る。差し直してもリセットされないか。2 回押しで電源が切れ、1 回押しで入るか。

## 10. 残件

- 9 章の 1〜4 (ユーザの目と手)。
- 電源を切る命令・電池の電圧をアプリに見せる口 (M5PM1 は画面の側の I2C の窓口から読める。Asterism のオブジェクトにするなら H4 と一緒に)。
- 音 (ES8311 + AW8737)。今は audio_none.c で音なし。
- 赤外線 (G46 / G42)、Grove、Hat2 は試していない。
- picoruby-bmi270 の取り込み (次の段)。
- plan.md の M3 の「M5 の機種をまとめる口」(今回はしない、と指示書)。今回 mini の一覧 (`FMRB_HW_MINI_TARGETS`) を作ったので、まとめるときの足場になる。
- AtomS3R と同じファームで定常の内蔵 RAM を比べる (7 章の差 3.2 KB の内訳)。

## 11. 回帰

| 構成 | 前 (develop) の D/IRAM | 後 | ファームの大きさ |
|---|---|---|---|
| TAB5 | 129,698 B | 129,698 B | 6,660,800 B (前後で同じ) |
| NARYAv3 (Retro) | 126,035 B | 126,035 B | 4,496,816 B (同じ) |
| ATOMS3R | 114,499 B | 114,499 B | 4,490,192 B (同じ) |
| STICKS3 | — | 114,863 B | 4,497,328 B |

- 前は作業の変更を stash して同じ checkout でビルドした。`rake test` (ti・BASIC・MicroPython など) は通る。sim (Linux) は回していない (Linux の側で変わるのは CMake の一覧の書き方だけで、Linux の分岐は通らない)。

## 12. コミット (fmruby-core、develop、push していない)

- `18efad09 board: M5StickS3 as FMRB_HW_TARGET=STICKS3`
- `76f4f412 ci: build the StickS3 too (build-esp32 (STICKS3))`
- この報告と指示書はコミットしていない (親がする)。

## 13. 端末の状態

- StickS3 (`/dev/ttyACM0`): 18efad09 のファームを全体で焼いた (/home は空から。`/home/asterism/config.toml` に手元の zenohd、`/app/test/sticks3_probe.app.rb` を置いた)。今は RAM 起動で asterism_demo が動いていて、zenohd につながっている。シリアルは container `sticks3_serial` が開いたまま fmruby-core/tmp/sticks3_serial.log に書いている (止めると端末はリセットされ、書き込み待ちに戻る)。
- 親の compose の zenohd は LAN に開いたまま (`docker-compose.zenoh-lan.yml` を重ねた起動)。
- Tab5・VPS・`~/asterism_keys` には触っていない。
