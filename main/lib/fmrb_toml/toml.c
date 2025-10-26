#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "toml.h"
#include "fmrb_hal.h"
#include "fmrb_mem.h"
#include "fmrb_toml.h"

const static char* TAG = "toml";

// ============================================================================
// Helper functions for easier TOML file loading and access
// ============================================================================

void fmrb_toml_init(void) {
    static bool initialized = false;
    if(initialized == true)
    {
        return;
    }
    initialized = true;

    toml_set_memutil(fmrb_sys_malloc, fmrb_sys_free);
    FMRB_LOGI(TAG, "fmrb_toml_init done");
}

/**
 * Load TOML from file
 */
toml_table_t* fmrb_toml_load_file(const char* path, char* errbuf, int errbufsz) {
    fmrb_file_t file;
    char *buffer = NULL;
    size_t file_size = 0;
    toml_table_t *conf = NULL;

    // Get file info
    fmrb_file_info_t info;
    fmrb_err_t ret = fmrb_hal_file_stat(path, &info);
    if (ret != FMRB_OK) {
        snprintf(errbuf, errbufsz, "File not found: %s", path);
        return NULL;
    }

    if (info.size == 0) {
        snprintf(errbuf, errbufsz, "File is empty: %s", path);
        return NULL;
    }
    file_size = info.size;

    // Open file
    ret = fmrb_hal_file_open(path, FMRB_O_RDONLY, &file);
    if (ret != FMRB_OK) {
        snprintf(errbuf, errbufsz, "Failed to open file: %s", path);
        return NULL;
    }

    // Allocate buffer
    buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        snprintf(errbuf, errbufsz, "Memory allocation failed (%zu bytes)", file_size);
        fmrb_hal_file_close(file);
        return NULL;
    }

    // Read file
    size_t bytes_read;
    ret = fmrb_hal_file_read(file, buffer, file_size, &bytes_read);
    fmrb_hal_file_close(file);

    if (ret != FMRB_OK || bytes_read != file_size) {
        snprintf(errbuf, errbufsz, "Read error: got %zu of %zu bytes", bytes_read, file_size);
        free(buffer);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    // Parse TOML
    conf = toml_parse(buffer, errbuf, errbufsz);
    free(buffer);

    return conf;
}

/**
 * Get string value with default
 */
const char* fmrb_toml_get_string(const toml_table_t* tab, const char* key, const char* default_val) {
    if (!tab || !key) return default_val;

    toml_datum_t d = toml_string_in(tab, key);
    if (d.ok) {
        // Note: Caller must free the returned string
        return d.u.s;
    }
    return default_val;
}

/**
 * Get int value with default
 */
int64_t fmrb_toml_get_int(const toml_table_t* tab, const char* key, int64_t default_val) {
    if (!tab || !key) return default_val;

    toml_datum_t d = toml_int_in(tab, key);
    return d.ok ? d.u.i : default_val;
}

/**
 * Get double value with default
 */
double fmrb_toml_get_double(const toml_table_t* tab, const char* key, double default_val) {
    if (!tab || !key) return default_val;

    toml_datum_t d = toml_double_in(tab, key);
    return d.ok ? d.u.d : default_val;
}

/**
 * Get bool value with default
 */
bool fmrb_toml_get_bool(const toml_table_t* tab, const char* key, bool default_val) {
    if (!tab || !key) return default_val;

    toml_datum_t d = toml_bool_in(tab, key);
    return d.ok ? (d.u.b != 0) : default_val;
}

/**
 * Get value by path (e.g., "server.port")
 */
toml_datum_t fmrb_toml_get_by_path(const toml_table_t* root, const char* path) {
    toml_datum_t result = {0};
    if (!root || !path) return result;

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    const toml_table_t* current_tab = root;
    char* token = strtok(path_copy, ".");
    char* next_token = NULL;

    while (token) {
        next_token = strtok(NULL, ".");

        if (next_token) {
            // Not the last token, traverse to next table
            current_tab = toml_table_in(current_tab, token);
            if (!current_tab) {
                return result;  // Path not found
            }
        } else {
            // Last token, try to get value
            // Try different types
            result = toml_string_in(current_tab, token);
            if (result.ok) return result;

            result = toml_int_in(current_tab, token);
            if (result.ok) return result;

            result = toml_double_in(current_tab, token);
            if (result.ok) return result;

            result = toml_bool_in(current_tab, token);
            if (result.ok) return result;
        }

        token = next_token;
    }

    return result;
}

/**
 * Get string array elements
 */
int fmrb_toml_array_get_strings(const toml_array_t* arr, char** out, int max_count) {
    if (!arr || !out || max_count <= 0) return 0;

    int n = toml_array_nelem(arr);
    int count = 0;

    for (int i = 0; i < n && count < max_count; i++) {
        toml_datum_t elem = toml_string_at(arr, i);
        if (elem.ok) {
            out[count++] = elem.u.s;  // Caller must free each string
        }
    }

    return count;
}

/**
 * Get int array elements
 */
int fmrb_toml_array_get_ints(const toml_array_t* arr, int64_t* out, int max_count) {
    if (!arr || !out || max_count <= 0) return 0;

    int n = toml_array_nelem(arr);
    int count = 0;

    for (int i = 0; i < n && count < max_count; i++) {
        toml_datum_t elem = toml_int_at(arr, i);
        if (elem.ok) {
            out[count++] = elem.u.i;
        }
    }

    return count;
}

// ============================================================================
// Debug/dump functions
// ============================================================================

void dump_toml_table(toml_table_t *tab, int indent) {
    const char *key;
    char indent_str[64];
    memset(indent_str, ' ', indent < 63 ? indent : 63);
    indent_str[indent < 63 ? indent : 63] = '\0';

    for (int i = 0; (key = toml_key_in(tab, i)); i++) {
        toml_datum_t v;

        // try as table
        toml_table_t *child_tab = toml_table_in(tab, key);
        if (child_tab) {
            FMRB_LOGI(TAG, "%s[%s]", indent_str, key);
            dump_toml_table(child_tab, indent + 2);
            continue;
        }

        // try as array
        toml_array_t *arr = toml_array_in(tab, key);
        if (arr) {
            int n = toml_array_nelem(arr);
            FMRB_LOGI(TAG, "%s%s = [ (%d elements) ]", indent_str, key, n);
            for (int k = 0; k < n; k++) {
                toml_datum_t elem = toml_string_at(arr, k);
                if (elem.ok) {
                    FMRB_LOGI(TAG, "%s  [%d] \"%s\"", indent_str, k, elem.u.s);
                    fmrb_sys_free(elem.u.s);
                } else {
                    toml_datum_t num = toml_int_at(arr, k);
                    if (num.ok) {
                        FMRB_LOGI(TAG, "%s  [%d] %" PRId64, indent_str, k, num.u.i);
                    }
                }
            }
            continue;
        }

        // try primitive values
        v = toml_string_in(tab, key);
        if (v.ok) {
            FMRB_LOGI(TAG, "%s%s = \"%s\"", indent_str, key, v.u.s);
            fmrb_sys_free(v.u.s);
            continue;
        }

        v = toml_int_in(tab, key);
        if (v.ok) {
            FMRB_LOGI(TAG, "%s%s = %" PRId64, indent_str, key, v.u.i);
            continue;
        }

        v = toml_double_in(tab, key);
        if (v.ok) {
            FMRB_LOGI(TAG, "%s%s = %f", indent_str, key, v.u.d);
            continue;
        }

        v = toml_bool_in(tab, key);
        if (v.ok) {
            FMRB_LOGI(TAG, "%s%s = %s", indent_str, key, v.u.b ? "true" : "false");
            continue;
        }
    }
}