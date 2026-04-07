#include "spi_conn_check.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"
#include "fmrb_pin_assign.h"
#include "fmrb_mem.h"
#include "fmrb_link_protocol.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif

static const char *TAG = "spi_conn_check";

// SPI configuration - use pins from fmrb_pin_assign.h
#define SPI_MOSI_PIN    FMRB_PIN_GFX_SPI_MOSI
#define SPI_MISO_PIN    FMRB_PIN_GFX_SPI_MISO
#define SPI_SCLK_PIN    FMRB_PIN_GFX_SPI_SCLK
#define SPI_CS_PIN      FMRB_PIN_GFX_SPI_CS
#define SPI_FREQUENCY   (10 * 1000 * 1000)  // 10MHz

static fmrb_spi_handle_t spi_handle = NULL;
static fmrb_task_handle_t spi_task_handle = NULL;
static volatile int spi_running = 0;

// DMA-capable buffers
static uint8_t *tx_buffer = NULL;
static uint8_t *rx_buffer = NULL;

static void spi_conn_check_task(void *arg)
{
    FMRB_LOGI(TAG, "SPI connection check task started on core %d", fmrb_get_core_id());

    int send_count = 0;

    while (spi_running) {
        send_count++;

        // Send test data every 5 seconds (500 * 10ms)
        if (send_count >= 500) {
            // Prepare test data in DMA buffer
            memset(tx_buffer, 0, FMRB_LINK_FRAME_SIZE);
            tx_buffer[0] = 0xAA;
            tx_buffer[1] = 0x55;
            tx_buffer[2] = 0x01;
            tx_buffer[3] = 0x02;
            tx_buffer[4] = 0x03;
            tx_buffer[5] = 0x04;

            memset(rx_buffer, 0, FMRB_LINK_FRAME_SIZE);

            FMRB_LOGI(TAG, "Sending %d bytes (frame_size=%d)...", 6, FMRB_LINK_FRAME_SIZE);

            // Transfer full frame
            fmrb_err_t ret = fmrb_hal_spi_transfer(spi_handle, tx_buffer, rx_buffer, FMRB_LINK_FRAME_SIZE, 1000);
            if (ret == FMRB_OK) {
                FMRB_LOGI(TAG, "SPI transfer OK");
                FMRB_LOGI(TAG, "TX: %02X %02X %02X %02X %02X %02X ...",
                         tx_buffer[0], tx_buffer[1], tx_buffer[2],
                         tx_buffer[3], tx_buffer[4], tx_buffer[5]);
                FMRB_LOGI(TAG, "RX: %02X %02X %02X %02X %02X %02X %02X %02X ...",
                         rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3],
                         rx_buffer[4], rx_buffer[5], rx_buffer[6], rx_buffer[7]);
            } else {
                FMRB_LOGE(TAG, "SPI transfer failed: %d", ret);
            }

            send_count = 0;
        }

        fmrb_task_delay_ms(10);
    }

    FMRB_LOGI(TAG, "SPI connection check task exiting");
    fmrb_task_delete(NULL);
}

int spi_conn_check_init(void)
{
    if (spi_handle != NULL) {
        FMRB_LOGW(TAG, "SPI already initialized");
        return 0;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    // Allocate DMA-capable buffers
    tx_buffer = (uint8_t *)heap_caps_malloc(FMRB_LINK_FRAME_SIZE, MALLOC_CAP_DMA);
    rx_buffer = (uint8_t *)heap_caps_malloc(FMRB_LINK_FRAME_SIZE, MALLOC_CAP_DMA);
#else
    tx_buffer = (uint8_t *)fmrb_sys_malloc(FMRB_LINK_FRAME_SIZE);
    rx_buffer = (uint8_t *)fmrb_sys_malloc(FMRB_LINK_FRAME_SIZE);
#endif

    if (!tx_buffer || !rx_buffer) {
        FMRB_LOGE(TAG, "Failed to allocate DMA buffers");
        if (tx_buffer) {
#ifndef CONFIG_IDF_TARGET_LINUX
            heap_caps_free(tx_buffer);
#else
            fmrb_sys_free(tx_buffer);
#endif
        }
        if (rx_buffer) {
#ifndef CONFIG_IDF_TARGET_LINUX
            heap_caps_free(rx_buffer);
#else
            fmrb_sys_free(rx_buffer);
#endif
        }
        tx_buffer = NULL;
        rx_buffer = NULL;
        return -1;
    }

    FMRB_LOGI(TAG, "DMA buffers allocated at tx=%p rx=%p (size=%d)",
             tx_buffer, rx_buffer, FMRB_LINK_FRAME_SIZE);

    fmrb_spi_config_t config = {
        .mosi_pin = SPI_MOSI_PIN,
        .miso_pin = SPI_MISO_PIN,
        .sclk_pin = SPI_SCLK_PIN,
        .cs_pin = SPI_CS_PIN,
        .frequency = SPI_FREQUENCY
    };

    fmrb_err_t ret = fmrb_hal_spi_init(&config, &spi_handle);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "SPI initialization failed: %d", ret);
#ifndef CONFIG_IDF_TARGET_LINUX
        heap_caps_free(tx_buffer);
        heap_caps_free(rx_buffer);
#else
        fmrb_sys_free(tx_buffer);
        fmrb_sys_free(rx_buffer);
#endif
        tx_buffer = NULL;
        rx_buffer = NULL;
        return -1;
    }

    FMRB_LOGI(TAG, "SPI initialized - MOSI:%d MISO:%d SCLK:%d CS:%d @ %dHz (frame=%d)",
             SPI_MOSI_PIN, SPI_MISO_PIN, SPI_SCLK_PIN, SPI_CS_PIN, SPI_FREQUENCY, FMRB_LINK_FRAME_SIZE);

    return 0;
}

void spi_conn_check_start(void)
{
    if (spi_handle == NULL) {
        FMRB_LOGE(TAG, "SPI not initialized, call spi_conn_check_init() first");
        return;
    }

    if (spi_running) {
        FMRB_LOGW(TAG, "SPI task already running");
        return;
    }

    spi_running = 1;

    fmrb_base_type_t ret = fmrb_task_create_ex(
        spi_conn_check_task,
        "spi_conn_check",
        FMRB_SPI_CONN_TASK_STACK_SIZE,
        NULL,
        FMRB_SPI_CONN_TASK_PRIORITY,
        &spi_task_handle,
        FMRB_SPI_CONN_TASK_FLAGS
    );

    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create SPI task");
        spi_running = 0;
    }
}

void spi_conn_check_stop(void)
{
    if (!spi_running) {
        return;
    }

    spi_running = 0;

    // Wait for task to exit
    fmrb_task_delay_ms(100);

    if (spi_handle != NULL) {
        fmrb_hal_spi_deinit(spi_handle);
        spi_handle = NULL;
    }

    // Free DMA buffers
    if (tx_buffer) {
#ifndef CONFIG_IDF_TARGET_LINUX
        heap_caps_free(tx_buffer);
#else
        fmrb_sys_free(tx_buffer);
#endif
        tx_buffer = NULL;
    }
    if (rx_buffer) {
#ifndef CONFIG_IDF_TARGET_LINUX
        heap_caps_free(rx_buffer);
#else
        fmrb_sys_free(rx_buffer);
#endif
        rx_buffer = NULL;
    }

    FMRB_LOGI(TAG, "SPI connection check stopped");
}
