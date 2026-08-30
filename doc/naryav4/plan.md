# NARYA v4 (ESP32-P4 + HDMI 出力) 対応計画

> 状態: 進行中 | 更新: 2026-08-30 | P0-P2 完了 (HDMI **1280x720 実 80MHz** で起動時から安定表示。起動時の歪みは「要求 40MHz が実は 39.70MHz だった」で解決、report/p2.md)。次は P3

## 目的

- 次期 Modern 専用基板 **NARYA v4** (設計中) は ESP32-P4-Nano を参照設計とし、
  映像を MIPI-DSI → LT8912B ブリッジ → HDMI で出力する。
- 基板の完成を待たずに、市販部材 (**Waveshare ESP32-P4-Nano + Olimex
  MIPI-HDMI 変換基板 + 15pin FPC ケーブル**) でソフトウェアの bring-up を
  先行して完了させる。
- bring-up で確定した事実 (ピン割当・電源順序・DSI タイミング・音声経路) を
  基板設計へ反映する。

方式選定は reference/hdmi_video_output_study.md で完了している
(案A = LT8912B ブリッジ)。本計画はその実行編。

## 前提

### ハードウェア

| 項目 | ESP32-P4-Nano (プロトタイプ) | Tab5 (現行 Modern) |
|---|---|---|
| SoC / メモリ | P4 + PSRAM 32MB (in-package) + flash 16MB | 同等 |
| 無線 | ESP32-C6-MINI を SDIO 接続 | 同構成 (ピンは異なる) |
| 映像 | 2-lane MIPI-DSI (15pin FPC) → LT8912B → HDMI 1280x720 横 (実 80MHz / 64.6Hz。P2 で決着) | DSI 直結パネル 720x1280 縦 |
| タッチ | なし (HDMI モニタ) | GT911/ST7123 |
| 入力 | USB-A (OTG HS) キーボード/マウス | 同 + 内蔵キーボード + タッチ |
| 音声 | ES8311 (I2C 0x18) + NS4150B アンプ (EN=GPIO53)。スピーカ端子とオンボードマイク (ES8311 MIC1) を使う | ES8388 + ES7210 |
| その他 | TF (SDIO), 100M Ethernet (不使用), RTC 電池端子 | SD, RX8130, BMI270, PI4IO x2 |

- Olimex 変換基板は LT8912B 搭載、DSI 1-4 lane 入力、1080p60 まで出力可、
  15pin FPC は ESP32-P4-DevKit とピン互換 (P4-Nano との互換は要実測)。
- LT8912B は Espressif 公式ドライバがある (esp-bsp の esp_lcd_lt8912b。
  公式 EV ボード BSP に 1280x720@60 実績)。さらに手元の
  managed_components の m5gfx (v0.2.28) に **Panel_LT8912B が既に入っている**。

### ソフトウェアの現状 (調査済み、2026-08-29)

- **NARYAv4 ターゲットは実体化済み** (P1)。専用の
  config/sdkconfig.defaults.naryav4 (chip rev v3.1 / C6 の SDIO ピン) と
  config/system_conf_naryav4.toml (display_mode = naryav4_hdmi)、
  fmrb_pin_assign.h の専用ブロックを持ち、プレースホルダ警告は無い。
- ボード分岐マクロは二軸: 世代 = `FMRB_HW_MODERN` / `FMRB_HW_FAMILY_MODERN`、
  ボード = `FMRB_HW_TAB5` / `FMRB_HW_NARYAV4`。
- ピンは components/fmrb_common/include/fmrb_pin_assign.h に集約
  (P1 で TAB5 / NARYAv4 の 2 ブロックに分割済み)。ただし
  display_p4_task.cpp と audio_p4_hw.c が Tab5 のピン・PI4IO・I2C を
  ローカル定義しており、ここにも分岐が要る (P2/P3)。
- 描画経路: 内部 426x240 RGB565 を PPA Blend 合成 → PPA SRM 3x 拡大 +
  90 度回転 → DSI FB (720x1280)。HDMI 化での差分は
  **回転なし・FB を 1280x720 化・パネル初期化を LT8912B に差し替え**の 3 点。
  426x240 → 1278x720 はちょうど 3 倍で、既存 PPA パスが流用できる。
- 無線: esp_hosted 1.4.0 (SDIO)。Tab5 の SDIO ピンと C6 電源投入
  (PI4IO #2、display 初期化に依存) は sdkconfig.defaults.p4 と
  boot.c の modern_radio_init_task に埋まっており、P4-Nano では両方変わる。
- USB HID (usb_task) は S3/P4 共通実装でボード依存なし。P4-Nano の
  USB-A (HS OTG PHY) でそのまま動く見込み。

## 方針

- **NARYAv4 ターゲットの実体 = 当面 P4-Nano + Olimex 構成**とする。
  最終基板は P4-Nano を参照設計とするためピン差分は小さい見込みで、
  差分が出たら pin_assign.h の NARYAV4 ブロックだけを差し替える。
- 表示は **1280x720 横向きを実 80MHz で駆動** (P2 で決着)。60Hz ちょうどは
  諦める (実リフレッシュ 64.6Hz)。**要求した周波数が実際に出ているかを
  ブリッジのライン計数で確かめること** — 800x600 の 40MHz は出ていなかった
  (report/p2.md)。内部解像度 426x240 は変えず、PPA の 3x で 1278x720 に
  拡大して載せる (黒縁は左右 1px)。
- LGFX (Bus_DSI) 経路を維持する。パネルは **espressif/esp_lcd_lt8912b を
  managed 依存で取り込み、LovyanGFX 側は Panel_FrameBufferBase 派生の薄い
  クラスで FB に被せる** (P2 で決着)。m5gfx 同梱の Panel_LT8912B は
  800x600 のタイミングを持たないため不採用 (report/p2.md)。
- 音声は P4-Nano オンボードで完結させる: スピーカ出力もマイク入力も
  ES8311 1 チップ (Tab5 の ES8388 + ES7210 の 2 チップ構成から集約)。
  esp_codec_dev は ES8311 に対応しており、audio_p4_hw.c の枠組みは変えない。
- Tab5 固有装置 (タッチ、内蔵キーボード、PI4IO、IMU、RX8130) はボード分岐で
  無効化する。**TAB5 ビルドの退行ゼロ**を常に保つ (検証構成は標準 +
  TAB5 / NARYAv4 の 2 実機ターゲット)。
- RTC チップはプロトタイプに無い。時刻は WiFi 時刻同期 (timesync サービス)
  で代替し、最終基板で RTC を搭載する際に改めて対応する。
- 無線スレーブ: プロトタイプは P4-Nano 搭載の C6 で bring-up するが、
  最終基板は技適表示の都合で**無印 ESP32 (WROOM-32 系) を第一候補**にする
  (詳細は未確定事項の節)。esp-hosted がスレーブ差を吸収するため、
  bring-up の成果はそのまま持ち越せる。
- graphics-audio リポジトリは触らない (Modern は core 単体構成)。

## スコープ (フェーズ分割)

### P0: 部材と単体疎通 (完了 2026-08-29、report/p0.md)

1. 部材: ESP32-P4-Nano / Olimex MIPI-HDMI / FPC-15 ケーブル
   (調達・接続済み。通電確認から始める)。
2. P4-Nano の DSI コネクタが Olimex (P4-DevKit 互換) とピン互換か実物確認。
3. esp-bsp の esp_lcd_lt8912b サンプルを **IDF 5.5.4** でビルドし、
   HDMI モニタに 1280x720@60 が出ることを確認する
   (公式 FAQ に「HDMI は IDF 5.4 以前推奨」の記述があるため、5.5 での
   動作可否をここで潰す。だめなら理由を特定して回避策を決めてから P2 へ)。
4. esptool で chip revision を実測する (sdkconfig の rev 範囲決定に使う。
   Tab5 は v1.0 で `SELECTS_REV_LESS_V3` 運用中。P4-Nano が v3.x なら
   NARYAv4 専用の rev 設定が要る)。

### P1: ビルドターゲットの実体化 (完了 2026-08-30、report/p1.md)

1. `config/sdkconfig.defaults.naryav4` を p4 から分岐して新設:
   esp_hosted の SDIO ピン (P4-Nano の C6 配線に合わせる)、chip revision
   範囲、PSRAM/flash は Tab5 と同値のはず (P0 で確認した値)。
2. `config/system_conf_naryav4.toml` 新設 (display_mode は新値
   `naryav4_hdmi` を追加、解像度 426x240 は据え置き)。
3. rakelib/build.rake の hw_config と cmake/check_storage_inputs.cmake の
   **両方**を新ファイルに向ける (後者は不一致でビルドを止める設計)。
4. fmrb_pin_assign.h に `#elif defined(FMRB_HW_NARYAV4)` ブロックを新設
   (P4-Nano の実ピン。SD は SDMMC、USB は HS PHY 専用パッド)。
5. fmrb_hw_defines.cmake のプレースホルダ警告を外す。
6. パーティションは partitions_p4.csv を共用 (flash 16MB 同一)。

### P2: 表示 bring-up (完了 2026-08-30、report/p2.md)

1. display_p4_task.cpp のローカル Tab5 定数 (ピン / PI4IO / tab5_power_on /
   パネルプローブ) をボード分岐へ出す。NARYAv4 は電源シーケンス不要
   (PI4IO なし) で、LT8912B の I2C 設定だけになる想定。
2. LGFX_Naryav4 クラス新設 (Bus_DSI + Panel_LT8912B、lane 2、
   800x600 横、タッチなし)。DSI レーン速度 1Gbps・DPI 40MHz
   (PLL_F240M/6)・VESA 800x600 タイミングは P0 の実績値。APLL は使わない
   (DDS がロックしない。根拠は report/p0.md)。
3. FB の RGB888 化 (LT8912B は RGB888 入力のみ、1280x720 で 2.76MB PSRAM)
   と PPA SRM の出力形式変更、**回転なしの 3x** で 426x240 → 1278x720。
4. 受け: デスクトップが HDMI モニタに表示され、GFX STATS が Tab5 同等。

### P3: 無線・入力・周辺の差し替え

1. C6: SDIO ピンは P1 の sdkconfig で対応済み。boot.c の
   modern_radio_init_task から PI4IO 依存 (display 待ち) を外す分岐。
   C6 出荷時 FW が esp_hosted 1.4.0 と噛み合うか確認し、必要なら
   slave FW の書込み手順を確立する。
2. USB キーボード/マウスの動作確認 (コードは共通、確認のみの想定)。
3. touch_task / tab5_keyboard を NARYAv4 で外す (CMake ソース分岐)。
   IMU / RX8130 も同様にガード。
4. SD (SDMMC) の実ピンで動作確認。
5. audio: audio_p4_hw.c の Tab5 ローカル定義 (I2S ピン・ES8388/ES7210・
   PI4IO 経由のアンプ EN) をボード分岐へ出し、NARYAv4 は ES8311 1 チップ
   (esp_codec_dev の ES8311 ドライバ) + NS4150B EN (GPIO53、回路図実測) 構成にする。
   I2S の実ピンは回路図から起こす。APU 出力 (47160Hz) とマイク入力
   (mic_spectrum 系) の両方を通す。音が鳴ること・マイクにレベルが
   入ることまでが受け (音質の官能評価はユーザ)。
6. remote desktop の動作確認。これが通ると tab5_* 系の遠隔検証ツールが
   NARYAv4 でもそのまま使える。

### P4: 総合検証と基板設計へのフィードバック

1. エディタ起動 + 打鍵、既存アプリ数本、crash マーカー 0、
   周期ダンプの IRAM/スタック確認 (Tab5 の常設計装をそのまま使う)。
2. モニタ数機種で EDID / ホットプラグ / 相性確認。
3. 実測: fps、PSRAM 消費 (RGB888 FB 分)、PPA 変換コスト。
4. 無印 ESP32 スレーブの事前疎通: WROOM-32 系 devkit に esp-hosted
   スレーブ FW を焼き、SDIO 接続で WiFi + BLE が通ることを基板発注前に
   確認する (ジャンパ配線では速度を落として疎通のみ確認。
   プルアップと GPIO12 の扱いをここで実地確認する)。
5. **基板設計への反映事項を report にまとめる**: 確定ピン一覧、
   無線スレーブ (無印 ESP32) の SDIO 固定ピン・プルアップ・eFuse 要件、
   スレーブの電源/リセットの要否、音声 (ES8311 + アンプのローカル構成で
   確定、実測したゲイン/レート設定)、RTC の搭載方針、5V/EDID/ESD、
   LT8912B の先行部品確保 (hdmi_video_output_study.md の残項目を消し込む)。

基板 (実 NARYA v4) が完成したら、ピン定義の差し替えと再検証を
別フェーズ (P5) として起こす。

## 受け入れ条件

- `FMRB_HW_TARGET=NARYAv4` の `rake build:esp32` が警告なしで通り、
  P4-Nano + Olimex + HDMI モニタでデスクトップが 800x600@60 で安定表示
  される。
- USB キーボード/マウスで操作でき、エディタを起動して打鍵できる。
- WiFi (C6) が接続し、remote desktop 経由の画面取得・入力注入が動く。
- APU の音がオンボードスピーカ出力から鳴り、オンボードマイクの入力が
  読める (音質の評価はユーザ)。
- ブートで crash マーカー 0、既存アプリが動く。
- TAB5 ビルドが退行しない (同一ソースで両ターゲットがビルド・動作する)。

## スコープ外

- 最終基板そのものの回路・アートワーク設計 (本計画は検証結果の提供まで)。
- HDMI 音声出力の実装 (音声はローカルの ES8311 経路で確定)。
- Ethernet (非対応で確定。ドライバも組み込まない)。
- RTC チップ対応 (プロトタイプに無い。最終基板で搭載予定、その時に別途)。
- graphics-audio 側の変更 (なし)。

## 未確定事項


- ~~HDMI の解像度の最終形~~ → **決着 (2026-08-29)**: 表示モードは
  **800x600@60 (PLL_F240M/6 = 正確な 40MHz)** を採用。fmruby の内部
  426x240 は 1.5x = 639x360 + 黒縁で載せる (P2 で実装)。
  720p60 は「74.25MHz が APLL でしか作れず、LT8912B の DDS が APLL
  (分数 SDM) のジッタにロックできない」ため IDF 5.5.4 では不成立と確定
  (切り分け実験と数値根拠は report/p0.md)。再挑戦条件も同 report に記載。
- C6 出荷時ファームウェアと esp_hosted 1.4.0 の適合 (P3-1)。
- ES8311 の 47160Hz 動作の確認とマイクゲインの適値 (P3-5)。I2S の実ピンは
  確定済み (P0 で回路図から起こし、P1 で fmrb_pin_assign.h に入れた)。
- 最終基板でのピン差分と RTC 型番 (設計中のため。pin_assign.h は
  差し替え前提で書く)。
- 最終基板の無線スレーブは**無印 ESP32 (WROOM-32 系) を第一候補**とする
  (2026-08-29 決定)。理由: C6-MINI-1 / C6-WROOM-1 とも型式認証
  (TELEC 2024-09 / 工事設計認証 007-AM0211) はあるが、実物のシールドに
  技適の表示刻印がないことを確認済み。電磁的表示 (画面表示) は認証取扱
  業者側の表示方法に踏み込むため自作機では採らない。WROOM-32 系は
  物理刻印の実績があるが、調達時に正規流通で現物確認する
  (刻印つき偽造モジュールの流通例あり)。
  ソフト面: esp-hosted は無印 ESP32 の SDIO スレーブに対応 (HCI over
  SDIO も可)。ホスト側は sdkconfig のスレーブターゲット変更とスレーブ FW
  の焼き直しだけで、esp_wifi_remote から上は無変更。機能差は
  WiFi6→WiFi4 / BLE5→BLE4.2 で現行用途に影響なし。
  基板設計への制約: 無印の SDIO スレーブは固定ピン (CLK=14 / CMD=15 /
  D0=2 / D1=4 / D2=12 / D3=13)、全ラインに外付けプルアップ 51kΩ 必須、
  D2=GPIO12 がフラッシュ電圧のストラップと衝突するため eFuse (VDD_SDIO)
  措置が要る。プロトタイプ (P4-Nano) は搭載済みの C6 のまま bring-up し、
  基板発注前に無印スレーブでの esp-hosted 疎通 (WiFi + BLE) を devkit で
  確認するのが安全 (P4 フェーズの項目に含める)。
