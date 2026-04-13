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
#include "fs_proxy_task.h"
#include "usb_task.h"
#include "hid_device_config.h"
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
#include "ble_task.h"
#include "rtc_task.h"
#ifdef FMRB_HW_ATOM_DISPLAY
#include "m5gfx_task.h"
#include "i2c_keyboard.h"
#endif
#endif

#include "boot.h"

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
    // USB HOST power
    fmrb_hal_gpio_config(FMRB_PIN_USB_POWER, FMRB_GPIO_MODE_OUTPUT, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_level(FMRB_PIN_USB_POWER, 1);
    FMRB_LOGI(TAG, "FMRB_PIN_USB_POWER set to HIGH");

    // Set UART data pins to floating (no internal pull-up/down)
    fmrb_hal_gpio_set_pull_mode(FMRB_PIN_GFX_UART_TX, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_pull_mode(FMRB_PIN_GFX_UART_RX, FMRB_GPIO_PULL_NONE);
    // RTS/CTS pins (GPIO12, GPIO10) are configured by UART driver with HW flow control
    FMRB_LOGI(TAG, "UART data pins set to floating");

    // Status LED (GPIO configured, task started later)
    fmrb_hal_gpio_config(FMRB_PIN_STATUS_LED, FMRB_GPIO_MODE_OUTPUT, FMRB_GPIO_PULL_NONE);
    fmrb_hal_gpio_set_level(FMRB_PIN_STATUS_LED, 1);

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

    ret = usb_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init usb_task");
        return false;
    }

    return true;
}
#else // ESP32
static bool init_hardware(void)
{
    fmrb_pin_manager_init();
    hw_proxy_init();

    init_gpio();
    fmrb_task_delay_ms(10); // wait for gpio to stabilize

#ifndef FMRB_HW_ATOM_DISPLAY
    status_led_start();
#endif

#ifdef ENABLE_HW_WIRING_TEST
    hw_check();
#endif

    fmrb_err_t ret = fmrb_hal_file_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init filesystem");
        return false;
    }

#ifndef FMRB_HW_ATOM_DISPLAY
    hid_device_config_init();

    ret = usb_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init usb_task");
        return false;
    }
#endif

    ret = ble_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init BLE, continuing without it");
    }

#ifdef FMRB_HW_ATOM_DISPLAY
    ret = m5gfx_task_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init M5GFX, continuing without it");
    }

    ret = i2c_keyboard_init();
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to init I2C keyboard, continuing without it");
    }
#else
    reset_wrover();
    rtc_task_start();
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

static bool boot_mode_check(void){
    //TODO check GPIO condition

    // if File transer mode
    if(false){
        // disable all log
        fmrb_disable_log();
        // minimum init for FS proxy
        fmrb_mem_init();
        fmrb_hal_file_init();
        // Serial FS proxy
        //fs_proxy_create_task();
        return true;
    }
    return false;
}

// Family mruby OS initialization
void fmrb_os_init(void)
{
    if( boot_mode_check() )
    {
        return;
    }

    //set log level
    fmrb_set_log_level_info();
    //fmrb_set_log_level_debug();

    FMRB_LOGI(TAG, "Family mruby OS version %s",FMRB_OS_VERSION);
    FMRB_LOGI(TAG, "Family mruby Core Firmware Starting...");
    FMRB_LOGD(TAG, "Debug log level enabled");
#ifdef CONFIG_IDF_TARGET_LINUX
    FMRB_LOGI(TAG, "Running on Linux target - Development mode");
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
    // Init memory
    init_mem();

    // Init HW
    init_hardware();

    //Start Frmb Kernel
    fmrb_err_t result = fmrb_kernel_start();
    if(result != FMRB_OK){
        FMRB_LOGE(TAG, "Failed to start kernel");
#ifndef CONFIG_IDF_TARGET_LINUX
        status_led_set_error(1);
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
}

void fmrb_os_close(void)
{
    fmrb_hal_file_deinit();
}
