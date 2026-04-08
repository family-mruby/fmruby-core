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
extern const uint8_t system_desktop_irep[];
extern const uint8_t shell_irep[];
extern const uint8_t editor_irep[];
extern const uint8_t config_irep[];

static fmrb_err_t spawn_system_desktop_app(int32_t* out_pid)
{
    FMRB_LOGI(TAG, "Creating system desktop app...");
    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_SYSTEM_APP,
        .type = APP_TYPE_SYSTEM_APP,
        .name = "system_desktop",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = system_desktop_irep,
        .stack_words = FMRB_SYSTEM_APP_TASK_STACK_SIZE,
        .priority = FMRB_SYSTEM_APP_TASK_PRIORITY,
        .flags = FMRB_SYSTEM_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .has_background_canvas = true,  // Desktop has bg (z=0) + fg (z=254)
        .window_width = 0,
        .window_height = 0,
        .window_pos_x = 0,
        .window_pos_y = 0
    };

    int32_t app_id;
    fmrb_err_t result;
    result = fmrb_app_spawn(&attr, &app_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "system desktop app spawned: id=%d", app_id);
        if (out_pid) {
            *out_pid = app_id;
        }
    } else {
        FMRB_LOGE(TAG, "Failed to spawn system desktop app: %d", result);
    }
    return result;
}

static fmrb_err_t spawn_shell_app(int32_t* out_pid)
{
    FMRB_LOGI(TAG, "spawn_shell_app: Starting");
    fmrb_spawn_attr_t attr = {
        .app_id = -1,  // Auto-assign available slot
        .type = APP_TYPE_USER_APP,
        .name = "shell",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = shell_irep,
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .window_width = 350,
        .window_height = 200,
        .window_pos_x = 10,
        .window_pos_y = 15
    };

    int32_t shell_id;
    fmrb_err_t result = fmrb_app_spawn(&attr, &shell_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "Shell app spawned: id=%d", shell_id);
        if (out_pid) {
            *out_pid = shell_id;
        }
    } else {
        FMRB_LOGE(TAG, "Failed to spawn shell app: %d", result);
    }
    return result;
}

static fmrb_err_t spawn_editor_app(int32_t* out_pid)
{
    FMRB_LOGI(TAG, "spawn_editor_app: Starting");
    fmrb_spawn_attr_t attr = {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = "editor",
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = editor_irep,
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .window_width = 320,
        .window_height = 240,
        .window_pos_x = 5,
        .window_pos_y = 15
    };

    int32_t editor_id;
    fmrb_err_t result = fmrb_app_spawn(&attr, &editor_id);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "Editor app spawned: id=%d", editor_id);
        if (out_pid) {
            *out_pid = editor_id;
        }
    } else {
        FMRB_LOGE(TAG, "Failed to spawn editor app: %d", result);
    }
    return result;
}

static fmrb_err_t spawn_user_app(const char* app_name, int32_t* out_pid)
{
    if (!app_name) {
        FMRB_LOGE(TAG, "app_name is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    FMRB_LOGI(TAG, "Creating user app from file: %s (stack free: %u bytes)",
              app_name, (unsigned)(fmrb_task_get_stack_high_water_mark(0) * sizeof(StackType_t)));

    FMRB_LOGI(TAG, "[spawn] 1 malloc");
    // Allocate work buffers on heap to avoid stack overflow
    // (this function is called from deep in the kernel mruby call stack)
    char* toml_path = (char*)fmrb_sys_malloc(FMRB_MAX_PATH_LEN);
    char* errbuf = (char*)fmrb_sys_malloc(256);
    if (!toml_path || !errbuf) {
        FMRB_LOGE(TAG, "Failed to allocate work buffers");
        if (toml_path) fmrb_sys_free(toml_path);
        if (errbuf) fmrb_sys_free(errbuf);
        return FMRB_ERR_NO_MEMORY;
    }

    fmrb_err_t result = FMRB_ERR_FAILED;

    FMRB_LOGI(TAG, "[spawn] 2 file_open");
    // Validate file exists before spawning
    fmrb_file_t file = NULL;
    fmrb_err_t ret = fmrb_hal_file_open(app_name, FMRB_O_RDONLY, &file);
    FMRB_LOGI(TAG, "[spawn] 3 file_open ret=%d", ret);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "File not found or cannot open: %s", app_name);
        result = FMRB_ERR_NOT_FOUND;
        goto cleanup_buffers;
    }
    fmrb_hal_file_close(file);

    FMRB_LOGI(TAG, "[spawn] 4 vm_type");
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
        } else if (strcmp(ext, ".bas") == 0) {
            vm_type = FMRB_VM_TYPE_BASIC;
            FMRB_LOGI(TAG, "Detected BASIC script: %s", app_name);
        }
    }

    FMRB_LOGI(TAG, "[spawn] 5 toml_path");
    // Build TOML configuration file path
    snprintf(toml_path, FMRB_MAX_PATH_LEN, "%s", app_name);

    // Replace last extension with .toml
    char* last_dot = strrchr(toml_path, '.');
    if (!last_dot) {
        FMRB_LOGE(TAG, "Invalid app name (no extension): %s", app_name);
        result = FMRB_ERR_INVALID_PARAM;
        goto cleanup_buffers;
    }

    // Check if ".toml" fits in buffer
    size_t base_len = last_dot - toml_path;
    if (base_len + 5 >= FMRB_MAX_PATH_LEN) {
        FMRB_LOGE(TAG, "TOML path would exceed buffer size: %s", app_name);
        result = FMRB_ERR_INVALID_PARAM;
        goto cleanup_buffers;
    }

    strcpy(last_dot, ".toml");

    // Default values for spawn attributes
    const char* app_screen_name = NULL;
    const char* toml_screen_name = NULL;
    const char* toml_window_mode = NULL;
    bool headless = false;
    bool fullscreen = false;
    int window_width = 100;
    int window_height = 100;
    int window_pos_x = 50;
    int window_pos_y = 50;

    FMRB_LOGI(TAG, "[spawn] 6 toml_load '%s'", toml_path);
    // Try loading TOML configuration
    toml_table_t* config = fmrb_toml_load_file(toml_path, errbuf, 256);
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
            } else if (strcmp(toml_window_mode, "fullscreen") == 0) {
                fullscreen = true;
            } else {
                headless = false;
            }
        }

        // Parse window dimensions and position
        if (fullscreen) {
            const fmrb_system_config_t* sys_config = fmrb_kernel_get_config();
            window_width = sys_config->display_width;
            window_height = sys_config->display_height;
            window_pos_x = 0;
            window_pos_y = 0;
        } else {
            window_width = (int)fmrb_toml_get_int(config, "default_window_width", 100);
            window_height = (int)fmrb_toml_get_int(config, "default_window_height", 100);
            window_pos_x = (int)fmrb_toml_get_int(config, "default_window_pos_x", 50);
            window_pos_y = (int)fmrb_toml_get_int(config, "default_window_pos_y", 50);
        }
    } else {
        FMRB_LOGW(TAG, "No TOML config found or parse error: %s (%s)", toml_path, errbuf);
    }

    FMRB_LOGI(TAG, "[spawn] 7 fmrb_app_spawn vm=%d", vm_type);
    // Set spawn attributes
    fmrb_spawn_attr_t attr = {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = app_screen_name,
        .vm_type = vm_type,
        .load_mode = FMRB_LOAD_MODE_FILE,
        .filepath = app_name,
        .stack_words = FMRB_USER_APP_TASK_STACK_SIZE,
        .priority = FMRB_USER_APP_PRIORITY,
        .flags = FMRB_USER_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = headless,
        .fullscreen = fullscreen,
        .window_width = window_width,
        .window_height = window_height,
        .window_pos_x = window_pos_x,
        .window_pos_y = window_pos_y
    };

    // Spawn the app
    int32_t app_id;
    result = fmrb_app_spawn(&attr, &app_id);
    FMRB_LOGI(TAG, "[spawn] 8 fmrb_app_spawn ret=%d", result);
    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "User app spawned: id=%d, name=%s, file=%s",
                  app_id, app_screen_name, app_name);
        if (out_pid) {
            *out_pid = app_id;
        }
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

cleanup_buffers:
    fmrb_sys_free(toml_path);
    fmrb_sys_free(errbuf);
    return result;
}

fmrb_err_t fmrb_app_spawn_app(const char* app_name, int32_t* out_pid)
{
    if (app_name == NULL) {
        FMRB_LOGE(TAG, "app_name is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    // Match app_name to spawn function
    // PreBuild Apps
    if (strcmp(app_name, "system/desktop") == 0) {
        return spawn_system_desktop_app(out_pid);
    } else if (strcmp(app_name, "default/shell") == 0) {
        return spawn_shell_app(out_pid);
    } else if (strcmp(app_name, "default/editor") == 0) {
        return spawn_editor_app(out_pid);
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
    return spawn_user_app(app_name, out_pid);
}