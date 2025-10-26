#include <stdint.h>
#include <stdio.h>

// Family mruby modules
#include "fmrb.h"
#include "fmrb_hal.h"
#include "fmrb_gfx.h"
#include "fmrb_audio.h"
#include "fmrb_toml.h"
#include "kernel/fmrb_kernel.h"
#include "kernel/host/host_task.h"
#include "fmrb_app.h"
#include "fmrb_task_config.h"
#include "fs_proxy_task.h"

#include "boot.h"

static const char *TAG = "boot";

// Generated from system_gui.app.rb (will be compiled by picorbc)
extern const uint8_t system_gui_irep[];

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
    FMRB_LOGI(TAG, "configTICK_RATE_HZ           = %d", configTICK_RATE_HZ);
    FMRB_LOGI(TAG, "configMAX_PRIORITIES         = %d", configMAX_PRIORITIES);
    FMRB_LOGI(TAG, "configMINIMAL_STACK_SIZE     = %d", configMINIMAL_STACK_SIZE);
    #ifdef CONFIG_IDF_TARGET_LINUX
    FMRB_LOGI(TAG, "configTOTAL_HEAP_SIZE        = %d", configTOTAL_HEAP_SIZE);
    #endif
    FMRB_LOGI(TAG, "configUSE_PREEMPTION         = %d", configUSE_PREEMPTION);
    FMRB_LOGI(TAG, "configUSE_TIME_SLICING       = %d", configUSE_TIME_SLICING);
    FMRB_LOGI(TAG, "configUSE_MUTEXES            = %d", configUSE_MUTEXES);
    FMRB_LOGI(TAG, "configNUM_THREAD_LOCAL_STORAGE_POINTERS = %d", configNUM_THREAD_LOCAL_STORAGE_POINTERS);
    FMRB_LOGI(TAG, "configCHECK_FOR_STACK_OVERFLOW = %d", configCHECK_FOR_STACK_OVERFLOW);
    FMRB_LOGI(TAG, "configUSE_TRACE_FACILITY     = %d", configUSE_TRACE_FACILITY);
    FMRB_LOGI(TAG, "MRB_TICK_UNIT                = %d", Machine_get_config_int(0));
    FMRB_LOGI(TAG, "MRB_TIMESLICE_TICK_COUNT     = %d", Machine_get_config_int(1));
    FMRB_LOGI(TAG, "current tick=%u", (unsigned)fmrb_task_get_tick_count());
    FMRB_LOGI(TAG, "------------------------------------------------");
}

static void create_system_app(void)
{
    FMRB_LOGI(TAG, "Creating system GUI app...");

    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_SYSTEM_APP,
        .type = APP_TYPE_SYSTEM_APP,
        .name = "system_gui",
        .irep = system_gui_irep,
        .stack_words = FMRB_SYSTEM_APP_TASK_STACK_SIZE,
        .priority = FMRB_SYSTEM_APP_TASK_PRIORITY,
        .core_affinity = -1,  // No core affinity
        .event_queue_len = 10 // Event queue for GUI events
    };

    int32_t app_id;
    bool result = false;
    #if 1
    fmrb_app_init();
    result = fmrb_app_spawn(&attr, &app_id);
    #else
    FMRB_LOGI(TAG, "simple spawn");
    result = fmrb_app_spawn_simple(&attr, &app_id);
    #endif

    if (!result) {
        FMRB_LOGE(TAG, "Failed to spawn system GUI app");
        return;
    }

    FMRB_LOGI(TAG, "System GUI app spawned successfully (id=%ld)", app_id);
}

static void init_hardware(void)
{
    // Filesystem
    fmrb_err_t ret = fmrb_hal_file_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to init filesystem");
        return;
    }
    // ESP32 IPC
    
    // USB HOST

}

static void init_mem(void)
{
    fmrb_init_system_mem();
    fmrb_toml_init();
}

static bool boot_mode_check(void){
    //TODO check GPIO condition

    // if File transer mode
    if(false){
        fmrb_disable_log();
        // disable all log esp_log_level_set("*", ESP_LOG_NONE);
        fmrb_init_system_mem();
        // Serial FS proxy
        fs_proxy_create_task();
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

    FMRB_LOGI(TAG, "Family mruby OS version %s",FMRB_OS_VERSION);
    FMRB_LOGI(TAG, "Family mruby Core Firmware Starting...");
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
        return;
    }
    FMRB_LOGI(TAG, "fmrb_kernel_start done");

    //TODO wait for kernel startup

    //start GUI
    create_system_app();

    FMRB_LOGI(TAG, "Family mruby OS initialization complete");
}

void fmrb_os_close(void)
{
    fmrb_hal_file_deinit();
}
