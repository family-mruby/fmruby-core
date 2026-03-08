# ESP32 SPI 通信方針（GPIO 1本 + COBS ペイロード + フレームヘッダ）

## 結論

GPIO が 1 本しかないなら、役割は次で固定する。

- GPIO 1本 = `READY`（送ってよいかの状態線）
- ACK や処理結果 = SPI MISO で **次回トランザクション** に返す
- 既存の COBS + msgpack エンコーディングは保持し、SPI フレームヘッダでラップする

SPI slave は、マスターがクロックを出したときにすでに送信バッファが決まっている必要がある。
ESP-IDF の slave API は、キューしたトランザクションのバッファ所有権をドライバに渡す前提なので、**事前に受信用 / 返答用スロットを複数本キューする**設計が必要。

---

## 実装方針

### 1. プロトコル方針

1. Master は `READY=HIGH` のときだけ送信する
2. Slave は常に `spi_slave_queue_trans()` を 2 本以上積んで受け口を確保する
3. Slave は受信後、アプリ処理結果を **次回返送用 TX バッファ** に反映する
4. Master は次回送信時に、前回分の `ack_seq + status` をフレームヘッダから読む
5. COBS ペイロードが必要な場合（応答データ付き ACK 等）は `data[]` フィールドに格納
6. GPIO 割り込みは **Master側の待機解除** にだけ使う
7. ACK を GPIO パルスで表そうとしない

### 2. ステータス設計

```c
typedef enum {
    STS_BOOT    = 0x00,  // 初期状態（起動直後）
    STS_RX_OK   = 0x10,  // 受信完了（アプリ未処理）
    STS_APP_OK  = 0x12,  // アプリ処理成功
    STS_APP_ERR = 0x13,  // アプリ処理失敗
    STS_CRC_ERR = 0xE1,  // フレーム CRC 異常
} spi_status_t;
```

### 3. フレーム構造（送受信共通）

```c
#define SPI_FRAME_MAGIC  0xA5
#define SPI_FRAME_SIZE   256
#define SPI_HEADER_SIZE  5    // magic + seq + ack_seq + status + data_len
#define SPI_CRC_SIZE     2    // CRC16 at end
#define SPI_MAX_DATA     (SPI_FRAME_SIZE - SPI_HEADER_SIZE - SPI_CRC_SIZE)  // 249 bytes

typedef struct __attribute__((packed)) {
    uint8_t magic;          // 0xA5: フレーム有効性チェック
    uint8_t seq;            // Master: コマンド seq / Slave: 0
    uint8_t ack_seq;        // Slave: 応答対象の master seq / Master: 0
    uint8_t status;         // Slave: STS_* / Master: 0
    uint8_t data_len;       // COBS ペイロード長 (0 = データなし。複数メッセージ時は 0x00 区切り含む合計長)
    uint8_t data[249];      // COBS エンコード済みメッセージ (msgpack)
    uint16_t crc16;         // フレーム全体の CRC16 (先頭254バイトに対して)
} spi_frame_t;              // 256 bytes
```

Master → Slave のフレーム:
- `seq`: コマンドのシーケンス番号（`seq >= 1`。`seq=0` はデータなしポーリング専用）
- `ack_seq`: 0（Master は ACK しない）
- `status`: 0
- `data[]`: COBS エンコード済み msgpack `[type, seq, sub_cmd, payload]`
- 注: `spi_frame_t.seq` は msgpack 内の `seq` と同一値を設定する（フレームレベルでの ACK マッチング用に冗長化）

Slave → Master のフレーム:
- `seq`: 0
- `ack_seq`: 応答対象の Master seq（`spi_frame_t.seq` に対応）
- `status`: STS_* コード
- `data[]`: COBS エンコード済み応答データ（必要な場合）。不要なら `data_len=0`

### 4. レイヤー構造

```
アプリケーション
    ↓
msgpack [type, seq, sub_cmd, payload]
    ↓
COBS エンコード (0x00 ターミネータで区切り)
    ↓
spi_frame_t.data[] に格納 (data_len = COBS 長)
    ↓
フレームヘッダ (magic, seq, ack_seq, status) + CRC16 付与
    ↓
SPI 転送 (256 bytes, full-duplex)
```

---

## 役割分担

### Master 側

- READY GPIO を入力（立ち上がり割り込み `GPIO_INTR_POSEDGE`）
- READY=HIGH の間だけ送信する
- SPI 送信と同時に rx_buffer を受け、フレームヘッダから `ack_seq + status` を読む
- `data_len > 0` なら COBS ペイロードをデコードして応答データを取得
- 重要コマンドだけ `STS_APP_OK` まで待つ（ダミーフレームでポーリング）
- DMA バッファは `heap_caps_malloc(SPI_FRAME_SIZE, MALLOC_CAP_DMA)` で確保

### Slave 側

- READY GPIO を出力
- `spi_slave_queue_trans()` を常時 2 本以上維持
- `post_setup_cb` (ISR): 何もしない
- `post_trans_cb` (ISR): pending--, pending==0 なら READY=LOW, セマフォ通知
- 本処理はタスク側で行い、結果を **次回返送用 TX バッファ** に書く
- re-queue 成功 → pending++, pending>0 なら READY=HIGH

### 注意点

- コールバック内で重い処理をしない（ISR コンテキスト）
- DMA を使うなら DMA 対応メモリを使う（`heap_caps_malloc` + `MALLOC_CAP_DMA`）
- Slave の `tx_buffer` / `rx_buffer` は、ドライバに渡した後は結果回収まで触らない
- ACK を同一トランザクション内で返す設計にしない
- GPIO 1本は状態線として使い、イベント線にしない
- `data[]` 内には複数の COBS メッセージを 0x00 区切りで連結可能。`data_len` は有効バイト数（0x00 区切り含む）

---

## サンプルコード

以下のサンプルは現在の実装構成（`comm_spi_slave.c`, `fmrb_hal_link_esp32.c`）に沿って記述している。

### 共通定義 (`spi_frame.h`)

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SPI_FRAME_MAGIC  0xA5
#define SPI_FRAME_SIZE   256
#define SPI_HEADER_SIZE  5
#define SPI_CRC_SIZE     2
#define SPI_MAX_DATA     (SPI_FRAME_SIZE - SPI_HEADER_SIZE - SPI_CRC_SIZE)  // 249

#define NUM_BUFFERS      2

typedef enum {
    STS_BOOT    = 0x00,
    STS_RX_OK   = 0x10,
    STS_APP_OK  = 0x12,
    STS_APP_ERR = 0x13,
    STS_CRC_ERR = 0xE1,
} spi_status_t;

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t seq;
    uint8_t ack_seq;
    uint8_t status;
    uint8_t data_len;
    uint8_t data[249];
    uint16_t crc16;
} spi_frame_t;

// CRC16-CCITT (polynomial 0x1021, init 0xFFFF)
static inline uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

// フレームヘッダ + CRC16 を構築（data[] は呼び出し前にセット済み前提）
static inline void spi_frame_finalize(spi_frame_t *f)
{
    f->crc16 = crc16_ccitt((const uint8_t *)f, SPI_FRAME_SIZE - SPI_CRC_SIZE);
}

// フレーム検証（magic + CRC16）
static inline bool spi_frame_validate(const spi_frame_t *f)
{
    if (f->magic != SPI_FRAME_MAGIC) return false;
    uint16_t expected = crc16_ccitt((const uint8_t *)f, SPI_FRAME_SIZE - SPI_CRC_SIZE);
    return expected == f->crc16;
}
```

### Slave 実装例（`comm_spi_slave.c` ベース）

現在の Slave は `comm_interface_t` 経由で `comm_task` から呼ばれる構成。
メッセージは MessageBuffer で `message_handler_task` に渡され、ACK は `s_ack_queue` 経由で返される。

```c
#include "comm_interface.h"
#include <string.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/message_buffer.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fmrb_pin_assign.h"   // FMRB_PIN_SPI_MOSI(21), MISO(18), CLK(19), CS(22), HANDSHAKE(23)
#include "comm_message.h"       // message_data_t
#include "fmrb_link_protocol.h" // fmrb_link_decode_frame(), FMRB_LINK_TYPE_EMPTY
#include "spi_frame.h"

static const char *TAG = "spi_slave";

#define SPI_HOST_ID  SPI2_HOST

// DMA-capable buffers (double buffered)
static uint8_t *rx_buffers[NUM_BUFFERS] = {NULL, NULL};
static uint8_t *tx_buffers[NUM_BUFFERS] = {NULL, NULL};
static spi_slave_transaction_t transactions[NUM_BUFFERS];
static int current_buf = 0;

static int spi_running = 0;
static SemaphoreHandle_t trans_ready_sem = NULL;
static MessageBufferHandle_t s_msg_buffer = NULL;

// ACK queue: message_handler_task enqueues via send_ack(), comm_task dequeues
typedef struct {
    uint8_t ack_seq;
    uint8_t status;
    uint8_t data[SPI_MAX_DATA];  // COBS エンコード済み応答（省略可）
    uint8_t data_len;
} ack_queue_item_t;

static QueueHandle_t s_ack_queue = NULL;

// 最新の応答状態（ACK キューから更新、fill_response が参照）
static volatile uint8_t s_last_status = STS_BOOT;
static volatile uint8_t s_last_ack_seq = 0;
static uint8_t s_resp_data[SPI_MAX_DATA];
static volatile uint8_t s_resp_data_len = 0;

// Pending transaction counter (ISR decrements, task increments)
static volatile int s_pending_trans = 0;
static volatile uint32_t s_queue_empty_count = 0;

// READY GPIO control (HIGH = ready to receive, LOW = busy)
static inline void IRAM_ATTR set_ready_high(void) {
    gpio_set_level(FMRB_PIN_SPI_HANDSHAKE, 1);
}
static inline void IRAM_ATTR set_ready_low(void) {
    gpio_set_level(FMRB_PIN_SPI_HANDSHAKE, 0);
}

// ISR: 転送開始（何もしない。READY は pending カウンタで管理）
static void IRAM_ATTR spi_post_setup_cb(spi_slave_transaction_t *trans)
{
    (void)trans;
}

// ISR: 転送完了 → pending 更新 + READY 制御 + セマフォ通知
static void IRAM_ATTR spi_post_trans_cb(spi_slave_transaction_t *trans)
{
    s_pending_trans--;
    if (s_pending_trans <= 0) {
        set_ready_low();  // キュー枯渇 → Master に送信禁止
        s_queue_empty_count++;
    }
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(trans_ready_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

// TX バッファに最新の応答フレームを構築
static void fill_response(uint8_t *tx_buf)
{
    spi_frame_t *f = (spi_frame_t *)tx_buf;
    memset(f, 0, SPI_FRAME_SIZE);
    f->magic = SPI_FRAME_MAGIC;
    f->seq = 0;
    f->ack_seq = s_last_ack_seq;
    f->status = s_last_status;
    f->data_len = s_resp_data_len;
    if (s_resp_data_len > 0) {
        memcpy(f->data, s_resp_data, s_resp_data_len);
    }
    spi_frame_finalize(f);
}

// スロット再キュー
static esp_err_t queue_next_transaction(void)
{
    int buf_idx = current_buf;
    transactions[buf_idx].length = SPI_FRAME_SIZE * 8;
    transactions[buf_idx].tx_buffer = tx_buffers[buf_idx];
    transactions[buf_idx].rx_buffer = rx_buffers[buf_idx];

    esp_err_t ret = spi_slave_queue_trans(SPI_HOST_ID, &transactions[buf_idx], 0);
    if (ret == ESP_OK) {
        s_pending_trans++;
    }
    return ret;
}

// 受信フレーム処理: spi_frame_t ヘッダ検証 → COBS ペイロード → MessageBuffer 送信
static int process_single_transaction(spi_slave_transaction_t *completed_trans)
{
    int buf_idx = (completed_trans->rx_buffer == rx_buffers[0]) ? 0 : 1;
    spi_frame_t *f = (spi_frame_t *)completed_trans->rx_buffer;

    // フレームヘッダ検証 + COBS ペイロード処理
    if (spi_frame_validate(f) && f->data_len > 0) {
        // 受信成功を即座に反映（Master がポーリングで確認可能）
        s_last_ack_seq = f->seq;
        s_last_status = STS_RX_OK;
        s_resp_data_len = 0;

        // data[] 内の複数 COBS メッセージを順次デコード → MessageBuffer へ
        size_t pos = 0;
        while (pos < f->data_len) {
            // 先頭の 0x00 をスキップ
            while (pos < f->data_len && f->data[pos] == 0x00) pos++;
            if (pos >= f->data_len) break;

            // COBS フレーム終端（0x00）を探す
            size_t frame_start = pos;
            while (pos < f->data_len && f->data[pos] != 0x00) pos++;
            size_t frame_len = pos - frame_start;

            // デコード
            message_data_t msg;
            size_t payload_len;
            int result = fmrb_link_decode_frame(
                f->data + frame_start, frame_len,
                &msg.type, &msg.seq, &msg.sub_cmd,
                msg.payload, sizeof(msg.payload), &payload_len);
            if (result == 0 && msg.type != FMRB_LINK_TYPE_EMPTY) {
                msg.payload_len = (uint16_t)payload_len;
                size_t send_size = offsetof(message_data_t, payload) + payload_len;
                xMessageBufferSend(s_msg_buffer, &msg, send_size, pdMS_TO_TICKS(100));
            }
        }
    } else if (f->magic == SPI_FRAME_MAGIC && !spi_frame_validate(f)) {
        // CRC エラー
        s_last_ack_seq = f->seq;
        s_last_status = STS_CRC_ERR;
        s_resp_data_len = 0;
    }
    // magic 不一致 or data_len==0: ダミーポーリング → 応答変更なし

    // ACK キューからレスポンス情報を取得（message_handler_task が send_ack 経由でキュー投入）
    ack_queue_item_t ack_item;
    if (xQueueReceive(s_ack_queue, &ack_item, 0) == pdTRUE) {
        s_last_ack_seq = ack_item.ack_seq;
        s_last_status = ack_item.status;
        s_resp_data_len = ack_item.data_len;
        if (ack_item.data_len > 0) {
            memcpy(s_resp_data, ack_item.data, ack_item.data_len);
        }
    }

    // TX バッファに応答を書き込み → 再キュー → pending>0 なら READY=HIGH
    current_buf = buf_idx;
    fill_response(tx_buffers[buf_idx]);
    queue_next_transaction();
    if (s_pending_trans > 0) {
        set_ready_high();
    }

    return 0;
}

// comm_interface_t::process — comm_task メインループから毎回呼ばれる
static int spi_process(void)
{
    if (!spi_running) return 0;
    if (xSemaphoreTake(trans_ready_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    int count = 0;
    spi_slave_transaction_t *completed;
    // 完了済みトランザクションをすべてドレイン
    while (spi_slave_get_trans_result(SPI_HOST_ID, &completed, 0) == ESP_OK) {
        process_single_transaction(completed);
        count++;
    }
    return count;
}

// comm_interface_t::send_ack — message_handler_task から呼ばれる
// 注意: この関数は成功時のみ呼ぶ（STS_APP_OK）。
// エラー時は呼ばず、Master はタイムアウトで検知する。
// 将来的にステータス引数を追加して STS_APP_ERR を返す拡張も検討。
static int spi_send_ack(uint8_t type, uint8_t seq,
                        const uint8_t *response_data, uint16_t response_len)
{
    ack_queue_item_t item = {
        .ack_seq = seq,
        .status = STS_APP_OK,
        .data_len = 0,
    };

    // 応答ペイロードがあれば COBS エンコードして data[] に格納
    if (response_data && response_len > 0) {
        size_t enc_len = 0;
        int enc_ret = fmrb_link_encode_ack(type, seq, response_data, response_len,
                                           item.data, SPI_MAX_DATA, &enc_len);
        if (enc_ret != 0) {
            ESP_LOGE(TAG, "Failed to encode ACK");
            return -1;
        }
        item.data_len = (uint8_t)enc_len;
    }

    if (xQueueSend(s_ack_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "ACK queue full");
        return -1;
    }
    return 0;
}

// comm_interface_t::init
static int spi_init(MessageBufferHandle_t msg_buffer)
{
    s_msg_buffer = msg_buffer;

    // DMA バッファ確保
    for (int i = 0; i < NUM_BUFFERS; i++) {
        rx_buffers[i] = heap_caps_malloc(SPI_FRAME_SIZE, MALLOC_CAP_DMA);
        tx_buffers[i] = heap_caps_malloc(SPI_FRAME_SIZE, MALLOC_CAP_DMA);
        if (!rx_buffers[i] || !tx_buffers[i]) return -1;
        memset(rx_buffers[i], 0, SPI_FRAME_SIZE);
        memset(tx_buffers[i], 0, SPI_FRAME_SIZE);
    }

    s_ack_queue = xQueueCreate(2, sizeof(ack_queue_item_t));
    trans_ready_sem = xSemaphoreCreateBinary();

    // READY GPIO 初期化（LOW = 起動中は送信禁止）
    gpio_config_t hs_conf = {
        .pin_bit_mask = (1ULL << FMRB_PIN_SPI_HANDSHAKE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&hs_conf);
    set_ready_low();

    // SPI slave 初期化
    spi_bus_config_t buscfg = {
        .mosi_io_num = FMRB_PIN_SPI_MOSI,   // GPIO 21
        .miso_io_num = FMRB_PIN_SPI_MISO,   // GPIO 18
        .sclk_io_num = FMRB_PIN_SPI_CLK,    // GPIO 19
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = SPI_FRAME_SIZE,
    };
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = FMRB_PIN_SPI_CS,    // GPIO 22
        .queue_size = NUM_BUFFERS,
        .flags = 0,
        .post_setup_cb = spi_post_setup_cb,
        .post_trans_cb = spi_post_trans_cb,
    };

    gpio_set_pull_mode(FMRB_PIN_SPI_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(FMRB_PIN_SPI_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(FMRB_PIN_SPI_CS, GPIO_PULLUP_ONLY);

    ESP_ERROR_CHECK(spi_slave_initialize(SPI_HOST_ID, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));

    // 初期 TX バッファ: BOOT 状態の空フレーム → キュー投入
    for (int i = 0; i < NUM_BUFFERS; i++) {
        fill_response(tx_buffers[i]);
        current_buf = i;
        queue_next_transaction();
    }
    s_pending_trans = NUM_BUFFERS;
    current_buf = 0;

    set_ready_high();
    spi_running = 1;
    ESP_LOGI(TAG, "SPI slave initialized - MOSI:%d MISO:%d CLK:%d CS:%d HS:%d",
             FMRB_PIN_SPI_MOSI, FMRB_PIN_SPI_MISO, FMRB_PIN_SPI_CLK,
             FMRB_PIN_SPI_CS, FMRB_PIN_SPI_HANDSHAKE);
    return 0;
}

static int spi_is_running(void) { return spi_running; }
static void spi_cleanup(void) { /* 省略: バッファ解放, SPI 解除, キュー削除 */ }

// comm_interface_t 登録
static const comm_interface_t spi_comm = {
    .init = spi_init,
    .send = NULL,
    .receive = NULL,
    .process = spi_process,
    .send_ack = spi_send_ack,
    .is_running = spi_is_running,
    .cleanup = spi_cleanup,
};

const comm_interface_t* comm_get_interface(void) {
    return &spi_comm;
}
```

タスク構成（`main_esp32.cpp` から抜粋）:

```c
// comm_task: SPI ポーリングループ（comm->process() を繰り返し呼ぶ）
// message_handler_task: MessageBuffer からメッセージ受信 → コマンド処理 → comm->send_ack()
MessageBufferHandle_t msg_buffer = xMessageBufferCreate(1024);

xTaskCreatePinnedToCore(comm_task, "comm_task", 4096,
                        (void *)msg_buffer, 19, NULL, 0);
xTaskCreatePinnedToCore(message_handler_task, "msg_handler", 4096,
                        (void *)msg_buffer, 10, NULL, 1);
```

### Master 実装例（`fmrb_hal_link_esp32.c` ベース）

現在の Master は `spi_mutex` で SPI アクセスを排他し、`ack_notify_sem` で GPIO ISR からの通知を受ける構成。

```c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fmrb_pin_assign.h"  // FMRB_PIN_GFX_SPI_MOSI(11), MISO(13), SCLK(12), CS(10), INTR(9)
#include "spi_frame.h"

static const char *TAG = "spi_master";

#define SPI_HOST_USED  SPI2_HOST
#define SPI_FREQUENCY  (10 * 1000 * 1000)  // 10MHz

static spi_device_handle_t s_spi;
static SemaphoreHandle_t spi_mutex = NULL;
static SemaphoreHandle_t ack_notify_sem = NULL;

// DMA バッファ（ヒープ確保、スタック上は不可）
static uint8_t *s_tx_dma_buf = NULL;
static uint8_t *s_rx_dma_buf = NULL;

// READY 立ち上がり ISR → セマフォ通知
static void IRAM_ATTR ready_isr(void *arg)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(ack_notify_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

// READY=HIGH を待つ（タイムアウト付き）
static bool wait_ready(uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if (gpio_get_level(FMRB_PIN_GFX_SPI_INTR) == 1) {
            return true;
        }
        xSemaphoreTake(ack_notify_sem, pdMS_TO_TICKS(10));
    }
    return false;
}

// SPI 転送: spi_frame_t を送信し、同時にスレーブの応答フレームを受信
static esp_err_t spi_transfer_frame(const spi_frame_t *tx_frame, spi_frame_t *rx_frame)
{
    memcpy(s_tx_dma_buf, tx_frame, SPI_FRAME_SIZE);
    memset(s_rx_dma_buf, 0, SPI_FRAME_SIZE);

    spi_transaction_t t = {0};
    t.length = SPI_FRAME_SIZE * 8;
    t.tx_buffer = s_tx_dma_buf;
    t.rx_buffer = s_rx_dma_buf;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    if (ret == ESP_OK && rx_frame) {
        memcpy(rx_frame, s_rx_dma_buf, SPI_FRAME_SIZE);
    }
    return ret;
}

// コマンドフレーム構築
static void build_command_frame(spi_frame_t *f, uint8_t seq,
                                const uint8_t *cobs_data, uint8_t cobs_len)
{
    memset(f, 0, SPI_FRAME_SIZE);
    f->magic = SPI_FRAME_MAGIC;
    f->seq = seq;
    f->ack_seq = 0;
    f->status = 0;
    f->data_len = cobs_len;
    if (cobs_data && cobs_len > 0) {
        memcpy(f->data, cobs_data, cobs_len);
    }
    spi_frame_finalize(f);
}

// 応答フレームの data[] 内の複数 COBS メッセージを順次デコード
// callback に各デコード済みペイロードを渡す
typedef void (*rx_data_callback_t)(const uint8_t *decoded, size_t decoded_len, void *ctx);

static void process_rx_cobs_data(const spi_frame_t *f,
                                  rx_data_callback_t callback, void *ctx)
{
    if (f->data_len == 0) return;

    size_t pos = 0;
    while (pos < f->data_len) {
        // 先頭の 0x00 をスキップ
        while (pos < f->data_len && f->data[pos] == 0x00) pos++;
        if (pos >= f->data_len) break;

        // COBS フレーム終端（0x00）を探す
        size_t frame_start = pos;
        while (pos < f->data_len && f->data[pos] != 0x00) pos++;
        size_t frame_len = pos - frame_start;

        // COBS デコード
        uint8_t decoded[SPI_MAX_DATA];
        ssize_t decoded_len = fmrb_link_cobs_decode(
            f->data + frame_start, frame_len, decoded);
        if (decoded_len > 0 && callback) {
            callback(decoded, (size_t)decoded_len, ctx);
        }
    }
}

// 同期コマンド送信: COBS ペイロードを送り、STS_APP_OK/ERR まで待つ
static esp_err_t send_command_sync(uint8_t seq,
                                   const uint8_t *cobs_data, uint8_t cobs_len,
                                   spi_frame_t *ack_out, uint32_t timeout_ms)
{
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    spi_frame_t tx, rx;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    // READY を待ってコマンド送信
    if (!wait_ready(timeout_ms)) {
        xSemaphoreGive(spi_mutex);
        return ESP_ERR_TIMEOUT;
    }

    build_command_frame(&tx, seq, cobs_data, cobs_len);
    spi_transfer_frame(&tx, &rx);

    // ポーリング: seq に一致する最終応答を待つ
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        uint32_t remaining = timeout_ms - ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
        if (!wait_ready(remaining)) {
            xSemaphoreGive(spi_mutex);
            return ESP_ERR_TIMEOUT;
        }

        // ダミーフレーム (data_len=0) でポーリング
        build_command_frame(&tx, 0, NULL, 0);
        spi_transfer_frame(&tx, &rx);

        if (!spi_frame_validate(&rx) || rx.ack_seq != seq) {
            continue;
        }
        if (rx.status == STS_RX_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        // 最終結果
        if (ack_out) memcpy(ack_out, &rx, sizeof(rx));
        xSemaphoreGive(spi_mutex);
        return (rx.status == STS_APP_OK) ? ESP_OK : ESP_FAIL;
    }

    xSemaphoreGive(spi_mutex);
    return ESP_ERR_TIMEOUT;
}

// 初期化
void spi_master_init(void)
{
    // DMA バッファ確保（スタック上は不可）
    s_tx_dma_buf = heap_caps_malloc(SPI_FRAME_SIZE, MALLOC_CAP_DMA);
    s_rx_dma_buf = heap_caps_malloc(SPI_FRAME_SIZE, MALLOC_CAP_DMA);
    spi_mutex = xSemaphoreCreateMutex();
    ack_notify_sem = xSemaphoreCreateBinary();

    // READY GPIO（立ち上がり割り込み）
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << FMRB_PIN_GFX_SPI_INTR,  // GPIO 9
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(FMRB_PIN_GFX_SPI_INTR, ready_isr, NULL);

    // SPI master 初期化
    spi_bus_config_t buscfg = {
        .mosi_io_num = FMRB_PIN_GFX_SPI_MOSI,  // GPIO 11
        .miso_io_num = FMRB_PIN_GFX_SPI_MISO,  // GPIO 13
        .sclk_io_num = FMRB_PIN_GFX_SPI_SCLK,  // GPIO 12
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = SPI_FRAME_SIZE,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_FREQUENCY,
        .mode = 0,
        .spics_io_num = FMRB_PIN_GFX_SPI_CS,   // GPIO 10
        .queue_size = 4,
    };
    spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI_HOST_USED, &devcfg, &s_spi);

    ESP_LOGI(TAG, "SPI master init (MOSI=%d MISO=%d SCLK=%d CS=%d READY=%d, %dMHz)",
             FMRB_PIN_GFX_SPI_MOSI, FMRB_PIN_GFX_SPI_MISO,
             FMRB_PIN_GFX_SPI_SCLK, FMRB_PIN_GFX_SPI_CS,
             FMRB_PIN_GFX_SPI_INTR, SPI_FREQUENCY / 1000000);
}
```

---

## 実運用上の補足

### 1. READY の意味

GPIO 1本は次に固定する。

- HIGH = 少なくとも 1 本は受信スロットあり。送ってよい
- LOW = 今は送るな

これだけにする。ACK や 完了通知 の意味を混ぜない。

### 2. ACK が遅い場合

アプリ処理が重い場合、Master は状態の 2 段階で判断する。

- `STS_RX_OK` = 受信完了（アプリ未処理）
- `STS_APP_OK` / `STS_APP_ERR` = 最終結果

Master は `STS_RX_OK` を見て「受信済みだが処理中」と判断し、ポーリングを続ける。

### 3. 割り込みの使いどころ

GPIO 割り込みは Master の待機解除にだけ使う。
ISR 本体ではセマフォ/キュー通知だけにする。重い処理を置かない。

### 4. 送信種別の分離

**同期コマンド**（設定変更、状態遷移、保存など）:
- `STS_APP_OK` まで待つ

**非同期コマンド**（描画投入、ストリーム投入など）:
- `STS_RX_OK` が見えたら次へ進めてもよい
- 最終結果は統計やエラーカウンタで見る

### 5. チャンキング（大きいメッセージの分割送信）

`SPI_MAX_DATA` (249 bytes) を超えるメッセージは、既存の `fmrb_link_fragment` 層でチャンク分割する。
各チャンクは個別の SPI フレームとして送信される。
チャンク ACK（credit / offset 情報）は COBS ペイロードとして `data[]` に格納。

### 6. seq の予約値

`seq=0` はデータなしポーリング専用。有効なコマンドは `seq >= 1` を使用する。
Slave は `seq=0` のフレームを受信した場合、`s_last_ack_seq` / `s_last_status` を更新せず、現在の応答状態をそのまま返す。

### 7. ダブルバッファと seq マッチング

Slave は 2 本のスロットをキューしている。完了したスロットから順に処理・再キューするため、
Master が受け取る応答フレームの `ack_seq` が必ずしも直前のコマンドに対応するとは限らない。
Master は必ず `ack_seq` を確認し、一致するまでポーリングを続ける。

### 8. エラー応答の方針

現行の `comm_interface_t::send_ack` は成功時のみ呼ぶ設計。
エラー時は `send_ack` を呼ばず、Master はタイムアウトで検知する。
将来的に `send_ack` にステータス引数を追加し、`STS_APP_ERR` を明示的に返す拡張を検討。

### 9. CRC の方針（トランスポート層に委ねる）

msgpack ペイロードに対する CRC32 は **付与しない**。整合性検証はトランスポート層が担保する。

| トランスポート | 整合性保証 | アプリ層 CRC |
|---|---|---|
| SPI | フレームヘッダの CRC16-CCITT | 不要 |
| TCP（ソケットサーバー） | TCP チェックサム | 不要 |

COBS の役割は **フレーム区切り**（0x00 ターミネータ）のみ。

```
SPI:  msgpack → COBS → spi_frame_t.data[] + CRC16
TCP:  msgpack → COBS → ソケット送信（TCP が保証）
```

### 10. この方式の利点

- GPIO 1本でも意味がぶれない
- Master 側ポーリング負荷を減らせる（READY で制御）
- Slave 側の重いアプリ処理と SPI 受信を分離できる
- `seq` があるので応答の取り違えが起きにくい
- COBS + msgpack の既存エンコード層を再利用できる
- フレームヘッダの CRC16 で SPI 転送レベルの整合性も検証できる
- CRC をトランスポート層に一元化し、レイヤー間の重複を排除
