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
#include "fmrb_toml.h"

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
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = system_gui_irep,
        .stack_words = FMRB_SYSTEM_APP_TASK_STACK_SIZE,
        .priority = FMRB_SYSTEM_APP_TASK_PRIORITY,
        .core_affinity = -1,  // No core affinity
        .headless = false,
        .window_width = 0,    // Use system default (fullscreen)
        .window_height = 0,   // Use system default (fullscreen)
        .window_pos_x = 0,
        .window_pos_y = 0
    };

    int32_t app_id;
    fmrb_err_t result;
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
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = shell_irep,
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .core_affinity = -1,
        .headless = false,
        .window_width = 0,    // Use system default
        .window_height = 0,   // Use system default
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

    FMRB_LOGI(TAG, "Creating user app from file: %s", app_name);

    // Validate file exists before spawning
    fmrb_file_t file = NULL;
    fmrb_err_t ret = fmrb_hal_file_open(app_name, FMRB_O_RDONLY, &file);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "File not found or cannot open: %s", app_name);
        return FMRB_ERR_NOT_FOUND;
    }
    fmrb_hal_file_close(file);

    // Determine VM type from file extension
    fmrb_vm_type_t vm_type = FMRB_VM_TYPE_MRUBY;  // Default to mruby
    const char* ext = strrchr(app_name, '.');
    if (ext) {
        if (strcmp(ext, ".lua") == 0) {
            vm_type = FMRB_VM_TYPE_LUA;
            FMRB_LOGI(TAG, "Detected Lua script: %s", app_name);
        } else if (strcmp(ext, ".rb") == 0) {
            vm_type = FMRB_VM_TYPE_MRUBY;
            FMRB_LOGI(TAG, "Detected mruby script: %s", app_name);
        }
    }

    // Try to load TOML configuration file
    // For "xxx.app.lua" or "xxx.app.rb", replace with "xxx.toml"
    char toml_path[FMRB_MAX_PATH_LEN];
    snprintf(toml_path, sizeof(toml_path), "%s", app_name);

    // Find ".app.lua" or ".app.rb" and replace with ".toml"
    char* app_ext = strstr(toml_path, ".app.");
    if (app_ext) {
        // Found ".app.lua" or ".app.rb", replace from ".app" onwards
        strcpy(app_ext, ".toml");
    } else {
        // No ".app." found, replace last extension
        char* last_dot = strrchr(toml_path, '.');
        if (last_dot) {
            strcpy(last_dot, ".toml");
        } else {
            strcat(toml_path, ".toml");
        }
    }

    // Default values for spawn attributes
    const char* app_screen_name = NULL;
    const char* toml_screen_name = NULL;
    const char* toml_window_mode = NULL;
    bool headless = false;
    int window_width = 100;
    int window_height = 100;
    int window_pos_x = 50;
    int window_pos_y = 50;

    // Try loading TOML configuration
    char errbuf[256];
    toml_table_t* config = fmrb_toml_load_file(toml_path, errbuf, sizeof(errbuf));
    if (config) {
        FMRB_LOGI(TAG, "Loaded TOML config: %s", toml_path);

        // Parse app_screen_name
        toml_screen_name = fmrb_toml_get_string(config, "app_screen_name", NULL);
        if (toml_screen_name) {
            app_screen_name = toml_screen_name;
        }

        // Parse default_window_mode
        toml_window_mode = fmrb_toml_get_string(config, "default_window_mode", NULL);
        if (toml_window_mode) {
            if (strcmp(toml_window_mode, "background") == 0) {
                headless = true;
            } else {
                headless = false;
            }
        }

        // Parse window dimensions and position
        window_width = (int)fmrb_toml_get_int(config, "default_window_width", 100);
        window_height = (int)fmrb_toml_get_int(config, "default_window_height", 100);
        window_pos_x = (int)fmrb_toml_get_int(config, "default_window_pos_x", 50);
        window_pos_y = (int)fmrb_toml_get_int(config, "default_window_pos_y", 50);

        // Note: window_mode parsing could be extended to handle fullscreen/fullwindow
        // but currently we only use it to determine headless mode
    } else {
        FMRB_LOGW(TAG, "No TOML config found or parse error: %s (%s)", toml_path, errbuf);
    }

    // Set spawn attributes
    fmrb_spawn_attr_t attr = {
        .app_id = -1,                                  // Auto-allocate slot
        .type = APP_TYPE_USER_APP,                     // User App type
        .name = app_screen_name,                       // From TOML or extracted name
        .vm_type = vm_type,                            // Auto-detected VM type
        .load_mode = FMRB_LOAD_MODE_FILE,              // Load from file
        .filepath = app_name,                          // File path
        .stack_words = FMRB_USER_APP_TASK_STACK_SIZE,  // Stack Size
        .priority = FMRB_USER_APP_PRIORITY,            // 
        .core_affinity = -1,                           // No core affinity
        .headless = headless,                          // From TOML
        .window_width = window_width,                  // From TOML
        .window_height = window_height,                // From TOML
        .window_pos_x = window_pos_x,                  // From TOML
        .window_pos_y = window_pos_y                   // From TOML
    };

    // Spawn the app
    int32_t app_id;
    fmrb_err_t result = fmrb_app_spawn(&attr, &app_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "User app spawned: id=%d, name=%s, file=%s",
                  app_id, app_screen_name, app_name);
    } else {
        FMRB_LOGE(TAG, "Failed to spawn user app: %s (error=%d)", app_name, result);
    }

    // Free TOML config and allocated strings
    if (toml_screen_name) {
        fmrb_sys_free((void*)toml_screen_name);
    }
    if (toml_window_mode) {
        fmrb_sys_free((void*)toml_window_mode);
    }
    if (config) {
        toml_free(config);
    }

    return result;
}

fmrb_err_t fmrb_app_spawn_app(const char* app_name)
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