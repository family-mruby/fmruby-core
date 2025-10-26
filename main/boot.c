#include <stdint.h>
#include <stdio.h>

// Family mruby modules
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

    // Serial FS proxy
    fs_proxy_create_task();
}

static void init_mem(void)
{
    fmrb_init_system_mem();
    fmrb_toml_init();
}

// Family mruby OS initialization
void fmrb_os_init(void)
{
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
