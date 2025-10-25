#include <stdio.h>
#include <stdint.h>
#include "fmrb_hal.h"

#include "boot.h"

static const char *TAG = "app_main";

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

extern int Machine_get_config_int(int type);

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
    FMRB_LOGI(TAG, "current tick=%u", (unsigned)xTaskGetTickCount());
    FMRB_LOGI(TAG, "------------------------------------------------");
}

void app_main(void)
{
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

    // Initialize Family mruby OS
    fmrb_os_init();

    // for linux target
    while (1) {
        FMRB_LOGI(TAG, "app_main keep wakeup");
        FMRB_LOGI("SIG", "app_main tick=%u", (unsigned)fmrb_task_get_tick_count());
        fmrb_task_delay(FMRB_MS_TO_TICKS(10000));
    }

    fmrb_os_close();
    FMRB_LOGI(TAG, "app_main exited");
    fmrb_task_delete(NULL);
}
