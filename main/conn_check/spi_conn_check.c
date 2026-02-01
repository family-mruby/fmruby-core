#include "spi_conn_check.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_pin_assign.h"

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

static void spi_conn_check_task(void *arg)
{
    FMRB_LOGI(TAG, "SPI connection check task started on core %d", fmrb_get_core_id());

    // Test data
    uint8_t tx_data[] = {0xAA, 0x55, 0x01, 0x02, 0x03, 0x04};
    uint8_t rx_data[sizeof(tx_data)] = {0};
    int send_count = 0;

    while (spi_running) {
        send_count++;

        // Send test data every 5 seconds (500 * 10ms)
        if (send_count >= 500) {
            FMRB_LOGI(TAG, "Sending test data...");

            fmrb_err_t ret = fmrb_hal_spi_transfer(spi_handle, tx_data, rx_data, sizeof(tx_data), 1000);
            if (ret == FMRB_OK) {
                FMRB_LOGI(TAG, "SPI transfer OK - TX: %02X %02X %02X %02X %02X %02X",
                         tx_data[0], tx_data[1], tx_data[2], tx_data[3], tx_data[4], tx_data[5]);
                FMRB_LOGI(TAG, "                 RX: %02X %02X %02X %02X %02X %02X",
                         rx_data[0], rx_data[1], rx_data[2], rx_data[3], rx_data[4], rx_data[5]);
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
        return -1;
    }

    FMRB_LOGI(TAG, "SPI initialized - MOSI:%d MISO:%d SCLK:%d CS:%d @ %dHz",
             SPI_MOSI_PIN, SPI_MISO_PIN, SPI_SCLK_PIN, SPI_CS_PIN, SPI_FREQUENCY);

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

    fmrb_base_type_t ret = fmrb_task_create_pinned(
        spi_conn_check_task,
        "spi_conn_check",
        4096,
        NULL,
        5,
        &spi_task_handle,
        1  // Core 1
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

    FMRB_LOGI(TAG, "SPI connection check stopped");
}
