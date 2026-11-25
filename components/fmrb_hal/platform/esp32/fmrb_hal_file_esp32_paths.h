#pragma once

// Where the ESP32 file HAL mounts its volumes (fmrb_hal_file_esp32.c mounts
// them, fmrb_hal_file_path_esp32.c resolves paths onto them).
#define LITTLEFS_PATH "/flash"
#define SDCARD_PATH "/sd"
