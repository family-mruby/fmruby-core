#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

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

static void print_sigset_esp(const char* label, const sigset_t* set) {
    char buf[1024];
    buf[0] = '\0';
    size_t len = 0;
    for (int s = 1; s < NSIG; ++s) {
        if (sigismember(set, s) == 1) {
            const char *name = strsignal(s);
            int n = snprintf(buf + len, sizeof(buf) - len,
                             "%s(%d) ", name ? name : "?", s);
            len += (n > 0) ? n : 0;
            if (len >= sizeof(buf) - 1) break;
        }
    }
    ESP_LOGI(TAG_SIG, "%s: %s", label, buf[0] ? buf : "(none)");
}

static void dump_signal_state(void) {
    sigset_t blocked, pending;

    if (pthread_sigmask(SIG_SETMASK, NULL, &blocked) != 0) {
        ESP_LOGE(TAG_SIG, "pthread_sigmask get failed: %s", strerror(errno));
        return;
    }
    print_sigset_esp("[Signal] BLOCKED", &blocked);

    if (sigpending(&pending) != 0) {
        ESP_LOGE(TAG_SIG, "sigpending failed: %s", strerror(errno));
        return;
    }
    print_sigset_esp("[Signal] PENDING", &pending);

    int bl = sigismember(&blocked, SIGALRM);
    int pn = sigismember(&pending, SIGALRM);
    ESP_LOGI(TAG_SIG, "[Signal] SIGALRM: blocked=%d, pending=%d", bl, pn);
}

static void unblock_timer_signals(void) {
    sigset_t unb;
    sigemptyset(&unb);
    sigaddset(&unb, SIGALRM);
#ifdef SIGRTMIN
    for (int s = SIGRTMIN; s <= SIGRTMAX; ++s) {
        sigaddset(&unb, s);
    }
#endif
    int rc = pthread_sigmask(SIG_UNBLOCK, &unb, NULL);
    if (rc != 0)
        ESP_LOGE(TAG_SIG, "UNBLOCK timer signals failed: %s", strerror(errno));
    else
        ESP_LOGI(TAG_SIG, "UNBLOCK timer signals: OK");
}


volatile unsigned long g_sigalrm_cnt = 0;

// static void sigalrm_handler(int signo, siginfo_t* info, void* uctx) {
//     (void)info; (void)uctx;
//     if (signo == SIGALRM) g_sigalrm_cnt++;
// }

// static void install_sigalrm_logger(void) {
//     struct sigaction sa = {0};
//     sa.sa_sigaction = sigalrm_handler;
//     sa.sa_flags = SA_SIGINFO;
//     sigemptyset(&sa.sa_mask);
//     if (sigaction(SIGALRM, &sa, NULL) != 0) {
//         ESP_LOGE(TAG_SIG, "sigaction(SIGALRM) failed: %s", strerror(errno));
//     } else {
//         ESP_LOGI(TAG_SIG, "SIGALRM logger installed");
//     }
// }

void log_sigalrm_counter(const char* where) {
    ESP_LOGI(TAG_SIG, "%s: SIGALRM count=%lu", where, g_sigalrm_cnt);
}

void dump_signal_mask(const char* where) {
    sigset_t blocked; sigemptyset(&blocked);
    if (pthread_sigmask(SIG_SETMASK, NULL, &blocked) != 0) {
        ESP_LOGE(TAG_SIG, "%s: pthread_sigmask get failed: %s", where, strerror(errno));
        return;
    }
    int bl = sigismember(&blocked, SIGALRM);
    ESP_LOGI(TAG_SIG, "%s: SIGALRM blocked=%d", where, bl);
}

void log_itimer_real(const char* where) {
    struct itimerval itv;
    if (getitimer(ITIMER_REAL, &itv) != 0) {
        ESP_LOGE(TAG_SIG, "%s: getitimer failed: %s", where, strerror(errno));
        return;
    }
    ESP_LOGI(TAG_SIG,
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
    ESP_LOGI(TAG, "------------------------------------------------");
    ESP_LOGI(TAG, "configTICK_RATE_HZ           = %d", configTICK_RATE_HZ);
    ESP_LOGI(TAG, "configMAX_PRIORITIES         = %d", configMAX_PRIORITIES);
    ESP_LOGI(TAG, "configMINIMAL_STACK_SIZE     = %d", configMINIMAL_STACK_SIZE);
    ESP_LOGI(TAG, "configTOTAL_HEAP_SIZE        = %d", configTOTAL_HEAP_SIZE);
    ESP_LOGI(TAG, "configUSE_PREEMPTION         = %d", configUSE_PREEMPTION);
    ESP_LOGI(TAG, "configUSE_TIME_SLICING       = %d", configUSE_TIME_SLICING);
    ESP_LOGI(TAG, "configUSE_MUTEXES            = %d", configUSE_MUTEXES);
    ESP_LOGI(TAG, "configNUM_THREAD_LOCAL_STORAGE_POINTERS = %d", configNUM_THREAD_LOCAL_STORAGE_POINTERS);
    ESP_LOGI(TAG, "configCHECK_FOR_STACK_OVERFLOW = %d", configCHECK_FOR_STACK_OVERFLOW);
    ESP_LOGI(TAG, "configUSE_TRACE_FACILITY     = %d", configUSE_TRACE_FACILITY);
    ESP_LOGI(TAG, "tick=%u", (unsigned)xTaskGetTickCount());
    ESP_LOGI(TAG, "MRB_TICK_UNIT                = %d", Machine_get_config_int(0));
    ESP_LOGI(TAG, "MRB_TIMESLICE_TICK_COUNT     = %d", Machine_get_config_int(1));
    ESP_LOGI(TAG, "------------------------------------------------");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Family mruby Core Firmware Starting...");
#ifdef CONFIG_IDF_TARGET_LINUX
    ESP_LOGI(TAG, "Running on Linux target - Development mode");
#else
    ESP_LOGI(TAG, "Running on ESP32-S3-N16R8 - Production mode");
#endif

    show_config();

#ifdef CONFIG_IDF_TARGET_LINUX
    ESP_LOGI(TAG_SIG, "=== Signal state before ===");
    dump_signal_state();

    unblock_timer_signals();

    ESP_LOGI(TAG_SIG, "=== Signal state after UNBLOCK ===");
    dump_signal_state();


    //install_sigalrm_logger();
    dump_signal_mask("app_main(before)");
    log_itimer_real("app_main(before)");

    // ここまででBLOCKED=0、interval>0、SIGALRM countが増えるならOKのはず
    usleep(1000 * 50); // 50ms
    log_sigalrm_counter("app_main(50ms)");
#endif

    // Initialize Family mruby OS
    fmrb_os_init();

    // for linux target
    while (1) {
        ESP_LOGI(TAG, "app_main keep wakeup");
        ESP_LOGI("SIG", "app_main tick=%u sigalrm=%lu",
                 (unsigned)xTaskGetTickCount(), g_sigalrm_cnt);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    ESP_LOGI(TAG, "app_main exited");
    vTaskDelete(NULL);
}
