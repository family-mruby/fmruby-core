#include "sd_conn_check.h"
#include "fmrb_log.h"
#include "fmrb_pin_assign.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static const char *TAG = "sd_conn_check";

// The boot-time connection check only knows the SPI wiring (Retro). On a
// board whose slot is on the SoC's SDMMC host the real mount in
// fmrb_hal_file_esp32.c is the only bring-up, and running this check would
// drive a SPI bus onto pins that are NC. Compile it out there.
#if !defined(CONFIG_IDF_TARGET_LINUX) && !FMRB_SD_HOST_SDMMC

// SD card SPI configuration
#define SD_SPI_HOST    SPI3_HOST
#define SD_CS_GPIO     FMRB_PIN_SD_CS
#define SD_MOSI_GPIO   FMRB_PIN_SD_MOSI
#define SD_SCLK_GPIO   FMRB_PIN_SD_SCLK
#define SD_MISO_GPIO   FMRB_PIN_SD_MISO

#define SD_MOUNT_POINT "/sdtest"
#define SD_TEST_FILE   "/sdtest/conn_test.tmp"

static sdmmc_card_t *s_card = NULL;
static bool s_spi_initialized = false;
static bool s_mounted = false;

int sd_conn_check_init(void)
{
    esp_err_t ret;

    FMRB_LOGI(TAG, "Initializing SD card connection check...");
    FMRB_LOGI(TAG, "SD pins - CS:%d MOSI:%d MISO:%d SCLK:%d",
             SD_CS_GPIO, SD_MOSI_GPIO, SD_MISO_GPIO, SD_SCLK_GPIO);

    // Initialize SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_GPIO,
        .miso_io_num = SD_MISO_GPIO,
        .sclk_io_num = SD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return -1;
    }
    s_spi_initialized = true;
    FMRB_LOGI(TAG, "SPI bus initialized");

    // Configure SDSPI device
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = SD_SPI_HOST;

    // Mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024
    };

    // Use SDSPI host
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free(SD_SPI_HOST);
        s_spi_initialized = false;
        return -1;
    }
    s_mounted = true;

    FMRB_LOGI(TAG, "SD card mounted successfully");

    // Print card info
    uint64_t card_size_mb = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
    FMRB_LOGI(TAG, "SD Card Info:");
    FMRB_LOGI(TAG, "  Name: %s", s_card->cid.name);
    FMRB_LOGI(TAG, "  Size: %lluMB", card_size_mb);
    FMRB_LOGI(TAG, "  Speed: %s", (s_card->csd.tr_speed > 25000000) ? "High Speed" : "Default Speed");

    return 0;
}

int sd_conn_check_test(void)
{
    // Connection check only - write test skipped
    // Card info was already verified in sd_conn_check_init()
    if (!s_mounted || s_card == NULL) {
        FMRB_LOGE(TAG, "SD card not initialized");
        return -1;
    }

    FMRB_LOGI(TAG, "SD card connection check: PASSED");
    return 0;
}

void sd_conn_check_deinit(void)
{
    if (s_mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
        s_mounted = false;
        FMRB_LOGI(TAG, "SD card unmounted");
    }

    if (s_spi_initialized) {
        spi_bus_free(SD_SPI_HOST);
        s_spi_initialized = false;
        FMRB_LOGI(TAG, "SPI bus freed");
    }
}

#else
// Stubs: Linux, or a board whose slot is on the SDMMC host (mounted for real
// by fmrb_hal_file_init instead).

int sd_conn_check_init(void)
{
    FMRB_LOGI(TAG, "SD connection check not applicable on this target");
    return 0;
}

int sd_conn_check_test(void)
{
    return 0;
}

void sd_conn_check_deinit(void)
{
}

#endif
