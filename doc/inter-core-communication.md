# コア間通信 - SPI通信の抽象化レイヤー

## 概要

Family mRubyシステムでは、Core (ESP32-S3) と Graphics-Audio (ESP32-WROVER) の2つのマイコン間でSPI通信を行います。この通信は以下の3層アーキテクチャで抽象化されています：

```
+---------------------------------------------+
|  Application Layer (User Code)              |
|  - mrubyスクリプト実行                       |
|  - コマンド送受信                            |
+---------------------------------------------+
                    ↓↑
+---------------------------------------------+
|  Transport Layer (fmrb_link_transport)      |
|  - MessagePack シリアライズ                 |
|  - ACK/NACK ハンドリング                     |
|  - リトライ管理                              |
|  - タイムアウト処理                          |
+---------------------------------------------+
                    ↓↑
+---------------------------------------------+
|  HAL Link Layer (fmrb_hal_link)             |
|  - フレーム構造 (ヘッダー + ペイロード)      |
|  - COBS エンコーディング                     |
|  - CRC32 チェックサム                        |
|  - プラットフォーム抽象化                    |
+---------------------------------------------+
                    ↓↑
+---------------------------------------------+
|  SPI Driver Layer (fmrb_hal_spi)            |
|  - ESP32 SPI Master/Slave                   |
|  - 10MHz クロック                            |
|  - 256 バイト固定長フレーム                  |
|  - 全二重転送                                |
+---------------------------------------------+
```

## 各レイヤーの責務

### 1. Transport Layer (fmrb_link_transport)

**ファイル**: `fmruby-core/components/fmrb_link/fmrb_link_transport.c`

**責務**:
- MessagePack形式でのメッセージシリアライズ/デシリアライズ
- ACK/NACKの送受信管理
- 送信失敗時のリトライ処理（最大3回）
- タイムアウト管理（デフォルト1000ms）
- シーケンス番号管理

**主要API**:
```c
fmrb_err_t fmrb_link_transport_send(fmrb_link_channel_t channel,
                                     uint8_t msg_type,
                                     const void *payload,
                                     size_t payload_size,
                                     uint32_t timeout_ms);

fmrb_err_t fmrb_link_transport_receive(fmrb_link_channel_t channel,
                                        uint8_t *msg_type,
                                        void **payload,
                                        size_t *payload_size,
                                        uint32_t timeout_ms);
```

**データフロー（送信時）**:
1. MessagePackでシリアライズ
2. `fmrb_hal_link_send()` を呼び出し
3. ACK待機（HAL層でポーリング実施）
4. ACK受信確認
5. 失敗時はリトライ（最大3回）

**データフロー（受信時）**:
1. `fmrb_hal_link_receive()` でフレーム受信
2. MessagePackデシリアライズ
3. メッセージタイプ判定（DATA/ACK/NACK）
4. ACKの場合は内部処理、DATAの場合はアプリへ返却

### 2. HAL Link Layer (fmrb_hal_link)

**ファイル**:
- ESP32実装: `fmruby-core/components/fmrb_hal/platform/esp32/fmrb_hal_link_esp32.c`
- Linux実装: `fmruby-core/components/fmrb_hal/platform/posix/fmrb_hal_link_posix.c`

**責務**:
- フレームヘッダーの構築/パース
- COBS (Consistent Overhead Byte Stuffing) エンコード/デコード
- CRC32チェックサム計算/検証
- プラットフォーム固有の通信処理
- **ESP32版のみ**: ACK待機とポーリング

#### フレーム構造

```
+--------------------------------------------------+
| Frame Header (14 bytes)                          |
+--------------------------------------------------+
| Magic (4 bytes): 0x464D5242 ("FMRB")             |
| Version (1 byte): 0x01                           |
| Message Type (1 byte): DATA/ACK/NACK             |
| Sequence Number (2 bytes)                        |
| Payload Length (4 bytes)                         |
| Header Checksum (2 bytes): CRC16 of header       |
+--------------------------------------------------+
| Payload (variable length, MessagePack data)      |
+--------------------------------------------------+
| CRC32 (4 bytes): checksum of header + payload    |
+--------------------------------------------------+
        ↓ COBS Encode
+--------------------------------------------------+
| COBS Encoded Data (no 0x00 bytes inside)         |
| + Frame Delimiter (0x00)                         |
+--------------------------------------------------+
```

#### ESP32実装の特徴

**全二重SPI通信によるACK受信**:

ESP32版では、SPI Master（Core側）が全二重転送機能を使って、コマンド送信と同時にACKを受信できます。

```c
// fmrb_hal_link_send() の実装 (ESP32版)

// 1. フレーム構築（ヘッダー + ペイロード）
// 2. CRC32計算
// 3. COBSエンコード

// 4. 全二重SPI転送（TX + RX同時）
uint8_t tx_frame[SPI_FRAME_SIZE];  // 送信データ
uint8_t rx_frame[SPI_FRAME_SIZE];  // 受信バッファ
fmrb_hal_spi_transfer(spi_handle, tx_frame, rx_frame, SPI_FRAME_SIZE, timeout_ms);

// 5. 受信データからACK抽出（即座に受信できた場合）
process_received_ack(channel, rx_frame, SPI_FRAME_SIZE);

// 6. ACKが即座に受信できなかった場合、ポーリング
if (!ch->ack_received && timeout_ms > 0) {
    while (!ch->ack_received) {
        // タイムアウトチェック
        if (fmrb_hal_time_is_timeout(start_time, timeout_ms * 1000)) {
            return FMRB_ERR_TIMEOUT;
        }

        // 空フレーム送信してACKポーリング
        poll_for_ack(channel, poll_interval_ms);

        // 5ms待機
        fmrb_hal_time_delay_ms(5);
    }
}
```

**ACK処理の詳細**:

```c
static void process_received_ack(fmrb_link_channel_t channel,
                                  const uint8_t *rx_frame,
                                  size_t frame_size) {
    // 1. 先頭のnullバイトをスキップ
    while (start_pos < frame_size && rx_frame[start_pos] == 0x00) {
        start_pos++;
    }

    // 2. COBSフレーム終端 (0x00) を検索
    while (end_pos < frame_size && rx_frame[end_pos] != 0x00) {
        end_pos++;
    }

    // 3. COBSデコード
    ssize_t decoded_len = fmrb_link_cobs_decode(rx_frame + start_pos,
                                                  end_pos - start_pos,
                                                  decoded);

    // 4. CRC32検証
    uint32_t crc_calculated = fmrb_link_crc32_update(0, decoded, payload_len_rx);
    uint32_t crc_received;
    memcpy(&crc_received, decoded + payload_len_rx, sizeof(uint32_t));

    if (crc_calculated != crc_received) {
        ESP_LOGE(TAG, "CRC32 mismatch");
        return;
    }

    // 5. ACKフラグセット + コールバック呼び出し
    ch->ack_received = true;

    if (ch->callback) {
        // ヒープにコピー（スタック変数のライフタイム問題回避）
        uint8_t *decoded_copy = (uint8_t*)fmrb_sys_malloc(payload_len_rx);
        memcpy(decoded_copy, decoded, payload_len_rx);

        fmrb_link_message_t ack_msg = {
            .data = decoded_copy,
            .size = payload_len_rx
        };
        ch->callback(channel, &ack_msg, ch->user_data);
    }
}
```

**ポーリング処理**:

```c
static fmrb_err_t poll_for_ack(fmrb_link_channel_t channel,
                                uint32_t timeout_ms) {
    // 空フレーム（全て0x00）を送信
    uint8_t tx_frame[SPI_FRAME_SIZE] = {0};
    uint8_t rx_frame[SPI_FRAME_SIZE] = {0};

    // 全二重転送でSlaveからのACKを受信
    fmrb_err_t ret = fmrb_hal_spi_transfer(spi_handle, tx_frame, rx_frame,
                                            SPI_FRAME_SIZE, timeout_ms);

    if (ret == FMRB_OK) {
        // 受信データからACK抽出
        process_received_ack(channel, rx_frame, SPI_FRAME_SIZE);
    }

    return ret;
}
```

#### Linux実装の特徴

Linux版（POSIX/Socket通信）では、ESP32版とは異なり、以下の特徴があります：

```c
// fmrb_hal_link_send() の実装 (Linux版)

// NOTE: Linux/Socket version does NOT wait for ACK in this function
// ACKs are received separately via fmrb_hal_link_receive()
// This differs from ESP32/SPI which uses full-duplex and can receive ACK immediately

// 1. フレーム構築
// 2. CRC32計算
// 3. COBSエンコード
// 4. Unix Socketで送信（send()）
// 5. ACK待機はせずに即座にリターン
```

**違いの理由**:
- Unix Socketは全二重だが、送信と受信が非同期
- ACKは別のスレッドの `fmrb_hal_link_receive()` で受信
- Transport層のACK待機ロジックで対応

### 3. SPI Driver Layer (fmrb_hal_spi)

**ファイル**: `fmruby-core/components/fmrb_hal/platform/esp32/fmrb_hal_spi_esp32.c`

**責務**:
- ESP-IDF SPI Master/Slave APIのラッパー
- 固定長フレーム転送（256バイト）
- クロック設定（10MHz）
- GPIO設定

**主要API**:
```c
// 全二重転送（Master側で使用）
fmrb_err_t fmrb_hal_spi_transfer(fmrb_hal_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t len,
                                  uint32_t timeout_ms);

// 送信のみ（旧API、現在は未使用）
fmrb_err_t fmrb_hal_spi_transmit(fmrb_hal_spi_handle_t handle,
                                  const uint8_t *data,
                                  size_t len,
                                  uint32_t timeout_ms);

// 受信のみ（Slave側で使用）
fmrb_err_t fmrb_hal_spi_receive(fmrb_hal_spi_handle_t handle,
                                 uint8_t *data,
                                 size_t len,
                                 uint32_t timeout_ms);
```

**SPI設定（ESP32 Master側）**:
```c
spi_bus_config_t bus_cfg = {
    .mosi_io_num = config->mosi_pin,
    .miso_io_num = config->miso_pin,
    .sclk_io_num = config->sclk_pin,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 256,  // 固定長フレーム
};

spi_device_interface_config_t dev_cfg = {
    .clock_speed_hz = 10 * 1000 * 1000,  // 10 MHz
    .mode = 0,  // CPOL=0, CPHA=0
    .spics_io_num = config->cs_pin,
    .queue_size = 7,
};
```

## 完全な送信データフロー

アプリケーションからGraphics-Audioへコマンドを送信する場合の完全なデータフロー：

```
[Application Layer]
  ↓ fmrb_link_transport_send(channel, MSG_TYPE_DRAW_PIXEL, {x, y, color}, ...)

[Transport Layer - fmrb_link_transport.c]
  ↓ 1. MessagePack シリアライズ
  ↓    payload = msgpack({x: 100, y: 200, color: 0xFF0000})
  ↓
  ↓ 2. fmrb_hal_link_send(channel, frame_data, ...)

[HAL Link Layer - fmrb_hal_link_esp32.c]
  ↓ 3. フレームヘッダー構築
  ↓    header = {magic: "FMRB", version: 1, type: DATA, seq: 42, len: 20, ...}
  ↓
  ↓ 4. CRC32計算
  ↓    crc32 = calc_crc32(header + payload)
  ↓
  ↓ 5. COBSエンコード
  ↓    encoded = cobs_encode(header + payload + crc32) + 0x00
  ↓
  ↓ 6. 256バイトフレームにパディング
  ↓    tx_frame = encoded + padding(0x00...)
  ↓
  ↓ 7. fmrb_hal_spi_transfer(spi, tx_frame, rx_frame, 256, ...)

[SPI Driver Layer - fmrb_hal_spi_esp32.c]
  ↓ 8. ESP-IDF SPI Master送信
  ↓    spi_device_transmit(spi_handle, &transaction)
  ↓
  ↓ 9. 同時にSlaveからACK受信（全二重）
  ↓    rx_frame ← ACKデータ（準備できていれば）

[HAL Link Layer - fmrb_hal_link_esp32.c]
  ↓ 10. 受信データからACK抽出
  ↓     process_received_ack(channel, rx_frame, 256)
  ↓       → COBSデコード → CRC32検証 → ACKフラグセット
  ↓
  ↓ 11. ACK未受信の場合、ポーリング（5msごと）
  ↓     while (!ack_received && !timeout) {
  ↓         poll_for_ack(channel, 5)  // 空フレーム送信
  ↓         delay(5ms)
  ↓     }
  ↓
  ↓ 12. ACK受信完了、またはタイムアウト

[Transport Layer - fmrb_link_transport.c]
  ↓ 13. ACK確認
  ↓     if (ack_ok) return SUCCESS;
  ↓     else retry (最大3回)

[Application Layer]
  ← 14. 送信完了
```

## Graphics-Audio側（Slave）のACK送信

**ファイル**: `fmruby-graphics-audio/main/comm_spi_slave.c`

```c
static void spi_slave_task(void *arg) {
    while (task_running) {
        // 1. Masterからのコマンド受信
        fmrb_hal_spi_receive(spi_handle, rx_buffer, SPI_FRAME_SIZE, timeout);

        // 2. COBS デコード + CRC32 検証
        // 3. コマンド処理
        // 4. ACK準備

        fmrb_link_message_t ack_msg = {
            .data = ack_data,
            .size = ack_size
        };

        // 5. ACK送信（次のMasterクロックで送信される）
        fmrb_hal_link_send(FMRB_LINK_CHANNEL_SPI, &ack_msg, timeout);
    }
}
```

## タイミングチャート

```
Master (Core)                Slave (Graphics-Audio)
     |                              |
     |------ CMD Frame ------------>|
     |                              | (コマンド処理中...)
     |<----- ACK Frame (即座) ------|  ← 処理が早ければ同一トランザクションで返る
     |                              |
     | OK, 完了                     |


または遅延ACKの場合:

Master (Core)                Slave (Graphics-Audio)
     |                              |
     |------ CMD Frame ------------>|
     |                              | (コマンド処理中...)
     |<----- (empty/padding) -------|  ← ACK未準備
     |                              |
     | (ACK未受信、ポーリング開始)   |
     |                              |
     |------ Empty Frame ---------->|  ← 5ms後にポーリング
     |<----- ACK Frame -------------|  ← ACK準備完了
     |                              |
     | OK, 完了                     |
```

## エラーハンドリング

### CRC32エラー
- HAL層で検出、フレーム破棄
- Transport層でタイムアウト → リトライ

### ACKタイムアウト
- HAL層でポーリングタイムアウト（ESP32版）
- Transport層で再送（最大3回）
- 3回失敗で `FMRB_ERR_TIMEOUT` 返却

### COBS デコードエラー
- HAL層で検出、フレーム破棄
- Transport層でタイムアウト → リトライ

## プラットフォーム比較

| 項目 | ESP32 (SPI) | Linux (Socket) |
|------|-------------|----------------|
| 通信方式 | SPI Master/Slave | Unix Domain Socket |
| 全二重性 | ハードウェア全二重 | ソフトウェア全二重 |
| ACK待機 | HAL層で実装（即座/ポーリング） | Transport層で実装 |
| フレームサイズ | 256バイト固定 | 可変長 |
| クロック | 10MHz | N/A |
| エンコーディング | COBS + CRC32 | COBS + CRC32 |

## パフォーマンス考慮事項

### ESP32版の最適化
- **全二重SPI活用**: 送信と同時にACK受信、レイテンシ削減
- **ポーリング間隔**: 5ms（バランス重視）
  - 短すぎる: CPU負荷増大
  - 長すぎる: ACK受信遅延
- **固定長フレーム**: DMA転送最適化

### メモリ管理
- **ヒープ割り当て**: コールバックデータは必ずヒープ
  - スタック変数のライフタイム問題回避
  - 呼び出し側で `fmrb_sys_free()` 必須
- **静的バッファ**: COBS デコード用（4096バイト）

## まとめ

このSPI通信抽象化により、以下が実現されています：

1. **プラットフォーム独立性**: ESP32とLinuxで同一のTransport API
2. **信頼性**: CRC32検証、ACK/NACKメカニズム、自動リトライ
3. **効率性**: ESP32では全二重SPIによる低レイテンシACK受信
4. **拡張性**: 各層が独立、新しいプラットフォーム追加が容易
5. **デバッグ性**: 各層で詳細なログ出力、COBS/CRC32による明確なフレーム境界

今後の改善可能性：
- Slave側の処理時間計測とポーリング間隔の動的調整
- DMA転送の活用によるさらなる高速化
- 複数チャネルの並列通信サポート
