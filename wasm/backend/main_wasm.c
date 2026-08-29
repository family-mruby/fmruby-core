/* main_wasm.c - process entry for the wasm firmware.
 *
 * On ESP-IDF app_main already runs inside a FreeRTOS task; here main() (on
 * the PROXY_TO_PTHREAD worker) has to create that task itself and hand the
 * thread to the scheduler, exactly the way the P1 PoC bootstraps.
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

void app_main(void);

#define MAIN_TASK_STACK_BYTES  (256 * 1024)
#define MAIN_TASK_PRIORITY     5

static void prvMainTask(void *arg)
{
    (void)arg;
    app_main();
    vTaskDelete(NULL);
}

int main(void)
{
    if (xTaskCreate(prvMainTask, "app_main", MAIN_TASK_STACK_BYTES, NULL,
                    MAIN_TASK_PRIORITY, NULL) != pdPASS) {
        printf("bootstrap failed: could not create the main task\n");
        return 1;
    }
    vTaskStartScheduler();
    printf("bootstrap failed: the scheduler returned\n");
    return 1;
}
