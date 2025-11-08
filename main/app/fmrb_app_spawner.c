#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <picoruby.h>
#include "fmrb_hal.h"
#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_kernel.h"

static const char *TAG = "fmrb_default_apps";

// External irep declarations (compiled by picorbc)
extern const uint8_t system_gui_irep[];
extern const uint8_t shell_irep[];
extern const uint8_t editor_irep[];
extern const uint8_t config_irep[];


static fmrb_err_t spawn_system_gui_app(void)
{
    FMRB_LOGI(TAG, "Creating system GUI app...");
    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_SYSTEM_APP,
        .type = APP_TYPE_SYSTEM_APP,
        .name = "system_gui",
        .load_mode = FMRB_LOAD_MODE_IREP,
        .irep = system_gui_irep,
        .stack_words = FMRB_SYSTEM_APP_TASK_STACK_SIZE,
        .priority = FMRB_SYSTEM_APP_TASK_PRIORITY,
        .core_affinity = -1  // No core affinity
    };

    int32_t app_id;
    fmrb_err_t result;
    fmrb_app_init();
    result = fmrb_app_spawn(&attr, &app_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "system GUI app spawned: id=%d", app_id);
    } else {
        FMRB_LOGE(TAG, "Failed to spawn system GUI app: %d", result);
    }
   return FMRB_OK;
}

static fmrb_err_t spawn_shell_app(void)
{
    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_USER_APP0,              // User App slot 0
        .type = APP_TYPE_USER_APP,                 // User App type
        .name = "shell",
        .load_mode = FMRB_LOAD_MODE_IREP,
        .irep = shell_irep,                        // shell.c irep array
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,  // 60KB
        .priority = FMRB_SHELL_APP_PRIORITY,            // Priority 5
        .core_affinity = -1,                       // No core affinity
        .headless = false                          // Graphics enabled
    };

    int32_t shell_id;
    fmrb_err_t result = fmrb_app_spawn(&attr, &shell_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "Shell app spawned: id=%d", shell_id);
    } else {
        FMRB_LOGE(TAG, "Failed to spawn shell app: %d", result);
    }
    return result;
}

static fmrb_err_t spawn_user_app(const char* app_name)
{
    // FMRB_LOGI(TAG, "Creating system GUI app...");
    // fmrb_spawn_attr_t attr = {
    //     .app_id = PROC_ID_SYSTEM_APP,
    //     .type = APP_TYPE_SYSTEM_APP,
    //     .name = "system_gui",
    //     .irep = system_gui_irep,
    //     .stack_words = FMRB_SYSTEM_APP_TASK_STACK_SIZE,
    //     .priority = FMRB_SYSTEM_APP_TASK_PRIORITY,
    //     .core_affinity = -1  // No core affinity
    // };

    // int32_t app_id;
    // fmrb_err_t result;
    // fmrb_app_init();
    // result = fmrb_app_spawn(&attr, &app_id);
    // if (result == FMRB_OK) {
    //     FMRB_LOGI(TAG, "system GUI app spawned: id=%d", app_id);
    // } else {
    //     FMRB_LOGE(TAG, "Failed to spawn system GUI app: %d", result);
    // }
   return FMRB_OK;
}

fmrb_err_t fmrb_app_spawn_default_app(const char* app_name)
{
    if (app_name == NULL) {
        FMRB_LOGE(TAG, "app_name is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    // Match app_name to spawn function
    // PreBuild Apps
    if (strcmp(app_name, "system/gui_app") == 0) {
        return spawn_system_gui_app();
    } else if (strcmp(app_name, "default/shell") == 0) {
        return spawn_shell_app();
    } else if (strcmp(app_name, "default/editor") == 0) {
        // Future implementation
        FMRB_LOGW(TAG, "Editor app not yet implemented");
        return FMRB_ERR_NOT_SUPPORTED;
    } else if (strcmp(app_name, "default/config") == 0) {
        // Future implementation
        FMRB_LOGW(TAG, "Config app not yet implemented");
        return FMRB_ERR_NOT_SUPPORTED;
    } 

    // Validate app_name prefix (only allow "system/" or "default/")
    if (strncmp(app_name, "system/", 7) != 0 && strncmp(app_name, "default/", 8) != 0) {
        FMRB_LOGE(TAG, "Invalid app name prefix: %s (must start with 'system/' or 'default/')", app_name);
        return FMRB_ERR_FAILED;
    }

    // User App
    // check if it exists
    {
        FMRB_LOGE(TAG, "Unknown app name: %s", app_name);
        return FMRB_ERR_NOT_FOUND;
    }
    // TODO: Load xxx.app.rb from filesystem
    return spawn_user_app(app_name);
}