# Tab5 内部 I2C バスの制約と設計ルール

2026-07-06 時点の知見。Tab5 (ESP32-P4, Modern) の内部 I2C バスは複数の
ドライバ方式が混在しており、誤った経路でアクセスすると通信が構造的に
失敗する。ここに実測で確定した制約と、守るべきルールをまとめる。

## バス構成

- 物理バス: SDA=GPIO31, SCL=GPIO32 (1本のバスを全デバイスが共有)
- fmruby-core では I2C コントローラ **port 1** に割り当て
  (M5Tab5-UserDemo の BSP は同じピンを port 0 に割り当てている点に注意)
- 接続デバイス:
  - PI4IO #1 (addr 0x43): P1=スピーカーアンプ有効(SPK_EN),
    P4=LCD_RST, P5=TP_RST, P6=CAM_RST, P7=ヘッドホン挿抜検出(入力, High=挿入)
  - PI4IO #2 (addr 0x44): 電源レール (bit0=C6電源, P3=USB5V_EN 等)
  - GT911 タッチ (addr 0x14; リセット時に INT=High で 0x14 を選択)
  - ES8388 オーディオコーデック
  - (RTC 等その他のデバイスも同一バス)

## 実測で確定した制約 (2026-07-06)

**LovyanGFX (M5GFX) はこのバスをレジスタ直叩きで使う。**
lgfx は `i2c_new_master_bus()` でバスを「確保」するが、実際の通信は
I2C ペリフェラルのレジスタを直接操作し、トランザクション毎に
レジスタ状態を保存/復元する方式
(managed_components/m5stack__m5gfx/src/lgfx/v1/platforms/esp32/common.cpp)。

この結果:

1. **タッチポーリング (GT911, 33ms周期) が動き出すと、同じコントローラ
   への i2c_master ドライバ経由のトランザクションはほぼ毎回失敗する。**
   まれな衝突ではない。ヘッドホン検出を i2c_master でポーリングした
   実験では、`i2c.master: clear bus failed / reset hardware failed /
   I2C transaction failed` がポーリング周期(166ms)ごとに連発し、
   バスリセットすら効かない状態になった
2. hw_proxy_i2c の `I2C bus id(1) has already been acquired` (error 259)
   も同根 (lgfx がバスを確保済みのため i2c_new_master_bus が失敗する)

## 設計ルール

1. **ランタイムに繰り返しアクセスするデバイス (PI4IO, GT911) は、
   lgfx の I2C ヘルパ (`lgfx::i2c::readRegister8` / `bitOn` / `bitOff`
   等) を使い、かつタッチタスクの文脈から呼ぶこと。**
   GT911 の読み取りと同一タスクに直列化することで競合を構造的に防ぐ。
   実装例: `display_p4_poll_headphone()` (display_p4_task.cpp) を
   touch_task のループから 165ms 毎に呼んでいる
2. **i2c_master ドライバでのアクセスは「初期化ウィンドウ」内のみ許容。**
   初期化ウィンドウ = display task が LGFX init を終えてから
   `g_lcd_ready` を立てるまでの区間 (タッチタスクは g_lcd_ready を
   待ってからポーリングを開始するため、バスが無競合)。
   実装例: ES8388 コーデック初期化 (audio_p4_hw_init) はこの窓で実行
3. tab5_power_on() (display_p4_task.cpp) は独自に i2c_master バスを
   作成して PI4IO を初期化し、**バスを削除してから** LGFX init に進む。
   この一時バスは他と重ならないので問題ない

## 既知の課題

- **音量変更 (SET_VOLUME) は esp_codec_dev = i2c_master 経由**のため、
  タッチポーリング開始後は失敗する可能性が高い (未対策)。
  対策案: ES8388 の音量レジスタ書き込みを lgfx::i2c 経由に置き換えて
  タッチタスク文脈で実行する、または音量変更時のみタッチポーリングを
  一時停止する
- hw_proxy_i2c は Modern では I2C1 を使えない (error 259)。mruby アプリ
  からの I2C アクセスを Modern で提供するなら lgfx 経由の仲介が必要

## 参考: ヘッドホン挿抜によるスピーカーミュートの実装

- 検出: PI4IO#1 入力レジスタ IN_STA(0x0F) bit7 (High=挿入)
- ミュート: PI4IO#1 OUT_SET(0x05) bit1 (SPK_EN) を lgfx::i2c::bitOn/bitOff
  で RMW (他ビット: LCD/TP/CAM リセット, EXT5V を保持)
- タッチタスクから 165ms 毎にポーリング、2回連続一致のデバウンス
- 起動時はコーデック初期化直後 (初期化ウィンドウ内) に初期状態を適用
- ES8388 はヘッドホン出力を駆動し続けるため、SPK_EN のみで排他になる
