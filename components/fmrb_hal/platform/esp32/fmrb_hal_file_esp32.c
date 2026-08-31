#include "fmrb_hal_file.h"
#include "fmrb_tmpfs.h"
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
#if FMRB_SD_HOST_SDMMC
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <errno.h>
#include "hw_proxy.h"
#include "hw_proxy_internal.h"
#include "fmrb_log.h"

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
    // Full path used at opendir() time. Needed by readdir() to call stat()
    // on each entry and report its size/mtime. POSIX readdir() only returns
    // d_name and d_type, so size/mtime require a separate stat() call which
    // needs the entry's full path.
    char path[MAX_PATH_LEN];
} fmrb_dir_slot_t;

// Static file and directory handle pools
EXT_RAM_BSS_ATTR static fmrb_file_slot_t s_file_slots[MAX_OPEN_FILES];
EXT_RAM_BSS_ATTR static fmrb_dir_slot_t s_dir_slots[MAX_OPEN_DIRS];

// Internal-RAM bounce buffer for writes whose source resides in PSRAM. SPI
// flash writes require the source to be in internal RAM; otherwise the write
// silently fails. Sized to balance syscall count against IRAM footprint.
// Access is serialized by s_file_mutex.
#define FILE_WRITE_BOUNCE_SIZE 4096
static uint8_t s_file_write_bounce[FILE_WRITE_BOUNCE_SIZE];

// Global mutex for thread safety
static SemaphoreHandle_t s_file_mutex = NULL;
#define LOCK() xSemaphoreTake(s_file_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_file_mutex)

// Mount points
#define LITTLEFS_PATH "/flash"
#define SDCARD_PATH "/sd"

// SD card wiring. Two shapes exist:
//   - Retro: the slot hangs off a SPI bus (SDSPI), with a card-detect line.
//   - Modern: the slot is on the SoC's own SDMMC host, no card-detect line.
// FMRB_SD_HOST_SDMMC (fmrb_pin_assign.h) picks which bring-up runs; the rest
// of the file only sees mount_sd_card() / unmount_sd_card().
#define SD_SPI_HOST    SPI3_HOST
#define SD_CS_GPIO     FMRB_PIN_SD_CS
#define SD_MOSI_GPIO   FMRB_PIN_SD_MOSI
#define SD_SCLK_GPIO   FMRB_PIN_SD_SCLK
#define SD_MISO_GPIO   FMRB_PIN_SD_MISO
#define SD_DETECT_GPIO FMRB_PIN_SD_DETECT

#ifndef FMRB_SD_HOST_SDMMC
#define FMRB_SD_HOST_SDMMC 0
#endif

// SD card state
static sdmmc_card_t *s_sd_card = NULL;
static bool s_sd_mounted = false;
#if FMRB_SD_HOST_SDMMC
static sd_pwr_ctrl_handle_t s_sd_pwr_ctrl = NULL;
#else
static bool s_spi_initialized = false;
#endif

// Path-prefix alias table. Each entry maps a user-visible prefix to the
// underlying VFS mount point. An entry whose `mount` equals its `prefix` is
// the canonical name; other entries are aliases (the prefix is rewritten to
// `mount` before reaching the VFS). To add a new alias, add one row here -
// no other code needs to change.
typedef struct {
    const char *prefix;  // user-visible prefix, no trailing slash
    const char *mount;   // actual VFS mount point, no trailing slash
} path_alias_t;

static const path_alias_t s_path_aliases[] = {
    { "/flash",  LITTLEFS_PATH },     // canonical flash mount
    { "/sd",     SDCARD_PATH },       // canonical SD mount
    { "/mnt/sd", SDCARD_PATH },       // Unix-style alias for SD
    // RAM filesystem, registered as its own VFS (fmrb_tmpfs_esp32.c). Listed
    // here so it is left alone instead of being folded under the flash mount.
    { FMRB_TMPFS_MOUNT, FMRB_TMPFS_MOUNT },
};

#define PATH_ALIAS_COUNT (sizeof(s_path_aliases) / sizeof(s_path_aliases[0]))

// Virtual mount-point directories that the underlying VFS does not own. They
// only exist as "containers" so the real mounts (/mnt/sd) are reachable via a
// Unix-style namespace. opendir/readdir on these returns the synthetic child
// list; stat reports them as directories.
typedef struct {
    const char *path;            // virtual directory path, no trailing slash
    const char *const *children; // null-terminated? no - count is explicit
    size_t count;
} path_virtual_t;

static const char *const s_root_extra[]    = { "mnt", "tmp" };
static const char *const s_mnt_children[]  = { "sd" };

static const path_virtual_t s_virtual_dirs[] = {
    { "/",    s_root_extra,   2 },  // "mnt" and "tmp" appear in addition to flash entries
    { "/mnt", s_mnt_children, 1 },  // purely virtual: only contains "sd"
};

#define PATH_VIRTUAL_COUNT (sizeof(s_virtual_dirs) / sizeof(s_virtual_dirs[0]))

// Resolve a virtual path to the underlying VFS path.
// Order of resolution:
//   1. Match against the alias table (prefix must terminate at '/' or '\0').
//   2. Otherwise, treat as living under the flash mount (legacy behavior:
//      both absolute and relative bare paths land under LITTLEFS_PATH).
fmrb_err_t fmrb_hal_file_resolve_path(const char *virtual_path,
                                      char *out, size_t out_len) {
    if (virtual_path == NULL || out == NULL || out_len == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    for (size_t i = 0; i < PATH_ALIAS_COUNT; i++) {
        const char *prefix = s_path_aliases[i].prefix;
        size_t plen = strlen(prefix);
        if (strncmp(virtual_path, prefix, plen) == 0 &&
            (virtual_path[plen] == '\0' || virtual_path[plen] == '/')) {
            snprintf(out, out_len, "%s%s",
                     s_path_aliases[i].mount, virtual_path + plen);
            return FMRB_OK;
        }
    }
    if (virtual_path[0] == '/') {
        snprintf(out, out_len, "%s%s", LITTLEFS_PATH, virtual_path);
    } else {
        snprintf(out, out_len, "%s/%s", LITTLEFS_PATH, virtual_path);
    }
    return FMRB_OK;
}

// Internal alias for legacy callsites in this file.
static inline void build_path(const char *path, char *full_path, size_t max_len) {
    fmrb_hal_file_resolve_path(path, full_path, max_len);
}

// Look up the synthetic children for a virtual mount-point directory.
// Returns NULL if `virtual_path` is not a known virtual directory.
const char *const *fmrb_hal_file_virtual_children(const char *virtual_path,
                                                  size_t *out_count) {
    if (virtual_path == NULL) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    for (size_t i = 0; i < PATH_VIRTUAL_COUNT; i++) {
        if (strcmp(virtual_path, s_virtual_dirs[i].path) == 0) {
            if (out_count) *out_count = s_virtual_dirs[i].count;
            return s_virtual_dirs[i].children;
        }
    }
    if (out_count) *out_count = 0;
    return NULL;
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

// Check if handle is a standard stream
static bool is_std_stream(fmrb_file_t handle) {
    return (handle == FMRB_STDIN_HANDLE ||
            handle == FMRB_STDOUT_HANDLE ||
            handle == FMRB_STDERR_HANDLE);
}

// Get FILE* for standard stream
static FILE* get_std_stream_fp(fmrb_file_t handle) {
    if (handle == FMRB_STDIN_HANDLE) return stdin;
    if (handle == FMRB_STDOUT_HANDLE) return stdout;
    if (handle == FMRB_STDERR_HANDLE) return stderr;
    return NULL;
}

// Validate file handle
static bool is_valid_file_handle(fmrb_file_t handle) {
    if (handle == NULL) {
        return false;
    }
    // Standard streams are always valid
    if (is_std_stream(handle)) {
        return true;
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

// Check if SD card is present. Boards without a card-detect line report
// "present" and let the mount itself decide -- an empty slot just fails to
// mount, which is already a non-fatal path.
static bool is_sd_card_present(void) {
    if (SD_DETECT_GPIO == GPIO_NUM_NC) {
        return true;
    }
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

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

#if FMRB_SD_HOST_SDMMC
    // SDMMC host (Modern). Two board facts drive this:
    //   - the slot's IO voltage comes from the on-chip LDO, channel 4. Without
    //     powering it the card never answers.
    //   - slot 0 has no card-detect / write-protect line.
    // Try 4-bit first; a card that will not train all four lines still works
    // at 1-bit, which is far more bandwidth than the filesystem asks for.
    if (!s_sd_pwr_ctrl) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = 4,
        };
        ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_sd_pwr_ctrl);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SD LDO power control failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.pwr_ctrl_handle = s_sd_pwr_ctrl;

    sdmmc_slot_config_t slot_config = {0};
    slot_config.cd  = SDMMC_SLOT_NO_CD;
    slot_config.wp  = SDMMC_SLOT_NO_WP;
    slot_config.clk = FMRB_PIN_SD_CLK;
    slot_config.cmd = FMRB_PIN_SD_CMD;
    slot_config.d0  = FMRB_PIN_SD_D0;
    slot_config.d1  = FMRB_PIN_SD_D1;
    slot_config.d2  = FMRB_PIN_SD_D2;
    slot_config.d3  = FMRB_PIN_SD_D3;
    slot_config.width = 4;
    slot_config.flags = 0;

    ret = esp_vfs_fat_sdmmc_mount(SDCARD_PATH, &host, &slot_config,
                                  &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD 4-bit mount failed (%s), retrying at 1-bit",
                 esp_err_to_name(ret));
        slot_config.width = 1;
        ret = esp_vfs_fat_sdmmc_mount(SDCARD_PATH, &host, &slot_config,
                                      &mount_config, &s_sd_card);
    }

    if (ret == ESP_OK) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "SD card mounted at %s (SDMMC %d-bit)",
                 SDCARD_PATH, slot_config.width);
        sdmmc_card_print_info(stdout, s_sd_card);
    } else {
        ESP_LOGW(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
    }
    return ret;
#else
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
#endif  // FMRB_SD_HOST_SDMMC
}

// Unmount SD card
static void unmount_sd_card(void) {
    if (s_sd_mounted) {
        esp_vfs_fat_sdcard_unmount(SDCARD_PATH, s_sd_card);
        s_sd_card = NULL;
        s_sd_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }

#if FMRB_SD_HOST_SDMMC
    if (s_sd_pwr_ctrl) {
        sd_pwr_ctrl_del_on_chip_ldo(s_sd_pwr_ctrl);
        s_sd_pwr_ctrl = NULL;
    }
#else
    if (s_spi_initialized) {
        spi_bus_free(SD_SPI_HOST);
        s_spi_initialized = false;
        ESP_LOGI(TAG, "SPI bus freed");
    }
#endif
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

    // Configure SD card detect GPIO. Boards without the line skip it; the
    // & 63 keeps the shift well-defined for the compiler on those boards,
    // where SD_DETECT_GPIO is the negative "not connected" value.
    const int detect_pin = SD_DETECT_GPIO;
    if (detect_pin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << (detect_pin & 63)),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }

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

    // /home is the user's own directory and ships empty, so the storage image
    // does not carry it. Make it here rather than at each writer: a user who
    // deletes it, or an app_only flash onto an older image, gets it back.
    if (mkdir(LITTLEFS_PATH "/home", 0755) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "cannot create %s/home", LITTLEFS_PATH);
    }

    // RAM filesystem at /tmp (non-fatal if it fails: only /tmp users notice)
    fmrb_tmpfs_init();

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
// PSRAM stack tasks are routed through hw_proxy (internal RAM stack) to avoid
// SPI flash DMA crashes. Internal RAM stack tasks call the impl directly.
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (hw_proxy_needs_proxy()) {
        hw_proxy_file_open_params_t p = { .path = path, .flags = flags, .out_handle = out_handle };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_OPEN, .params = &p };
        return hw_proxy_call(&req);
    }

    char full_path[MAX_PATH_LEN];
    build_path(path, full_path, sizeof(full_path));

    LOCK();

    fmrb_file_slot_t *slot = find_free_file_slot();
    if (slot == NULL) {
        UNLOCK();
        return FMRB_ERR_BUSY;
    }

    char mode[8];
    flags_to_mode(flags, mode);

    slot->fp = fopen(full_path, mode);
    if (slot->fp == NULL) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    // Disable libc stdio buffering, but only when the file can be written.
    // The newlib FILE buffer can be allocated in PSRAM by the default heap,
    // and SPI flash writes from a PSRAM source silently fail. With _IONBF
    // every write goes straight to the VFS layer, so the source buffer we pass
    // in (bounced to internal RAM in fmrb_hal_file_write) is what reaches the
    // flash driver.
    //
    // Reads carry no such constraint, and _IONBF is ruinous for them: newlib
    // refills an unbuffered stream one byte at a time, so a single fread costs
    // one VFS round trip per byte. Measured on LittleFS at 20.4 us/byte, i.e.
    // 290 ms to read a 14 KB app script. Read-only opens therefore keep
    // newlib's default buffering.
    if (flags & (FMRB_O_WRONLY | FMRB_O_RDWR | FMRB_O_CREAT |
                 FMRB_O_TRUNC | FMRB_O_APPEND)) {
        setvbuf(slot->fp, NULL, _IONBF, 0);
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

    // Standard streams cannot be closed
    if (is_std_stream(handle)) {
        return FMRB_OK;
    }

    if (hw_proxy_needs_proxy()) {
        hw_proxy_file_close_params_t p = { .handle = handle };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_CLOSE, .params = &p };
        return hw_proxy_call(&req);
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

    if (!is_std_stream(handle) && hw_proxy_needs_proxy()) {
        hw_proxy_file_read_params_t p = { .handle = handle, .buf = buffer, .size = size, .out_read = bytes_read };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_READ, .params = &p };
        return hw_proxy_call(&req);
    }

    // Handle standard streams directly
    if (is_std_stream(handle)) {
        FILE *fp = get_std_stream_fp(handle);
        if (fp == NULL) {
            return FMRB_ERR_INVALID_PARAM;
        }
        size_t n = fread(buffer, 1, size, fp);
        if (bytes_read != NULL) {
            *bytes_read = n;
        }
        return FMRB_OK;
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

    if (!is_std_stream(handle) && hw_proxy_needs_proxy()) {
        hw_proxy_file_write_params_t p = { .handle = handle, .buf = buffer, .size = size, .out_written = bytes_written };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_WRITE, .params = &p };
        return hw_proxy_call(&req);
    }

    // Handle standard streams directly
    if (is_std_stream(handle)) {
        FILE *fp = get_std_stream_fp(handle);
        if (fp == NULL) {
            return FMRB_ERR_INVALID_PARAM;
        }
        size_t n = fwrite(buffer, 1, size, fp);
        fflush(fp);  // Flush immediately for standard streams
        if (bytes_written != NULL) {
            *bytes_written = n;
        }
        return (n == size) ? FMRB_OK : FMRB_ERR_FAILED;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;

    // SPI flash writes require the source buffer to live in internal RAM.
    // Callers may pass a PSRAM-resident buffer (Ruby strings, mempool data),
    // and writing directly from PSRAM silently fails (no error, 0 bytes
    // persisted). Bounce through the file-scope internal-RAM buffer in that
    // case. Single-threaded by s_file_mutex, so no concurrency on the buffer.
    uintptr_t addr = (uintptr_t)buffer;
    bool needs_bounce = (size > 0 && addr >= 0x3C000000 && addr < 0x3E000000);

    size_t n;
    if (needs_bounce) {
        const uint8_t *src_bytes = (const uint8_t *)buffer;
        size_t total = 0;
        while (total < size) {
            size_t chunk = size - total;
            if (chunk > FILE_WRITE_BOUNCE_SIZE) chunk = FILE_WRITE_BOUNCE_SIZE;
            memcpy(s_file_write_bounce, src_bytes + total, chunk);
            size_t wrote = fwrite(s_file_write_bounce, 1, chunk, slot->fp);
            total += wrote;
            if (wrote != chunk) break;
        }
        n = total;
    } else {
        n = fwrite(buffer, 1, size, slot->fp);
    }

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

    if (hw_proxy_needs_proxy()) {
        hw_proxy_file_seek_params_t p = { .handle = handle, .offset = offset, .whence = (uint32_t)mode };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_SEEK, .params = &p };
        return hw_proxy_call(&req);
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

    if (hw_proxy_needs_proxy()) {
        hw_proxy_file_tell_params_t p = { .handle = handle, .position = position };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_TELL, .params = &p };
        return hw_proxy_call(&req);
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

    // Virtual mount-point parents have no on-disk entry. Report them as
    // directories so File.exist? / File.directory? answer consistently with
    // Dir.open behavior.
    if (fmrb_hal_file_virtual_children(path, NULL) != NULL) {
        memset(info, 0, sizeof(fmrb_file_info_t));
        const char *basename = strrchr(path, '/');
        basename = (basename && basename[1]) ? basename + 1 : path;
        snprintf(info->name, sizeof(info->name), "%s", basename);
        info->mode = FMRB_S_IFDIR;
        info->is_dir = true;
        return FMRB_OK;
    }

    if (hw_proxy_needs_proxy()) {
        hw_proxy_file_stat_params_t p = { .path = path, .info = info };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_STAT, .params = &p };
        return hw_proxy_call(&req);
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

    info->mode = st.st_mode;
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
    // Remember full path so readdir() can stat() each entry by full path.
    snprintf(slot->path, sizeof(slot->path), "%s", full_path);
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
    // Snapshot the dir's stored path before releasing the lock so we can
    // stat() the entry below without holding the file mutex.
    char dir_path[MAX_PATH_LEN];
    snprintf(dir_path, sizeof(dir_path), "%s", slot->path);

    UNLOCK();

    if (entry == NULL) {
        return FMRB_ERR_NOT_SUPPORTED;  // No more entries
    }

    memset(info, 0, sizeof(fmrb_file_info_t));
    snprintf(info->name, sizeof(info->name), "%s", entry->d_name);
    info->is_dir = (entry->d_type == DT_DIR);
    info->size = 0;
    info->mtime = 0;

    // POSIX readdir() does not return size/mtime; stat() the entry here
    // so callers (BLE LS, picoruby Dir.read) get real values.
    char entry_path[MAX_PATH_LEN + 1 + sizeof(info->name)];
    if (strcmp(dir_path, "/") == 0) {
        snprintf(entry_path, sizeof(entry_path), "/%s", entry->d_name);
    } else {
        snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, entry->d_name);
    }
    struct stat st;
    if (stat(entry_path, &st) == 0) {
        info->size = (uint32_t)st.st_size;
        info->mtime = (uint32_t)st.st_mtime;
    }

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

    if (hw_proxy_needs_proxy()) {
        hw_proxy_file_size_params_t p = { .handle = handle, .size = size };
        hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_SIZE, .params = &p };
        return hw_proxy_call(&req);
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
