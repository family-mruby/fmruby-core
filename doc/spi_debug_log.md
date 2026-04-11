# SPI通信デバッグ記録 (2026-04-11)

## ハードウェア構成

- Master: ESP32-S3-N16R8 (CORE)
  - MOSI=GPIO11, MISO=GPIO13, SCLK=GPIO12, CS=GPIO10, READY=GPIO9
- Slave: ESP32-WROVER (Graphics-Audio)
  - MOSI=GPIO21, MISO=GPIO18, CLK=GPIO19, CS=GPIO22, HS=GPIO23
- 全ピン: GPIO matrix経由 (IO_MUXではない)
- フレームサイズ: 256バイト、両側DMA有効

## テスト結果一覧

| # | 周波数 | Mode | input_delay_ns | WROVER受信 (MOSI方向) | CORE受信 (MISO方向) | 結果 |
|---|--------|------|----------------|----------------------|---------------------|------|
| 1 | 10MHz | 0 | 0 | ~30トランザクション後DMAが4バイトしか書かない | - | NG |
| 2 | 5MHz | 0 | 68 | CRCエラーなし | magic=0x4A(1ビットシフト)、ファイル転送1回成功 | 部分的 |
| 3 | 5MHz | 0 | 0 | CRCエラーなし | magic=0x4A、#2より悪化 | NG |
| 4 | 5MHz | 1 | 68 | CRCエラー(DMA 4バイト) | データ化け(0x9C) | NG |
| 5 | 5MHz | 3 | 68 | CRCエラー(DMA 4バイト) | データ化け(0x9C) | NG |
| 6 | 5MHz | 0 | 68 | CRCエラーなし | magic=0x4A、ファイル転送タイムアウト | 不安定 |
| 7 | 2MHz | 0 | 68 | 問題なし | magic=0x4A継続 | NG |
| 8 | 1MHz | 0 | 68 | 問題なし | magic=0x4A継続、ホストタイムアウト | NG |
| 9 | 5MHz | 0 | 68 | CRCエラーなし(NUM_BUFFERS=1) | magic=0x4A主体、0xA5が1回 | NG |
| 10 | 5MHz | 0 | 40 | trans#4525で909bits途中切断、data=23/4973 | magic=0x4A、ファイル転送98%(4140/4224) | 改善 |
| 11 | 5MHz | 0 | 50 | CRCエラー1回(途中切断2回) | magic=0x4A、ファイル転送98%(4140/4224) | #10同等 |
| 12 | 5MHz | 0 | 25 | CRCエラー0回! trans#4271で1168bits途中切断 | magic=0x4A、0xA5が1回。ファイル転送98% | #10同等 |

## 調査結果

### 問題1: MOSI方向 (Master -> Slave) のDMA問題

**症状:** 10MHzで約30トランザクション後、SPI slave DMAが4バイトしか書き込まなくなる。

**原因:** ESP32 SPI slave mode 0+DMAでは、GPIO matrix経由のMISO出力が半クロック早くシフトされる。10MHzではGPIO matrixの遅延(68.75ns)がクロック半周期(50ns)を超え、タイミング違反が発生。

**対策:** 5MHzに周波数を下げることで解決。CRCエラーなしで安定動作。

**注意:** mode 1/3に変更するとMOSI受信が壊れる(5MHzでも)。mode 0のみ動作する。

### 問題2: MISO方向 (Slave -> Master) の0x4A問題

**症状:** COREがslaveの応答を読むと magic=0x4A (0xA5の1ビット右シフト) が出る。

**判明した事実:**
- 0x4Aはslave初期化完了前やACK準備前のゴミ応答
- 実データ応答(VERSION ACK, Canvas作成ACK, ファイル転送DATA ACK)は全て正常に読める
- RX valid #1-#22まで連続成功を確認

**根本原因:** READY=HIGHが「受信可能」を示すだけで「ACK応答準備完了」を示さない。
ダブルバッファのため、slaveのTXバッファ更新前にmasterがpollすると古い応答が返る。

### 問題3: ACK待ちpollの大量発生とトランザクション途中切断

**症状:** 数千トランザクション後にtrans_len < 2048bits のフレームが発生。

**原因:** READY制御の問題で空振りpollが大量発生(5秒timeout中に2500回)。
累積されたSPIトランザクションがDMAに負荷をかけ途中切断を引き起こす。

**hw_proxyの影響:** 旧FWではgpio_get_level()を直接呼んでいたが、新FWではhw_proxy経由に
変更されていた。SPI通信のホットパスでhw_proxy経由のGPIO読み取りは遅すぎる。
直接gpio_get_level()に戻すことでpoll効率は改善。

### 問題4: on_create後のWDTリセット

**症状:** Canvas #1, #2作成成功、on_create到達後にWDTリセット。

**原因:** draw_background/draw_foregroundの描画コマンド(clear, present)が
fmrb_transport_send(同期ACK待ち)を使用。ACK待ちpollでtimeout -> WDTリセット。

## 適用済みの修正

- boot.c hw_proxy初期化順序修正 (commit cf63d42)
- spi_frame_tサイズの_Static_assert追加
- ack_queue_item_tの型不一致修正 (uint8_t -> uint16_t)
- .gitignoreのlog*ルール修正

## 旧FWとの比較

- CORE b919aae + WROVER 009a257: 10MHzで動作確認済み
- 旧struct: ヘッダー5バイト, data[249], data_len: uint8_t
- 新struct: ヘッダー6バイト, data[248], data_len: uint16_t
- structのバイトレイアウト変更がDMA問題を顕在化させた

## 適用済みのSPI設定 (現在の状態)

- 周波数: 5MHz (10MHzからの変更)
- SPI mode: 0
- input_delay_ns: 25
- NUM_BUFFERS: 2
- gpio_get_level直接呼び出し (hw_proxy経由からの変更)
- ISRで常にREADY=LOW + task側でREADY=HIGH復帰
- ACK_REQUIRED時のtimeout付きACKキュー待ち (100ms)
- 途中切断フレーム(trans_len < 2048)の無視

## 結論

SPIのMOSI方向(master->slave)は5MHzで安定動作。MISO方向の0x4Aは
READY制御の問題で、実データ応答は正常に読める。しかしACK待ちpollの
空振りが大量発生し、描画コマンドの同期送信でtimeout -> WDTリセット。

SPIのREADY制御とACKプロトコルの根本的な再設計が必要。ただし、
GPIO matrix経由のSPI DMA制約(mode 0のみ、5MHz以下)も本質的な制限。

## 今後の方針

**UARTへの切り替えを検討する。** SPI特有のDMA/GPIO matrix/READY制御の問題を回避できる。
Linux simのソケット通信と概念的に類似しており、既存の通信抽象化層を活用可能。
