#include "fmrb_hal_file.h"
#include "fmrb_pin_assign.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#define TAG "fmrb_hal_file"

// Maximum number of open files/directories
#define MAX_OPEN_FILES 10
#define MAX_OPEN_DIRS 10

#define MAX_PATH_LEN 128

// Internal file handle structure
typedef struct {
    FILE *fp;
    bool in_use;
} fmrb_file_slot_t;

// Internal directory handle structure
typedef struct {
    DIR *dir;
    bool in_use;
} fmrb_dir_slot_t;

// Static file and directory handle pools
EXT_RAM_BSS_ATTR static fmrb_file_slot_t s_file_slots[MAX_OPEN_FILES];
EXT_RAM_BSS_ATTR static fmrb_dir_slot_t s_dir_slots[MAX_OPEN_DIRS];

// Global mutex for thread safety
static SemaphoreHandle_t s_file_mutex = NULL;
#define LOCK() xSemaphoreTake(s_file_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_file_mutex)

// Mount points
#define LITTLEFS_PATH "/flash"
#define SDCARD_PATH "/sd"

// SD card SPI configuration
#define SD_SPI_HOST    SPI3_HOST
#define SD_CS_GPIO     FMRB_PIN_SD_CS
#define SD_MOSI_GPIO   FMRB_PIN_SD_MOSI
#define SD_SCLK_GPIO   FMRB_PIN_SD_SCLK
#define SD_MISO_GPIO   FMRB_PIN_SD_MISO
#define SD_DETECT_GPIO FMRB_PIN_SD_DETECT

// SD card state
static sdmmc_card_t *s_sd_card = NULL;
static bool s_sd_mounted = false;
static bool s_spi_initialized = false;

// Helper function to build full path
// Accepts paths with /flash or /sd prefix, or bare paths (defaults to /flash)
static void build_path(const char *path, char *full_path, size_t max_len) {
    // Check if path already has /flash or /sd prefix
    if (strncmp(path, "/flash/", 7) == 0 || strcmp(path, "/flash") == 0) {
        snprintf(full_path, max_len, "%s", path);
    } else if (strncmp(path, "/sd/", 4) == 0 || strcmp(path, "/sd") == 0) {
        snprintf(full_path, max_len, "%s", path);
    } else if (path[0] == '/') {
        // Absolute path without prefix - default to flash
        snprintf(full_path, max_len, "%s%s", LITTLEFS_PATH, path);
    } else {
        // Relative path - default to flash
        snprintf(full_path, max_len, "%s/%s", LITTLEFS_PATH, path);
    }
}

// Convert flags to POSIX mode string
static void flags_to_mode(uint32_t flags, char *mode) {
    if (flags & FMRB_O_RDWR) {
        if (flags & FMRB_O_CREAT) {
            if (flags & FMRB_O_TRUNC) {
                strcpy(mode, "w+");
            } else {
                strcpy(mode, "r+");
            }
        } else {
            strcpy(mode, "r+");
        }
    } else if (flags & FMRB_O_WRONLY) {
        if (flags & FMRB_O_APPEND) {
            strcpy(mode, "a");
        } else if (flags & FMRB_O_TRUNC) {
            strcpy(mode, "w");
        } else {
            strcpy(mode, "r+");
        }
    } else {
        strcpy(mode, "r");
    }

    // Add binary mode
    strcat(mode, "b");
}

// Find free file slot
static fmrb_file_slot_t* find_free_file_slot(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!s_file_slots[i].in_use) {
            return &s_file_slots[i];
        }
    }
    return NULL;
}

// Find free directory slot
static fmrb_dir_slot_t* find_free_dir_slot(void) {
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (!s_dir_slots[i].in_use) {
            return &s_dir_slots[i];
        }
    }
    return NULL;
}

// Validate file handle
static bool is_valid_file_handle(fmrb_file_t handle) {
    if (handle == NULL) {
        return false;
    }
    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    // Check if pointer is within our static array
    if (slot < s_file_slots || slot >= s_file_slots + MAX_OPEN_FILES) {
        return false;
    }
    return slot->in_use;
}

// Validate directory handle
static bool is_valid_dir_handle(fmrb_dir_t handle) {
    if (handle == NULL) {
        return false;
    }
    fmrb_dir_slot_t *slot = (fmrb_dir_slot_t *)handle;
    // Check if pointer is within our static array
    if (slot < s_dir_slots || slot >= s_dir_slots + MAX_OPEN_DIRS) {
        return false;
    }
    return slot->in_use;
}

// Check if SD card is present
static bool is_sd_card_present(void) {
    return (gpio_get_level(SD_DETECT_GPIO) == 0);  // Active low
}

// Mount SD card
static esp_err_t mount_sd_card(void) {
    if (s_sd_mounted) {
        return ESP_OK;  // Already mounted
    }

    if (!is_sd_card_present()) {
        ESP_LOGW(TAG, "SD card not detected");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret;

    // Initialize SPI bus if not already done
    if (!s_spi_initialized) {
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
            ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            return ret;
        }
        s_spi_initialized = true;
        ESP_LOGI(TAG, "SPI bus initialized for SD card");
    }

    // Configure SDSPI device
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = SD_SPI_HOST;

    // Mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Use SDSPI host
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    ret = esp_vfs_fat_sdspi_mount(SDCARD_PATH, &host, &slot_config, &mount_config, &s_sd_card);

    if (ret == ESP_OK) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_PATH);
        sdmmc_card_print_info(stdout, s_sd_card);
    } else {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
    }

    return ret;
}

// Unmount SD card
static void unmount_sd_card(void) {
    if (s_sd_mounted) {
        esp_vfs_fat_sdcard_unmount(SDCARD_PATH, s_sd_card);
        s_sd_card = NULL;
        s_sd_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }

    if (s_spi_initialized) {
        spi_bus_free(SD_SPI_HOST);
        s_spi_initialized = false;
        ESP_LOGI(TAG, "SPI bus freed");
    }
}

// Initialize file system
fmrb_err_t fmrb_hal_file_init(void) {
    // Initialize file slots
    memset(s_file_slots, 0, sizeof(s_file_slots));
    memset(s_dir_slots, 0, sizeof(s_dir_slots));

    // Create mutex
    s_file_mutex = xSemaphoreCreateMutex();
    if (s_file_mutex == NULL) {
        return FMRB_ERR_NO_MEMORY;
    }

    // Configure SD card detect GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SD_DETECT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Mount LittleFS
    esp_vfs_littlefs_conf_t lfs_conf = {
        .base_path = LITTLEFS_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&lfs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_file_mutex);
        s_file_mutex = NULL;
        return FMRB_ERR_FAILED;
    }
    ESP_LOGI(TAG, "LittleFS mounted at %s", LITTLEFS_PATH);

    // Try to mount SD card (non-fatal if it fails)
    mount_sd_card();

    return FMRB_OK;
}

// Deinitialize file system
void fmrb_hal_file_deinit(void) {
    if (s_file_mutex != NULL) {
        LOCK();

        // Close all open files
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (s_file_slots[i].in_use && s_file_slots[i].fp != NULL) {
                fclose(s_file_slots[i].fp);
                s_file_slots[i].in_use = false;
                s_file_slots[i].fp = NULL;
            }
        }

        // Close all open directories
        for (int i = 0; i < MAX_OPEN_DIRS; i++) {
            if (s_dir_slots[i].in_use && s_dir_slots[i].dir != NULL) {
                closedir(s_dir_slots[i].dir);
                s_dir_slots[i].in_use = false;
                s_dir_slots[i].dir = NULL;
            }
        }

        UNLOCK();

        // Unmount SD card
        unmount_sd_card();

        // Unmount LittleFS
        esp_vfs_littlefs_unregister("storage");

        vSemaphoreDelete(s_file_mutex);
        s_file_mutex = NULL;
    }
}

// Open a file
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    fmrb_file_slot_t *slot = find_free_file_slot();
    if (slot == NULL) {
        UNLOCK();
        return FMRB_ERR_BUSY;  // All slots are in use
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    char mode[8];
    flags_to_mode(flags, mode);

    slot->fp = fopen(full_path, mode);
    if (slot->fp == NULL) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    slot->in_use = true;
    *out_handle = (fmrb_file_t)slot;
    UNLOCK();
    return FMRB_OK;
}

// Close a file
fmrb_err_t fmrb_hal_file_close(fmrb_file_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    fclose(slot->fp);
    slot->fp = NULL;
    slot->in_use = false;

    UNLOCK();
    return FMRB_OK;
}

// Read from file
fmrb_err_t fmrb_hal_file_read(fmrb_file_t handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    size_t n = fread(buffer, 1, size, slot->fp);
    if (bytes_read != NULL) {
        *bytes_read = n;
    }

    UNLOCK();
    return FMRB_OK;
}

// Write to file
fmrb_err_t fmrb_hal_file_write(fmrb_file_t handle, const void *buffer, size_t size, size_t *bytes_written) {
    if (handle == NULL || buffer == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    size_t n = fwrite(buffer, 1, size, slot->fp);
    if (bytes_written != NULL) {
        *bytes_written = n;
    }

    UNLOCK();
    return (n == size) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Seek to position in file
fmrb_err_t fmrb_hal_file_seek(fmrb_file_t handle, int32_t offset, fmrb_seek_mode_t mode) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    int whence;
    switch (mode) {
        case FMRB_SEEK_SET: whence = SEEK_SET; break;
        case FMRB_SEEK_CUR: whence = SEEK_CUR; break;
        case FMRB_SEEK_END: whence = SEEK_END; break;
        default: return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    int ret = fseek(slot->fp, offset, whence);

    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get current position in file
fmrb_err_t fmrb_hal_file_tell(fmrb_file_t handle, uint32_t *position) {
    if (handle == NULL || position == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    long pos = ftell(slot->fp);

    UNLOCK();

    if (pos < 0) {
        return FMRB_ERR_FAILED;
    }

    *position = (uint32_t)pos;
    return FMRB_OK;
}

// Delete a file
fmrb_err_t fmrb_hal_file_remove(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = unlink(full_path);
    UNLOCK();

    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Rename/move a file
fmrb_err_t fmrb_hal_file_rename(const char *old_path, const char *new_path) {
    if (old_path == NULL || new_path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char old_full_path[MAX_PATH_LEN];
    char new_full_path[MAX_PATH_LEN];
    build_path(old_path, old_full_path, sizeof(old_full_path));
    build_path(new_path, new_full_path, sizeof(new_full_path));

    LOCK();
    int ret = rename(old_full_path, new_full_path);
    UNLOCK();

    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get file information
fmrb_err_t fmrb_hal_file_stat(const char *path, fmrb_file_info_t *info) {
    if (path == NULL || info == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    struct stat st;
    int ret = stat(full_path, &st);
    UNLOCK();

    if (ret != 0) {
        return FMRB_ERR_FAILED;
    }

    memset(info, 0, sizeof(fmrb_file_info_t));

    // Extract basename from path
    const char *basename = strrchr(path, '/');
    if (basename != NULL) {
        basename++;
    } else {
        basename = path;
    }
    snprintf(info->name, sizeof(info->name), "%s", basename);

    info->size = st.st_size;
    info->is_dir = S_ISDIR(st.st_mode);
    info->mtime = st.st_mtime;

    return FMRB_OK;
}

// Create a directory
fmrb_err_t fmrb_hal_file_mkdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = mkdir(full_path, 0755);
    UNLOCK();

    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Remove a directory
fmrb_err_t fmrb_hal_file_rmdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = rmdir(full_path);
    UNLOCK();

    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Open a directory for reading
fmrb_err_t fmrb_hal_file_opendir(const char *path, fmrb_dir_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    LOCK();

    fmrb_dir_slot_t *slot = find_free_dir_slot();
    if (slot == NULL) {
        UNLOCK();
        return FMRB_ERR_BUSY;  // All slots are in use
    }

    slot->dir = opendir(full_path);
    if (slot->dir == NULL) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    slot->in_use = true;
    *out_handle = (fmrb_dir_t)slot;

    UNLOCK();
    return FMRB_OK;
}

// Close a directory
fmrb_err_t fmrb_hal_file_closedir(fmrb_dir_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_dir_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_dir_slot_t *slot = (fmrb_dir_slot_t *)handle;
    closedir(slot->dir);
    slot->dir = NULL;
    slot->in_use = false;

    UNLOCK();
    return FMRB_OK;
}

// Read next entry from directory
fmrb_err_t fmrb_hal_file_readdir(fmrb_dir_t handle, fmrb_file_info_t *info) {
    if (handle == NULL || info == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_dir_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_dir_slot_t *slot = (fmrb_dir_slot_t *)handle;
    struct dirent *entry = readdir(slot->dir);

    UNLOCK();

    if (entry == NULL) {
        return FMRB_ERR_NOT_SUPPORTED;  // No more entries
    }

    memset(info, 0, sizeof(fmrb_file_info_t));
    snprintf(info->name, sizeof(info->name), "%s", entry->d_name);
    info->is_dir = (entry->d_type == DT_DIR);
    info->size = 0;
    info->mtime = 0;

    return FMRB_OK;
}

// Flush file buffers to storage
fmrb_err_t fmrb_hal_file_sync(fmrb_file_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    int ret = fflush(slot->fp);

    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get file size
fmrb_err_t fmrb_hal_file_size(fmrb_file_t handle, uint32_t *size) {
    if (handle == NULL || size == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    long current = ftell(slot->fp);
    if (current < 0) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    if (fseek(slot->fp, 0, SEEK_END) != 0) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    long file_size = ftell(slot->fp);
    fseek(slot->fp, current, SEEK_SET);  // Restore position
    UNLOCK();

    if (file_size < 0) {
        return FMRB_ERR_FAILED;
    }

    *size = (uint32_t)file_size;
    return FMRB_OK;
}

// Change current working directory
fmrb_err_t fmrb_hal_file_chdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = chdir(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get current working directory
fmrb_err_t fmrb_hal_file_getcwd(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    char *ret = getcwd(buffer, size);
    UNLOCK();
    return (ret != NULL) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Change file modification time
fmrb_err_t fmrb_hal_file_utime(const char *path, uint32_t mtime) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    struct utimbuf times;
    times.actime = mtime;
    times.modtime = mtime;

    LOCK();
    int ret = utime(full_path, &times);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Change file attributes/permissions
fmrb_err_t fmrb_hal_file_chmod(const char *path, uint32_t attr) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    // On ESP32 VFS, chmod is supported
    LOCK();
    int ret = chmod(full_path, (mode_t)attr);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get filesystem statistics
fmrb_err_t fmrb_hal_file_statfs(const char *path, uint64_t *total_bytes, uint64_t *free_bytes) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    // Use ESP-IDF VFS API
    uint64_t total = 0, used = 0;
    esp_err_t ret = ESP_OK;

    // Determine which filesystem to query
    if (strncmp(full_path, SDCARD_PATH, strlen(SDCARD_PATH)) == 0) {
        // SD card (FAT) - esp_vfs_fat_info uses uint64_t*
        ret = esp_vfs_fat_info(SDCARD_PATH, &total, &used);
    } else {
        // LittleFS - esp_littlefs_info uses size_t*
        size_t total_lfs = 0, used_lfs = 0;
        ret = esp_littlefs_info("storage", &total_lfs, &used_lfs);
        total = total_lfs;
        used = used_lfs;
    }

    if (ret != ESP_OK) {
        return FMRB_ERR_FAILED;
    }

    if (total_bytes != NULL) {
        *total_bytes = total;
    }
    if (free_bytes != NULL) {
        *free_bytes = total - used;
    }

    return FMRB_OK;
}

// Format filesystem
fmrb_err_t fmrb_hal_file_mkfs(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // For ESP32, we can format LittleFS by re-registering with format flag
    if (strncmp(path, LITTLEFS_PATH, strlen(LITTLEFS_PATH)) == 0 ||
        strncmp(path, "/flash", 6) == 0) {
        esp_err_t ret = esp_littlefs_format("storage");
        return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
    }

    // SD card formatting not supported via this API
    return FMRB_ERR_NOT_SUPPORTED;
}

// Get volume label - not fully supported on ESP32
fmrb_err_t fmrb_hal_file_getlabel(const char *path, char *label) {
    (void)path;
    (void)label;
    // ESP-IDF VFS doesn't provide volume label API
    return FMRB_ERR_NOT_SUPPORTED;
}

// Set volume label - not fully supported on ESP32
fmrb_err_t fmrb_hal_file_setlabel(const char *path, const char *label) {
    (void)path;
    (void)label;
    // ESP-IDF VFS doesn't provide volume label API
    return FMRB_ERR_NOT_SUPPORTED;
}

// Get sector size
uint32_t fmrb_hal_file_sector_size(void) {
    // LittleFS block size is typically 4096
    // SD card sector size is typically 512
    // Return a common default
    return 4096;
}

// Get physical address - not supported on ESP32 VFS
fmrb_err_t fmrb_hal_file_physical_address(fmrb_file_t handle, uintptr_t *addr) {
    (void)handle;
    (void)addr;
    // VFS abstraction doesn't expose physical addresses
    // For XIP, use esp_partition_mmap instead
    return FMRB_ERR_NOT_SUPPORTED;
}

// Erase storage volume
fmrb_err_t fmrb_hal_file_erase(const char *volume) {
    if (volume == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Erase LittleFS partition
    if (strcmp(volume, "0:") == 0 || strcmp(volume, "storage") == 0) {
        const esp_partition_t *partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
        if (partition == NULL) {
            return FMRB_ERR_FAILED;
        }
        esp_err_t ret = esp_partition_erase_range(partition, 0, partition->size);
        return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
    }

    return FMRB_ERR_NOT_SUPPORTED;
}

// Check if file is stored contiguously - not supported
fmrb_err_t fmrb_hal_file_is_contiguous(const char *path, bool *is_contiguous) {
    (void)path;
    (void)is_contiguous;
    // VFS abstraction doesn't expose this level of detail
    return FMRB_ERR_NOT_SUPPORTED;
}

// Mount filesystem
fmrb_err_t fmrb_hal_file_mount(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Check if this is SD card mount request
    if (strncmp(path, SDCARD_PATH, strlen(SDCARD_PATH)) == 0 ||
        strncmp(path, "/sd", 3) == 0) {
        esp_err_t ret = mount_sd_card();
        return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
    }

    // LittleFS is auto-mounted in init
    return FMRB_ERR_NOT_SUPPORTED;  // Already mounted
}

// Unmount filesystem
fmrb_err_t fmrb_hal_file_unmount(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Check if this is SD card unmount request
    if (strncmp(path, SDCARD_PATH, strlen(SDCARD_PATH)) == 0 ||
        strncmp(path, "/sd", 3) == 0) {
        unmount_sd_card();
        return FMRB_OK;
    }

    // LittleFS unmounting handled in deinit
    return FMRB_ERR_NOT_SUPPORTED;
}
