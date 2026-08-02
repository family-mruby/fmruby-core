#include <stdint.h>
#include <stdio.h>

// Family mruby modules
#include "fmrb.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_gfx.h"
#include "fmrb_audio.h"
#include "fmrb_toml.h"
#include "fmrb_kernel.h"
#include "host/host_task.h"
#include "fmrb_app.h"
#include "fmrb_task_config.h"
#include "usb_task.h"
#include "hid_device_config.h"
#include "fs_bench.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "hw_proxy.h"
#include "fmrb_hal_pin_manager.h"
#include "spi_conn_check.h"
#include "usb_hid_conn_check.h"
#include "i2c_conn_check.h"
#include "sd_conn_check.h"
#include "fmrb_pin_assign.h"
#include "fmrb_hal_gpio.h"
#include "status_led.h"
// BLE: built-in radio on retro targets; on Modern (ESP32-P4) the controller
// is on the ESP32-C6 coprocessor via esp_hosted (host-only NimBLE).
#include "ble_task.h"
#ifdef FMRB_HW_ATOM_DISPLAY
#include "m5gfx_task.h"
#include "i2c_keyboard.h"
#endif
#ifdef FMRB_HW_MODERN
#include "display_p4_task.h"
#include "tab5_keyboard.h"
#include "touch_task.h"
#include "audio_p4.h"
#include "wifi_task.h"
#include "rd_task.h"
#endif
#endif

#include "boot.h"

#include "fmrb_debugd.h"     // remote-debugger daemon (all targets)

static const char *TAG = "boot";

// Startup synchronization flags
static volatile bool kernel_ready = false;
static volatile bool host_ready = false;

// Getter functions
bool fmrb_kernel_is_ready(void) {
    return kernel_ready;
}

bool fmrb_host_is_ready(void) {
    return host_ready;
}

// Setter functions (called by kernel/host tasks)
void fmrb_kernel_set_ready(void) {
    kernel_ready = true;
    FMRB_LOGI(TAG, "Kernel task ready");
}

void fmrb_host_set_ready(void) {
    host_ready = true;
    FMRB_LOGI(TAG, "Host task ready");
}

extern int Machine_get_config_int(int type);

#ifdef CONFIG_IDF_TARGET_LINUX

#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG_SIG = "signal_check";

void dump_signal_mask(const char* where) {
    sigset_t blocked; sigemptyset(&blocked);
    if (pthread_sigmask(SIG_SETMASK, NULL, &blocked) != 0) {
        ESP_LOGE(TAG_SIG, "%s: pthread_sigmask get failed: %s", where, strerror(errno));
        return;
    }
    int bl = sigismember(&blocked, SIGALRM);
    FMRB_LOGI(TAG_SIG, "%s: SIGALRM blocked=%d", where, bl);
}

void log_itimer_real(const char* where) {
    struct itimerval itv;
    if (getitimer(ITIMER_REAL, &itv) != 0) {
        ESP_LOGE(TAG_SIG, "%s: getitimer failed: %s", where, strerror(errno));
        return;
    }
    FMRB_LOGI(TAG_SIG,
        "%s: ITIMER_REAL: interval=%ld.%06lds, value=%ld.%06lds",
        where,
        (long)itv.it_interval.tv_sec, (long)itv.it_interval.tv_usec,
        (long)itv.it_value.tv_sec,    (long)itv.it_value.tv_usec);
    // 期待値イメージ: interval≈1ms(0.001s), value>0 でカウントダウン中
}

#endif //CONFIG_IDF_TARGET_LINUX

void show_config(void)
{
    FMRB_LOGI(TAG, "------------------------------------------------");
    #ifndef CONFIG_IDF_TARGET_LINUX
    FMRB_LOGI(TAG, "configTICK_RATE_HZ           = %d", configTICK_RATE_HZ);
    FMRB_LOGI(TAG, "configMAX_PRIORITIES         = %d", configMAX_PRIORITIES);
    FMRB_LOGI(TAG, "configMINIMAL_STACK_SIZE     = %d", configMINIMAL_STACK_SIZE);
    #endif
    #ifndef CONFIG_IDF_TARGET_LINUX
    FMRB_LOGI(TAG, "configUSE_PREEMPTION         = %d", configUSE_PREEMPTION);
    FMRB_LOGI(TAG, "configUSE_TIME_SLICING       = %d", configUSE_TIME_SLICING);
    FMRB_LOGI(TAG, "configUSE_MUTEXES            = %d", configUSE_MUTEXES);
    FMRB_LOGI(TAG, "configNUM_THREAD_LOCAL_STORAGE_POINTERS = %d", configNUM_THREAD_LOCAL_STORAGE_POINTERS);
    FMRB_LOGI(TAG, "configCHECK_FOR_STACK_OVERFLOW = %d", configCHECK_FOR_STACK_OVERFLOW);
    FMRB_LOGI(TAG, "configUSE_TRACE_FACILITY     = %d", configUSE_TRACE_FACILITY);
    #endif
    FMRB_LOGI(TAG, "MRB_TICK_UNIT                = %d", Machine_get_config_int(0));
    FMRB_LOGI(TAG, "MRB_TIMESLICE_TICK_COUNT     = %d", Machine_get_config_int(1));
    FMRB_LOGI(TAG, "current tick=%u", (unsigned)fmrb_task_get_tick_count());
    FMRB_LOGI(TAG, "FMRB_MAX_APPS                = %d", FMRB_MAX_APPS);
    FMRB_LOGI(TAG, "FMRB_MAX_USER_APPS           = %d", FMRB_MAX_USER_APPS);

    FMRB_LOGI(TAG, "------------------------------------------------");

    // Display PSRAM information (ESP32 only)
    fmrb_mem_print_psram_info();
}

#ifndef CONFIG_IDF_TARGET_LINUX
// Initialize GPIO pins before peripheral initialization
static void init_gpio(void)
{
    // USB HOST power: keep LOW until USB Host PHY is initialized.
    // Bus-powered hubs draw inrush current; enabling Vbus before the host
    // is ready can cause the upstream connection event to be missed.
    fmrb_hal_gpio_config(FMRB_PIN_USB_POWER, FMRB_GPIO_MODE_OUTPUT, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_level(FMRB_PIN_USB_POWER, 0);
    FMRB_LOGI(TAG, "FMRB_PIN_USB_POWER held LOW (Vbus deferred until USB Host ready)");

    // Set UART data pins to floating (no internal pull-up/down)
    fmrb_hal_gpio_set_pull_mode(FMRB_PIN_GFX_UART_TX, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_pull_mode(FMRB_PIN_GFX_UART_RX, FMRB_GPIO_PULL_NONE);
    // RTS/CTS pins (GPIO12, GPIO10) are configured by UART driver with HW flow control
    FMRB_LOGI(TAG, "UART data pins set to floating");

    // Status LED (green, GPIO configured, task started later)
    fmrb_hal_gpio_config(FMRB_PIN_STATUS_LED, FMRB_GPIO_MODE_OUTPUT, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 1);

    // Error LED (red, shared with MTCK)
    fmrb_hal_gpio_config(FMRB_PIN_ERROR_LED, FMRB_GPIO_MODE_OUTPUT, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_level(FMRB_PIN_ERROR_LED, 0);

    // WROVER-RESET: open-drain HIGH (high-impedance), external pull-up keeps WROVER running
    fmrb_hal_gpio_config(FMRB_PIN_WROVER_RESET, FMRB_GPIO_MODE_OUTPUT_OD, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_level(FMRB_PIN_WROVER_RESET, 1);
}
#endif

#if !defined(CONFIG_IDF_TARGET_LINUX) && defined(ENABLE_HW_WIRING_TEST)
static void hw_check(void)
{
    // // USB HID Host (for keyboard/mouse detection)
    // if (usb_hid_conn_check_init() == 0) {
    //     usb_hid_conn_check_start();
    //     FMRB_LOGI(TAG, "USB HID connection check started");
    // } else {
    //     FMRB_LOGW(TAG, "USB HID init failed, continuing without it");
    // }

    // // SPI connection check (for communication with graphics-audio board)
    // if (spi_conn_check_init() == 0) {
    //     spi_conn_check_start();
    //     FMRB_LOGI(TAG, "SPI connection check task started");
    //     while(1){
    //         FMRB_LOGI(TAG, "SPI connection check task running");
    //         fmrb_task_delay_ms(5000);
    //     }
    // } else {
    //     FMRB_LOGW(TAG, "SPI connection check init failed, continuing without it");
    // }

    // SD connection check
    if (sd_conn_check_init() == 0) {
        sd_conn_check_test();
        sd_conn_check_deinit();
        FMRB_LOGI(TAG, "SD card connection check completed");
    } else {
        FMRB_LOGW(TAG, "SD card connection check init failed");
    }

    // I2C connection check
    if (i2c_conn_check_init() == 0) {
        i2c_conn_check_scan(1);  // Scan I2C1 bus
        i2c_conn_check_scan(2);  // Scan I2C2 bus
        i2c_conn_check_deinit();
        FMRB_LOGI(TAG, "I2C connection check completed");
    } else {
        FMRB_LOGW(TAG, "I2C connection check init failed");
    }

}
#endif

/**
 * Reset ESP32-WROVER via GPIO (open-drain)
 * Open-drain output: LOW to assert reset, HIGH to release (high-impedance).
 * External pull-up resistor brings the reset line HIGH when released.
 */
#ifndef CONFIG_IDF_TARGET_LINUX
static void reset_wrover(void)
{
    FMRB_LOGI(TAG, "Resetting ESP32-WROVER...");
    // Open-drain: drive LOW to assert reset
    fmrb_hal_gpio_config(FMRB_PIN_WROVER_RESET, FMRB_GPIO_MODE_OUTPUT_OD, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_level(FMRB_PIN_WROVER_RESET, 0);
    fmrb_task_delay_ms(100);
    // Release: set HIGH -> open-drain goes high-impedance, external pull-up brings reset HIGH
    fmrb_hal_gpio_set_level(FMRB_PIN_WROVER_RESET, 1);
    FMRB_LOGI(TAG, "Waiting for ESP32-WROVER boot...");
    fmrb_task_delay_ms(3000);
    FMRB_LOGI(TAG, "ESP32-WROVER boot wait done");
}
#endif

#ifdef CONFIG_IDF_TARGET_LINUX
static bool init_hardware(void)
{
    fmrb_err_t ret = fmrb_hal_file_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init filesystem");
        return false;
    }

    // Per-operation filesystem cost, measured before any VM exists so the
    // figures can be compared against the ESP32 build.
    fs_bench_run(FMRB_FS_BENCH_DIR);

    ret = usb_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init usb_task");
        return false;
    }

    return true;
}
#else // ESP32
#ifdef FMRB_HW_MODERN
// One-shot helper for Modern: the C6 coprocessor power rail is switched
// by the display task (tab5_power_on via PI4IO #2), so wait for the
// display before starting the esp_hosted SDIO handshake. BLE goes first
// (its nimble_port_init brings the SDIO transport up, the proven path),
// WiFi strictly after: esp_hosted 1.4.0 corrupts the heap when an RPC is
// issued before the transport is up (see doc/ble_c6_web_console.md).
static void modern_radio_init_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 300 && !display_p4_is_ready(); i++) {
        fmrb_task_delay_ms(10);
    }
    if (!display_p4_is_ready()) {
        FMRB_LOGW(TAG, "Display not ready, skipping BLE/WiFi init");
        fmrb_task_delete_ex(NULL);
        return;
    }
    if (ble_task_init() != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init BLE via C6, continuing without it");
    }
    fmrb_mem_log_boot_snapshot("ble");
    if (wifi_task_init() != FMRB_OK) {
        FMRB_LOGW(TAG, "WiFi not started (disabled or failed)");
    } else if (rd_task_init() != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init remote desktop, continuing without it");
    }
    fmrb_mem_log_boot_snapshot("wifi_rd");
    fmrb_task_delete_ex(NULL);
}
#endif

static bool init_hardware(void)
{
    fmrb_mem_log_boot_snapshot("hw_init_start");
    fmrb_pin_manager_init();
    hw_proxy_init();

    init_gpio();
    fmrb_task_delay_ms(10); // wait for gpio to stabilize

#ifndef FMRB_HW_ATOM_DISPLAY
    status_led_start();
#endif
    fmrb_mem_log_boot_snapshot("gpio_led_proxy");

#ifdef ENABLE_HW_WIRING_TEST
    hw_check();
#endif

    fmrb_err_t ret = fmrb_hal_file_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init filesystem");
        return false;
    }
    fmrb_mem_log_boot_snapshot("littlefs_mount");

    // Per-operation filesystem cost. Runs here, right after the mount and
    // before any VM is created, so the numbers carry no interpreter or GC
    // component and stay comparable across engines and storage settings.
    fs_bench_run(FMRB_FS_BENCH_DIR);
    fmrb_mem_log_boot_snapshot("fs_bench");

#ifndef FMRB_HW_ATOM_DISPLAY
    hid_device_config_init();

    ret = usb_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init usb_task");
        return false;
    }
    // Bus-powered hubs need a long enough Vbus-LOW dwell to fully discharge
    // their internal caps before they will see Vbus HIGH as a clean rising
    // edge. Hold LOW a bit more here on top of the elapsed init time.
    fmrb_task_delay_ms(150);
    fmrb_hal_gpio_set_level(FMRB_PIN_USB_POWER, 1);
    FMRB_LOGI(TAG, "FMRB_PIN_USB_POWER set to HIGH (post-USB-init)");
    // Let the hub power-up and PHY signaling settle before the host stack
    // starts driving the bus, then enable the root port to trigger
    // enumeration with the host already listening.
    fmrb_task_delay_ms(200);
    if (usb_task_power_on_root_port() != FMRB_OK) {
        FMRB_LOGW(TAG, "USB root port power-on failed; enumeration may not start");
    }
    fmrb_mem_log_boot_snapshot("usb_host");
#endif

    // Retro BLE (built-in radio) is no longer started here: the decision needs
    // system_conf.toml (ble_auto_start), which the kernel reads. See
    // fmrb_os_init below. Modern (P4) keeps its own path: BLE via the C6
    // coprocessor is tied to the SDIO transport bring-up order.

#if defined(FMRB_HW_ATOM_DISPLAY)
    ret = m5gfx_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init M5GFX, continuing without it");
    }

    ret = i2c_keyboard_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init I2C keyboard, continuing without it");
    }
#elif defined(FMRB_HW_MODERN)
    // Modern (ESP32-P4 / Tab5): local display task drives the MIPI-DSI panel;
    // the Tab5 Keyboard accessory provides key input. No WROVER child chip.
    ret = display_p4_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init P4 display, continuing without it");
    }

    ret = tab5_keyboard_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init Tab5 keyboard, continuing without it");
    }

    ret = touch_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init touch, continuing without it");
    }

    // Local APU audio engine (codec hw is brought up by the display task)
    ret = audio_p4_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init Tab5 audio, continuing without it");
    }

    // BLE + WiFi via the ESP32-C6 coprocessor: run in a one-shot helper
    // task so the SDIO handshake / RPC timeouts never delay boot.
    if (fmrb_task_create(modern_radio_init_task, "radio_init", 6144, NULL,
                         4, NULL) != FMRB_PASS) {
        FMRB_LOGW(TAG, "Failed to spawn radio init task");
    }
#else
    reset_wrover();
#endif

    return true;
}
#endif // CONFIG_IDF_TARGET_LINUX

static void init_mem(void)
{
    fmrb_mem_init();

    // Initialize log ring buffer (must be before any FMRB_LOG calls that need buffering)
    void *log_buf = fmrb_get_mempool_ptr(POOL_ID_LOG_BUFFER);
    size_t log_size = fmrb_get_mempool_size(POOL_ID_LOG_BUFFER);
    if (log_buf && log_size > 0) {
        fmrb_log_buffer_init(log_buf, log_size);
    }

    fmrb_mempool_print_ranges();
    fmrb_toml_init();
}

// Family mruby OS initialization
void fmrb_os_init(void)
{
    //set log level
    fmrb_set_log_level_info();
    //fmrb_set_log_level_debug();

    FMRB_LOGI(TAG, "Family mruby OS version %s",FMRB_OS_VERSION);
    FMRB_LOGI(TAG, "Family mruby Core Firmware Starting...");
    FMRB_LOGD(TAG, "Debug log level enabled");
#ifdef CONFIG_IDF_TARGET_LINUX
    FMRB_LOGI(TAG, "Running on Linux target - Development mode");
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
    FMRB_LOGI(TAG, "Running on ESP32-P4 (M5Stack Tab5) - Production mode");
#else
    FMRB_LOGI(TAG, "Running on ESP32-S3-N16R8 - Production mode");
#endif

    show_config();

#ifdef CONFIG_IDF_TARGET_LINUX
    //install_sigalrm_logger();
    dump_signal_mask("app_main(before)");
    log_itimer_real("app_main(before)");
#endif

    FMRB_LOGI(TAG, "Initializing Family mruby OS...");
    fmrb_mem_log_boot_snapshot("boot_start");
    // Init memory
    init_mem();
    fmrb_mem_log_boot_snapshot("mem_init");

    // Init HW
    init_hardware();

    //Start Frmb Kernel
    fmrb_err_t result = fmrb_kernel_start();
    if(result != FMRB_OK){
        FMRB_LOGE(TAG, "Failed to start kernel");
#ifndef CONFIG_IDF_TARGET_LINUX
        status_led_set_error(FMRB_LED_STATUS_FATAL);
#endif
        return;
    }
    FMRB_LOGI(TAG, "fmrb_kernel_start done");

    // Wait for kernel and host initialization
    int timeout_ms = 5000;  // 5 second timeout
    int elapsed_ms = 0;
    while ((!fmrb_kernel_is_ready()) && elapsed_ms < timeout_ms) {
        FMRB_LOGI(TAG, "Waiting for kernel to be ready...");
        fmrb_task_delay_ms(100);
        elapsed_ms += 100;
    }

    if (!fmrb_kernel_is_ready()) {
        FMRB_LOGE(TAG, "Kernel task initialization timeout");
        return;
    }
    FMRB_LOGI(TAG, "Family mruby OS initialization complete");

#if !defined(CONFIG_IDF_TARGET_LINUX) && !defined(CONFIG_IDF_TARGET_ESP32P4)
    // Retro BLE boot policy, decided by system_conf.toml (readable only now
    // that the kernel loaded it). Auto-start preserves the old behavior;
    // opting out leaves BLE's ~75 KB of internal RAM free until the desktop
    // menu calls ble_service_start() (doc/internal_ram_budget.md, D axis).
    if (fmrb_kernel_get_config()->ble_auto_start) {
        if (ble_service_start() != FMRB_OK) {
            FMRB_LOGW(TAG, "Failed to start BLE, continuing without it");
        }
    } else {
        FMRB_LOGI(TAG, "BLE auto-start disabled (ble_auto_start=false); "
                       "start it from the desktop menu when needed");
    }
#endif

    // Remote debugger daemon (doc/remote_debug/). Linux talks TCP on
    // FMRB_DEBUG_TCP_PORT, ESP32 targets talk the BLE debug GATT service.
    // Started after the kernel/app subsystems are up. The BLE transport does
    // not depend on BLE being initialized yet, so the order is free here.
    fmrb_debugd_init();
    fmrb_mem_log_boot_snapshot("debugd");
}

void fmrb_os_close(void)
{
    fmrb_hal_file_deinit();
}
