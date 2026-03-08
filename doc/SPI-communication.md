# ESP32 SPI 通信方針（GPIO 1本 + ACKはSPI返送）

## 結論

GPIO が 1 本しかないなら、役割は次で固定する。

- GPIO 1本 = `READY`（送ってよいかの状態線）
- ACK（Acknowledge：受信確認応答）や処理結果 = `SPI MISO（Master In Slave Out：マスター受信/スレーブ送信）` で返す
- ACK は **同一トランザクション内の後出し** ではなく、**次回トランザクションで返す** 前提にする

SPI slave（SPIスレーブ：受け側）は、マスターがクロックを出したときにすでに送信バッファが決まっている必要がある。  
また ESP-IDF（Espressif IoT Development Framework：Espressif公式開発基盤）の slave API は、キューしたトランザクションのバッファ所有権をドライバに渡す前提なので、**事前に受信用 / 返答用スロットを複数本キューする**設計が必要。 :contentReference[oaicite:0]{index=0}

## 実装方針

### 1. プロトコル方針

1. Master（通信開始側）は `READY=HIGH` のときだけ送信する
2. Slave は常に `spi_slave_queue_trans()` を 2 本以上積んで受け口を確保する
3. Slave は受信後、アプリ処理結果を `next response` に反映する
4. Master は次回送信時に、前回分の `seq + status` を同時に読む
5. GPIO 割り込みは **Master側の待機解除** にだけ使う
6. ACK を GPIO パルスで表そうとしない


### 2. ステータス設計

```
#define STS_BOOT      0x00  // 初期状態
#define STS_RX_OK     0x10  // 受信完了
#define STS_BUSY      0x11  // アプリ処理中
#define STS_APP_OK    0x12  // アプリ処理成功
#define STS_APP_ERR   0x13  // アプリ処理失敗
#define STS_CRC_ERR   0xE1  // CRC異常
```


3. フレーム構造
```
typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t seq;
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[248];
    uint16_t crc16;
    uint8_t reserved[2];
} spi_frame_t;   // 256 bytes
```

返答は先頭数バイトで十分。

```
typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t ack_seq;
    uint8_t status;
    uint8_t result;
    uint8_t reserved[252];
} spi_resp_t;    // 256 bytes
```

## 役割分担

### Master 側

READY GPIO を入力

READY の立ち上がり割り込みで送信タスクを起こす

READY=HIGH の間だけ送信する

SPI 送信と同時に rx_buffer を受け、そこから前回分の ack_seq + status を読む

重要コマンドだけ STS_APP_OK まで待つ

ESP-IDF の master driver では、spi_device_interface_config_t に pre_cb と post_cb を設定できる。これはトランザクション前後の軽いGPIO同期用で、重い処理を置く場所ではない。

### Slave 側

READY GPIO を出力

spi_slave_queue_trans() を常時 2 本以上維持

post_setup_cb は「転送が始まった」の軽い通知だけ

post_trans_cb は ISR（Interrupt Service Routine：割り込み処理）安全な通知だけ

本処理はタスク側で行う

処理結果は 次回返送用TXバッファ に書く

ESP-IDF の slave driver には post_setup_cb と post_trans_cb があり、1 回の SPI トランザクションは master の CS アサートと SCLK によって始まる full duplex（全二重）通信として扱われる。

### 注意点

コールバック内で重い処理をしない

DMA（Direct Memory Access：CPUを介さない転送）を使うなら DMA 対応メモリを使う

Slave の tx_buffer / rx_buffer は、ドライバに渡した後は結果回収まで触らない

ACK を同一トランザクションの「受信後に即返す」設計にしない

GPIO 1本は状態線として使い、イベント線にしない

ESP-IDF の master / slave ドライバはキュー型で動き、slave 側はキューしたトランザクションのバッファ所有権をドライバに渡す。

## サンプルコード

### 共通定義

```
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FRAME_SIZE 256
#define QUEUE_DEPTH 2

#define PROTO_MAGIC 0xA5

#define STS_BOOT      0x00
#define STS_RX_OK     0x10
#define STS_BUSY      0x11
#define STS_APP_OK    0x12
#define STS_APP_ERR   0x13
#define STS_CRC_ERR   0xE1

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t seq;
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[248];
    uint16_t crc16;
    uint8_t reserved[2];
} spi_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t ack_seq;
    uint8_t status;
    uint8_t result;
    uint8_t reserved[252];
} spi_resp_t;

static inline uint16_t crc16_dummy(const uint8_t *data, int len)
{
    uint16_t c = 0;
    for (int i = 0; i < len; i++) {
        c = (uint16_t)(c + data[i]);
    }
    return c;
}
```


### Slave 実装例

```
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"

#include "proto.h"

// ピンは適宜変更
#define PIN_NUM_MOSI   23
#define PIN_NUM_MISO   19
#define PIN_NUM_CLK    18
#define PIN_NUM_CS      5
#define PIN_NUM_READY  22

#define SPI_HOST_USED SPI2_HOST
static const char *TAG = "spi_slave";

typedef struct {
    spi_slave_transaction_t trans;
    uint8_t *rx_buf;
    uint8_t *tx_buf;
    int slot_id;
} slave_slot_t;

static QueueHandle_t s_done_queue;
static slave_slot_t s_slots[QUEUE_DEPTH];

static volatile uint8_t s_last_status = STS_BOOT;
static volatile uint8_t s_last_ack_seq = 0;
static volatile uint8_t s_last_result = 0;

static void IRAM_ATTR post_setup_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(PIN_NUM_READY, 0);
}

static void IRAM_ATTR post_trans_cb(spi_slave_transaction_t *trans)
{
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(s_done_queue, &trans, &hp);
    if (hp) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t init_ready_gpio(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_NUM_READY,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_NUM_READY, 0);
    return ESP_OK;
}

static esp_err_t init_spi_slave_dev(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FRAME_SIZE,
    };

    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = QUEUE_DEPTH,
        .flags = 0,
        .post_setup_cb = post_setup_cb,
        .post_trans_cb = post_trans_cb,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(SPI_HOST_USED, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));
    return ESP_OK;
}

static void fill_response(uint8_t *tx_buf)
{
    spi_resp_t *r = (spi_resp_t *)tx_buf;
    memset(r, 0, sizeof(*r));
    r->magic = PROTO_MAGIC;
    r->ack_seq = s_last_ack_seq;
    r->status = s_last_status;
    r->result = s_last_result;
}

static esp_err_t queue_slot(slave_slot_t *slot)
{
    memset(slot->rx_buf, 0, FRAME_SIZE);
    fill_response(slot->tx_buf);

    memset(&slot->trans, 0, sizeof(slot->trans));
    slot->trans.length = FRAME_SIZE * 8;
    slot->trans.rx_buffer = slot->rx_buf;
    slot->trans.tx_buffer = slot->tx_buf;
    slot->trans.user = slot;

    return spi_slave_queue_trans(SPI_HOST_USED, &slot->trans, portMAX_DELAY);
}

static bool validate_frame(const spi_frame_t *f)
{
    if (f->magic != PROTO_MAGIC) {
        return false;
    }
    uint16_t crc = crc16_dummy((const uint8_t *)f, FRAME_SIZE - 4);
    return crc == f->crc16;
}

static void app_process_frame(const spi_frame_t *f, uint8_t *result, uint8_t *status)
{
    // ここは例
    // 実際には cmd ごとの処理に差し替える
    vTaskDelay(pdMS_TO_TICKS(5));

    if (f->cmd == 0x01) {
        *result = f->payload[0];
        *status = STS_APP_OK;
    } else {
        *result = 0xFF;
        *status = STS_APP_ERR;
    }
}

static void worker_task(void *arg)
{
    while (1) {
        spi_slave_transaction_t *done = NULL;
        if (xQueueReceive(s_done_queue, &done, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        slave_slot_t *slot = (slave_slot_t *)done->user;
        spi_frame_t *f = (spi_frame_t *)slot->rx_buf;

        if (!validate_frame(f)) {
            s_last_ack_seq = f->seq;
            s_last_status = STS_CRC_ERR;
            s_last_result = 0;
        } else {
            s_last_ack_seq = f->seq;
            s_last_status = STS_RX_OK;
            s_last_result = 0;

            // 重いアプリ処理はここ
            s_last_status = STS_BUSY;
            uint8_t result = 0;
            uint8_t final_status = STS_APP_ERR;
            app_process_frame(f, &result, &final_status);

            s_last_result = result;
            s_last_status = final_status;
        }

        ESP_ERROR_CHECK(queue_slot(slot));

        // ここで少なくとも1本再キュー済み
        gpio_set_level(PIN_NUM_READY, 1);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_ready_gpio());

    s_done_queue = xQueueCreate(8, sizeof(spi_slave_transaction_t *));
    if (!s_done_queue) {
        ESP_LOGE(TAG, "queue create failed");
        return;
    }

    for (int i = 0; i < QUEUE_DEPTH; i++) {
        s_slots[i].slot_id = i;
        s_slots[i].rx_buf = heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_DMA);
        s_slots[i].tx_buf = heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_DMA);
        if (!s_slots[i].rx_buf || !s_slots[i].tx_buf) {
            ESP_LOGE(TAG, "dma alloc failed");
            return;
        }
    }

    ESP_ERROR_CHECK(init_spi_slave_dev());

    for (int i = 0; i < QUEUE_DEPTH; i++) {
        ESP_ERROR_CHECK(queue_slot(&s_slots[i]));
    }

    gpio_set_level(PIN_NUM_READY, 1);
    xTaskCreate(worker_task, "worker_task", 4096, NULL, 10, NULL);
}
```

### Master 実装例

```
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "proto.h"

// ピンは適宜変更
#define PIN_NUM_MOSI   23
#define PIN_NUM_MISO   19
#define PIN_NUM_CLK    18
#define PIN_NUM_CS      5
#define PIN_NUM_READY  22

#define SPI_HOST_USED SPI2_HOST
static const char *TAG = "spi_master";

static spi_device_handle_t s_spi;
static QueueHandle_t s_ready_evt_queue;

static void IRAM_ATTR ready_isr(void *arg)
{
    BaseType_t hp = pdFALSE;
    uint32_t ev = 1;
    xQueueSendFromISR(s_ready_evt_queue, &ev, &hp);
    if (hp) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t init_ready_gpio_input(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_NUM_READY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_NUM_READY, ready_isr, NULL));
    return ESP_OK;
}

static esp_err_t init_spi_master_dev(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FRAME_SIZE,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 4,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_USED, &devcfg, &s_spi));
    return ESP_OK;
}

static void build_frame(spi_frame_t *f, uint8_t seq, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    memset(f, 0, sizeof(*f));
    f->magic = PROTO_MAGIC;
    f->seq = seq;
    f->cmd = cmd;
    f->len = len;
    if (payload && len > 0) {
        memcpy(f->payload, payload, len);
    }
    f->crc16 = crc16_dummy((const uint8_t *)f, FRAME_SIZE - 4);
}

static esp_err_t send_one_and_get_prev_ack(spi_frame_t *txf, spi_resp_t *rxr)
{
    spi_transaction_t t = {0};
    t.length = FRAME_SIZE * 8;
    t.tx_buffer = txf;
    t.rx_buffer = rxr;
    return spi_device_transmit(s_spi, &t);
}

static bool wait_ready(TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if (gpio_get_level(PIN_NUM_READY) == 1) {
            return true;
        }

        uint32_t ev;
        xQueueReceive(s_ready_evt_queue, &ev, pdMS_TO_TICKS(10));
    }
    return false;
}

static esp_err_t send_command_sync(uint8_t seq, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    spi_frame_t txf;
    spi_resp_t rxr;

    if (!wait_ready(pdMS_TO_TICKS(1000))) {
        ESP_LOGE(TAG, "ready timeout");
        return ESP_ERR_TIMEOUT;
    }

    build_frame(&txf, seq, cmd, payload, len);
    ESP_ERROR_CHECK(send_one_and_get_prev_ack(&txf, &rxr));

    // ここで rxr は「前回分」。
    // 今回の最終結果が必要なら、ダミー送信で後続ポーリングする。
    for (int i = 0; i < 100; i++) {
        if (!wait_ready(pdMS_TO_TICKS(1000))) {
            return ESP_ERR_TIMEOUT;
        }

        spi_frame_t poll = {0};
        build_frame(&poll, 0, 0xFF, NULL, 0);

        memset(&rxr, 0, sizeof(rxr));
        ESP_ERROR_CHECK(send_one_and_get_prev_ack(&poll, &rxr));

        if (rxr.magic != PROTO_MAGIC) {
            continue;
        }
        if (rxr.ack_seq != seq) {
            continue;
        }

        if (rxr.status == STS_BUSY || rxr.status == STS_RX_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        if (rxr.status == STS_APP_OK) {
            ESP_LOGI(TAG, "seq=%u app ok result=%u", seq, rxr.result);
            return ESP_OK;
        }

        ESP_LOGE(TAG, "seq=%u failed status=0x%02X", seq, rxr.status);
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

void app_main(void)
{
    s_ready_evt_queue = xQueueCreate(8, sizeof(uint32_t));
    if (!s_ready_evt_queue) {
        ESP_LOGE(TAG, "queue create failed");
        return;
    }

    ESP_ERROR_CHECK(init_ready_gpio_input());
    ESP_ERROR_CHECK(init_spi_master_dev());

    uint8_t payload[1] = { 42 };
    esp_err_t err = send_command_sync(1, 0x01, payload, 1);
    ESP_LOGI(TAG, "send result=%s", esp_err_to_name(err));
}
```

## 実運用上の補足

1. READY の意味

GPIO 1本は次に固定する。

HIGH = 少なくとも1本は受信スロットあり。送ってよい

LOW = 今は送るな

これだけにする。
ACK や 完了通知 の意味を混ぜない。

2. ACK が遅い場合

アプリ処理が重いなら、状態は 2 段階以上に分ける。

STS_RX_OK = 受信だけ完了

STS_BUSY = アプリ処理中

STS_APP_OK / STS_APP_ERR = 最終結果

これで Master は「受信済み」と「処理完了」を分けて扱える。

3. 割り込みの使いどころ

GPIO 割り込みは Master の待機解除 にだけ使う。
割り込みの本体では重い処理をしない。
ISR ではキュー通知だけにする。

4. 送信種別の分離
同期コマンド

設定変更、状態遷移、保存など。

STS_APP_OK まで待つ

非同期コマンド

描画投入、ストリーム投入など。

STS_RX_OK が見えたら次へ進めてもよい

最終結果は統計やエラーカウンタで見る

5. この方式の利点

GPIO 1本でも意味がぶれない

Master 側ポーリング負荷を減らせる

Slave 側の重いアプリ処理と SPI 受信を分離できる

seq があるので応答の取り違えが起きにくい