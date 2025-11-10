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

static const char* extract_app_name(const char* filepath, char* name_buf, size_t buf_size) {
    if (!filepath || !name_buf || buf_size == 0) {
        if (name_buf && buf_size > 0) {
            name_buf[0] = '\0';
        }
        return name_buf;
    }

    // Find last '/' to get basename
    const char* basename = strrchr(filepath, '/');
    if (basename) {
        basename++;  // Skip '/'
    } else {
        basename = filepath;
    }

    // Copy to buffer
    strncpy(name_buf, basename, buf_size - 1);
    name_buf[buf_size - 1] = '\0';

    // Remove .rb extension if present
    char* ext = strrchr(name_buf, '.');
    if (ext && strcmp(ext, ".rb") == 0) {
        *ext = '\0';
    }

    return name_buf;
}


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
    FMRB_LOGI(TAG, "spawn_shell_app: Starting");
    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_USER_APP0,              
        .type = APP_TYPE_USER_APP,            
        .name = "shell",
        .load_mode = FMRB_LOAD_MODE_IREP,
        .irep = shell_irep,             
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,  
        .priority = FMRB_SHELL_APP_PRIORITY,        
        .core_affinity = -1,                  
        .headless = false,
        .window_pos_x = 10,
        .window_pos_y = 30           
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
    if (!app_name) {
        FMRB_LOGE(TAG, "app_name is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    // Extract display name from file path
    char display_name[32];
    extract_app_name(app_name, display_name, sizeof(display_name));

    FMRB_LOGI(TAG, "Creating user app from file: %s (name: %s)", app_name, display_name);

    // Validate file exists before spawning
    fmrb_file_t file = NULL;
    fmrb_err_t ret = fmrb_hal_file_open(app_name, FMRB_O_RDONLY, &file);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "File not found or cannot open: %s", app_name);
        return FMRB_ERR_NOT_FOUND;
    }
    fmrb_hal_file_close(file);

    // Set spawn attributes
    fmrb_spawn_attr_t attr = {
        .app_id = -1,                                  // Auto-allocate slot
        .type = APP_TYPE_USER_APP,                     // User App type
        .name = display_name,                          // Extracted name
        .load_mode = FMRB_LOAD_MODE_FILE,              // Load from file
        .filepath = app_name,                          // File path
        .stack_words = FMRB_USER_APP_TASK_STACK_SIZE,  // 60KB
        .priority = FMRB_USER_APP_PRIORITY,            // Priority 5
        .core_affinity = -1,                           // No core affinity
        .headless = false                              // Graphics enabled
    };

    // Spawn the app
    int32_t app_id;
    fmrb_err_t result = fmrb_app_spawn(&attr, &app_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "User app spawned: id=%d, name=%s, file=%s",
                  app_id, display_name, app_name);
    } else {
        FMRB_LOGE(TAG, "Failed to spawn user app: %s (error=%d)", app_name, result);
    }

    return result;
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

    // For paths starting with system/ or default/, reject as unknown built-in app
    if (strncmp(app_name, "system/", 7) == 0 || strncmp(app_name, "default/", 8) == 0) {
        FMRB_LOGE(TAG, "Unknown built-in app name: %s", app_name);
        return FMRB_ERR_NOT_FOUND;
    }

    // User App from filesystem
    // Assume any other path is a filesystem path (e.g., "/flash/app/myapp.rb")
    return spawn_user_app(app_name);
}