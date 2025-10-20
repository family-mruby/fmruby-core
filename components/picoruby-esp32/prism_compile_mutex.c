// Prism compile mutex implementation for ESP32 and Linux targets
#include <stddef.h>

#if defined(PICORB_PLATFORM_POSIX)
  // Linux: use pthread mutex
  #include <pthread.h>
  static pthread_mutex_t prism_compile_mutex = PTHREAD_MUTEX_INITIALIZER;

  void prism_compile_lock(void) {
    pthread_mutex_lock(&prism_compile_mutex);
  }

  void prism_compile_unlock(void) {
    pthread_mutex_unlock(&prism_compile_mutex);
  }

#else
  // ESP32: use FreeRTOS semaphore
  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"

  static SemaphoreHandle_t prism_compile_mutex = NULL;

  static void prism_compile_mutex_init(void) {
    if (prism_compile_mutex == NULL) {
      prism_compile_mutex = xSemaphoreCreateMutex();
    }
  }

  void prism_compile_lock(void) {
    if (prism_compile_mutex == NULL) {
      prism_compile_mutex_init();
    }
    xSemaphoreTake(prism_compile_mutex, portMAX_DELAY);
  }

  void prism_compile_unlock(void) {
    xSemaphoreGive(prism_compile_mutex);
  }

#endif
