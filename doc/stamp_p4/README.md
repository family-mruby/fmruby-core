# Stamp-P4 ヘッドレス機 (切符サイズの Modern)

> 状態: 構想 | 更新: 2026-08-31 | M5Stamp-P4 + Stamp-AddOn C6 を殻に入れたヘッドレス Family mruby。まず殻 (case_design.md)、ファーム分岐は後続

M5Stack の Stamp-P4 (ESP32-P4、16MB flash / 32MB PSRAM) に C6 無線モジュールを
積層し、画面を持たない Family mruby (Modern) として使う構想。位置づけと
使い方の候補は doc/ruby_asterism/node_variants.md 3 章、遠隔画面を
iPhone から使うための AP モードは doc/softap_remote/plan.md。

このテーマの文書:

- case_design.md: 生基板を触らずに使うための殻 (3D プリント) の設計方針と
  モデル作成の指示。**別セッションでモデルを書く前提で、測るものと
  決めごとをまとめてある**。
- (後続) ファームのターゲット分岐: doc/naryav4/plan.md の P1 と同じ型で
  `sdkconfig.defaults.stampp4` + ボード分岐を足す。表示は
  FMRB_DISPLAY_BACKEND (PPA のまま。パネル無し)、音は audio_backend_t を
  空に、C6 は esp_hosted (SDIO)。着手時に plan.md を起こす。

## 実物の仕様 (2026-08-31 時点で確認できたもの)

- 本体 29.8 x 22.0 x 4.3mm、2.7g。ESP32-P4NRW32 (16MB flash、32MB PSRAM =
  Tab5 と同じ)。
- **USB-C を基板上に持つ** (5V 給電 + 書き込み。USB-Serial-JTAG なので
  書き込みにボタンは要らない)。入力は DC 5V、6V 超の保護あり。
  **電池の充電回路は無い**。
- 端子: 44 本の GPIO (G0-G39, G41, G49, G50, G52) をスタンプ穴に出す。
  同じ基板が 1.27mm / 2.00mm ピッチの SMT と 2.54mm ピッチの DIP
  (ピンヘッダを自分で付ける) の両方に対応。**製品は 1 種類 (SKU S013)
  で、DIP 版という別製品は無い**。MIPI DSI (2 lane)、USB HOST の D+/D-、
  CHIP_EN もスタンプ穴。RMII (Ethernet、PHY 無し) は 1.27mm 側。
- BTB コネクタ 2 つ: SDIO (0.4mm ピッチ 20 pin) に **Stamp-AddOn C6
  (27.0 x 18.0 x 4.0mm、ESP32-C6-MINI-1-N4 = 基板アンテナ) を積層**、
  もう 1 つは MIPI CSI (カメラ)。
- 消費電流の公称値は待機 5V@30.76mA (WiFi と描画中の値ではない。要実測)。
- **基板上にボタン・LED・TF スロットは見当たらない** (製品資料に記載無し。
  実物で確認)。
- 純正のベース・ケースは無い (Stamp S3 用の BreakOut は寸法が違うので
  流用しない)。有志の 3D モデルも無い。

## 用途と道具立て

ヘッドレスなので、**殻 + USB-C だけで成立する** (キャリア基板は要らない)。
給電はモバイルバッテリか USB 充電器、設定の書き込みは USB シリアル、
操作は遠隔画面。キャリア基板 (電池回路・Ethernet PHY・TF・UART ヘッダ・
Grove) は用途が絞れてから node_variants.md 3.5 の表に従って起こす。
