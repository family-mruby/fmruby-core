# コア間通信 - SPI通信の抽象化レイヤー

## 概要

Family mRubyシステムでは、Core (ESP32-S3, Master) と Graphics-Audio (ESP32-WROVER, Slave) の2つのマイコン間でSPI通信を行います。この通信は以下の4層アーキテクチャで抽象化されています：

```
+---------------------------------------------------+
|  Application Layer                                |
|  - mrubyスクリプト (gfx.c → send_gfx_command)    |
|  - host_task (コマンドバッファリング + 実行)       |
+---------------------------------------------------+
                    ↓↑
+---------------------------------------------------+
|  Transport Layer (fmrb_transport)            |
|  - MessagePack シリアライズ [type, seq, sub_cmd,  |
|    payload]                                       |
|  - 同期/非同期送信                                 |
|  - ACK/NACK マッチング (seq番号)                  |
|  - リトライ管理                                    |
|  - フラグメンテーション (大きいペイロード)          |
+---------------------------------------------------+
                    ↓↑
+---------------------------------------------------+
|  HAL Link Layer (fmrb_hal_link)                   |
|  - CRC32 チェックサム付加/検証                     |
|  - COBS エンコーディング/デコーディング            |
|  - GPIO ハンドシェイク (ACK通知)                   |
|  - ACK受信キュー (ack_recv_queue)                 |
|  - 指数バックオフ付きACKポーリング                 |
+---------------------------------------------------+
                    ↓↑
+---------------------------------------------------+
|  SPI Driver Layer (fmrb_hal_spi / spi_slave)      |
|  Master: ESP-IDF SPI Master (10MHz, 全二重)        |
|  Slave: ESP-IDF SPI Slave (ダブルバッファDMA)      |
|  256バイト固定長フレーム                            |
+---------------------------------------------------+
```

## Master側 (fmruby-core)

### Transport Layer

**ファイル**: `fmruby-core/components/fmrb_transport/fmrb_transport.c`

**主要API**:
```c
// 非同期送信（ACKはHAL層で待機、pendingリストで追跡）
fmrb_err_t fmrb_transport_send(uint8_t link_type,
                                    uint8_t sub_cmd,
                                    const uint8_t *payload,
                                    uint32_t payload_len);

// 同期送信（レスポンスを待機、セマフォでブロック）
fmrb_err_t fmrb_transport_send_sync(uint8_t link_type,
                                         uint8_t sub_cmd,
                                         const uint8_t *payload,
                                         uint32_t payload_len,
                                         uint8_t *response_payload,
                                         uint32_t *response_len,
                                         uint32_t timeout_ms);

// 受信処理（host_taskのメインループから定期呼び出し）
fmrb_err_t fmrb_transport_process(void);
```

**MessagePackフォーマット**:
```
msgpack array [4]:
  [0] type     (uint8) - FMRB_LINK_TYPE_CONTROL=1, GRAPHICS=2, AUDIO=4
  [1] seq      (uint8) - シーケンス番号 (0-255)
  [2] sub_cmd  (uint8) - コマンド種別
  [3] payload  (bin)   - ペイロード（バイナリ）
```

**送信フロー (`fmrb_transport_send`)**:
1. `fmrb_transport_process()` を呼び出し、未処理ACKを排出
2. シーケンス番号を割り当て、msgpackでシリアライズ
3. `fmrb_hal_link_send()` を呼び出し（HAL層でACK完了まで待機）
4. retransmit有効時はpendingリストに追加

**受信フロー (`fmrb_transport_process`)**:
1. `fmrb_hal_link_receive()` でACKキューからデータ取得
2. msgpackデシリアライズ → `handle_received_message()`
3. ACK/NACKの場合: sync_requestsまたはpending_messagesとseq番号をマッチング
4. sync_requestにマッチ → セマフォをgiveしてブロック中のスレッドを起床

### HAL Link Layer (Master)

**ファイル**: `fmruby-core/components/fmrb_hal/platform/esp32/fmrb_hal_link_esp32.c`

**送信 (`fmrb_hal_link_send`)** の詳細フロー:

```c
// 1. ペイロード + CRC32 を連結
buffer = [payload | CRC32(payload)]

// 2. COBSエンコード
encoded = cobs_encode(buffer)

// 3. SPIフレーム構築 (256バイト固定)
tx_frame = [encoded | 0x00(終端) | padding(0x00...)]

// 4. 全二重SPI転送 (送信 + 同時受信)
fmrb_hal_spi_transfer(tx_frame, rx_frame, 256)

// 5. 受信データからACK抽出を試みる
process_received_ack(rx_frame)  // → ack_recv_queueにキュー

// 6. ACK未受信ならGPIOハンドシェイク付きポーリング
while (!ack_received && !timeout) {
    if (GPIO == LOW) {           // Slaveが「ACK準備完了」を通知
        poll_for_ack()           // 空フレーム送信でACK受信
    }
    delay(poll_interval_ms)      // 指数バックオフ: 1→2→4→8ms
}
```

**ACK受信キュー (`ack_recv_queue`)**:

HAL層とTransport層のブリッジ。HAL層でACKをデコードした後、ack_recv_queueにキューイング。
Transport層の `fmrb_transport_process()` がデキューし、seq番号でマッチングする。

- サイズ: 32エントリ
- エントリ: `{data*, size}` （呼び出し側がfree責任）

**受信 (`fmrb_hal_link_receive`)** :

```c
// ack_recv_queueからデキューのみ行う。
// Slaveは自発的にデータを送信しない（ACKのみ）ため、
// SPI読み取りは不要。不要なSPIトランザクションは
// Slaveのトランザクションキューを圧迫するため行わない。
```

## Slave側 (fmruby-graphics-audio)

### タスク構成

**ファイル**: `fmruby-graphics-audio/main/main_esp32.cpp`

| タスク | 優先度 | Core | 役割 |
|--------|--------|------|------|
| audio_task | 7 (最高) | 0 | 60Hz APUエミュレーション（ハードリアルタイム） |
| comm_task | 6 | 0 | SPI通信処理 + メッセージディスパッチ |
| graphics_task | 5 | 0 | ディスプレイ描画 (~60FPS) |

### SPI Slave実装

**ファイル**: `fmruby-graphics-audio/main/communication/comm_spi_slave.c`

**ダブルバッファDMA構成**:
```
tx_buffers[0] ←→ spi_slave_transaction[0]
tx_buffers[1] ←→ spi_slave_transaction[1]
rx_buffers[0]     (各256バイト、MALLOC_CAP_DMA)
rx_buffers[1]
pending_ack_buf   (ACKステージング用)
```

初期化時に2つのトランザクションを事前キューイングし、常にSlaveがSPIリクエストに対応可能な状態を維持。

**受信処理 (`spi_process`)** :

```c
// 1. セマフォ待機（ISRからの通知）
xSemaphoreTake(trans_ready_sem, 100ms)

// 2. 完了済みトランザクションを全て排出
//    (バイナリセマフォは複数ISRを1つに圧縮するため、
//     whileループで全結果を取得)
while (spi_slave_get_trans_result(&completed, 0) == ESP_OK) {
    process_single_transaction(completed)
}
```

**トランザクション処理 (`process_single_transaction`)** :

```c
// 1. ACK送信済みバッファの確認 → GPIO HIGH (アイドル)
if (ack_buf_idx == buf_idx) {
    gpio_set_level(HANDSHAKE, 1)  // ACK送信完了
    ack_buf_idx = -1
}

// 2. 受信データからCOBSフレーム検出 + デコード → メッセージキュー
if (frame_found) {
    process_cobs_frame(rx_buf, frame_end)  // → message_queueにenqueue
}

// 3. pending_ack_bufがあればTXバッファにコピー
if (pending_ack_len > 0) {
    memcpy(tx_buffers[buf_idx], pending_ack_buf, pending_ack_len)
    ack_buf_idx = buf_idx
}

// 4. トランザクション再キュー
queue_next_transaction()
```

**ACK送信 (`spi_send_ack`)** :

```c
// 1. ACKをmsgpack + CRC32 + COBSでpending_ack_bufにエンコード
fmrb_link_encode_ack(type, seq, response_data, response_len,
                     pending_ack_buf, SPI_FRAME_SIZE, &encoded_len)

// 2. GPIO LOWでMasterに通知
gpio_set_level(FMRB_PIN_SPI_HANDSHAKE, 0)  // ACK準備完了

// → 次のSPIトランザクション時にTXバッファにコピーされて送信
// → 送信完了後にGPIO HIGHに戻る
```

### comm_task メインループ

**ファイル**: `fmruby-graphics-audio/main/tasks/comm_task.c`

```c
while (task_running) {
    // 1. SPI低レベル処理
    int frames_received = comm->process()

    // 2. デコード済みメッセージを処理
    while (comm->receive_message(&type, &seq, &sub_cmd, &payload, &len) > 0) {
        message_handler_process(type, seq, sub_cmd, payload, len)
    }

    // 3. アイドル時のみ1msスリープ（ビジー時は即ループ）
    if (frames_received <= 0) {
        vTaskDelay(pdMS_TO_TICKS(1))
    }
}
```

## GPIOハンドシェイク

Master GPIO9 (入力) ←→ Slave GPIO23 (出力)

**信号**: **LOW = ACK準備完了**, **HIGH = アイドル** (外部プルアップ)

```
Master (ESP32-S3)                GPIO    Slave (ESP32-WROVER)
─────────────────────────────────────────────────────────────
1. コマンド送信 (SPI全二重)      HIGH    受信 → メッセージキュー

2. ack_received=false            HIGH    comm_task: デキュー → 処理
   ポーリングループ開始                   → spi_send_ack()

3.                               LOW ←── pending_ack_bufにエンコード
                                         gpio_set_level(0)

4. GPIO読み取り → LOW            LOW     ポーリング待ち
   空フレーム送信 (SPI全二重)

5.                               LOW     ISR → process_single_transaction()
                                         pending_ack_buf → tx_buffers[n]

6. 次のSPIトランザクションで      LOW     ACKデータが送信される
   ACK受信 → デコード成功
   ack_received = true

7.                               HIGH ←── ACK送信完了確認
                                          gpio_set_level(1)
```

**Masterのポーリング戦略**:
- GPIOがHIGHなら即リターン（SPIトランザクション不要）
- GPIOがLOWなら空フレーム送信してACK受信
- 指数バックオフ: 1ms → 2ms → 4ms → 8ms（max）
- タイムアウト: 1000ms

## フレーム構造

```
送信側で構築:
  [msgpack_data | CRC32(msgpack_data)]
      ↓ COBSエンコード
  [COBS_encoded_data | 0x00(終端)]
      ↓ 256バイトにパディング
  [COBS_data | 0x00 | padding(0x00...)]  ← SPIフレーム (256 bytes)

受信側で処理:
  SPIフレーム → 先頭の0x00スキップ → 0x00終端検出
      ↓ COBSデコード
  [msgpack_data | CRC32]
      ↓ CRC32検証
  msgpack_data → [type, seq, sub_cmd, payload]
```

## Master側の完全な送信データフロー

```
[Ruby App]
  gfx.fill_rect(x, y, w, h, color)
      ↓ send_gfx_command()
      ↓ fmrb_msg_send(PROC_ID_HOST, &msg, 500ms)

[host_task (メインループ)]
  fmrb_msg_receive() → host_task_process_gfx_command()
      ↓ 非PRESENTコマンド: コマンドバッファに追加 (即座)
      ↓ PRESENTコマンド: fmrb_gfx_command_buffer_execute()
          ↓ 各コマンドを順次実行

[GFX Layer - fmrb_gfx.c]
  fmrb_gfx_fill_rect() → send_graphics_command()
      ↓ fmrb_transport_send(GRAPHICS, FILL_RECT, payload)

[Transport Layer]
  1. fmrb_transport_process()  ← 未処理ACKを排出
  2. msgpack: [type=2, seq=N, sub_cmd=0x05, payload]
  3. send_raw_message() → fmrb_hal_link_send(1000ms)

[HAL Link Layer]
  4. payload + CRC32 → COBSエンコード → 256バイトフレーム
  5. SPI全二重転送 (tx_frame → Slave, rx_frame ← Slave)
  6. rx_frameからACK抽出 → ack_recv_queueにキュー
  7. ACK未受信 → GPIOチェック + ポーリング (1→2→4→8ms)
  8. ACK受信完了 → FMRB_OK返却

[Transport Layer]
  9. retransmit有効ならpendingリストに追加
```

## エラーハンドリング

| エラー | 検出層 | 対処 |
|--------|--------|------|
| CRC32不一致 | HAL層 | フレーム破棄、ACKタイムアウトでリトライ |
| COBSデコード失敗 | HAL層 | フレーム破棄 |
| ACKタイムアウト | HAL層 (1000ms) | `FMRB_ERR_TIMEOUT` 返却 |
| メッセージキュー満杯 | Transport層 | 送信エラー (`FMRB_ERR_TIMEOUT`) |
| ACKキュー満杯 | HAL層 | ACKデータ破棄 (ログ警告) |

## プラットフォーム比較

| 項目 | ESP32 (SPI) | Linux (Socket) |
|------|-------------|----------------|
| 通信方式 | SPI Master/Slave | Unix Domain Socket |
| 全二重性 | ハードウェア全二重 | ソフトウェア全二重 |
| ACK待機 | HAL層 (GPIO + ポーリング) | Transport層 |
| フレームサイズ | 256バイト固定 | 可変長 |
| クロック | 10MHz | N/A |
| エンコーディング | COBS + CRC32 | COBS + CRC32 |
| Slave側バッファリング | ダブルバッファDMA + PSRAM MQ | ソケットバッファ |

## パフォーマンス設定

| パラメータ | 値 | ファイル |
|-----------|-----|---------|
| SPI_FRAME_SIZE | 256 bytes | fmrb_hal_link_esp32.c, comm_spi_slave.c |
| SPI_FREQUENCY | 10 MHz | fmrb_hal_link_esp32.c |
| ACK_RECV_QUEUE_SIZE | 32 | fmrb_hal_link_esp32.c |
| HOST_QUEUE_SIZE | 64 | host_task.c |
| GFX_CMD_BUFFER_SIZE | 128 | host_task.c |
| ACKポーリング | 1→2→4→8ms (指数バックオフ) | fmrb_hal_link_esp32.c |
| ACKタイムアウト | 1000ms | fmrb_transport.c |
| send_gfx_command timeout | 500ms | gfx.c |
| Slave NUM_BUFFERS | 2 (ダブルバッファ) | comm_spi_slave.c |
| Slave MSG_QUEUE | PSRAM (525KB) | comm_spi_slave.c |
