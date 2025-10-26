#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Include tomlc99 library (must come first for type definitions)
#include "toml.h"
#include "fmrb_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize TOML Lib with custom mem allcator
 */
void fmrb_toml_init(void);

// ============================================================================
// File loading and parsing
// ============================================================================

/**
 * Load TOML file from filesystem
 *
 * @param path Path to TOML file (e.g., "/etc/config.toml")
 * @param errbuf Error message buffer
 * @param errbufsz Size of error buffer
 * @return Parsed TOML table, or NULL on error. Caller must toml_free() the result.
 */
toml_table_t* fmrb_toml_load_file(const char* path, char* errbuf, int errbufsz);

// ============================================================================
// Type-safe value getters with defaults
// ============================================================================

/**
 * Get string value from table
 *
 * @param tab TOML table
 * @param key Key to look up
 * @param default_val Default value if key not found
 * @return String value. If not default, caller must free() the returned string.
 */
const char* fmrb_toml_get_string(const toml_table_t* tab, const char* key, const char* default_val);

/**
 * Get integer value from table
 */
int64_t fmrb_toml_get_int(const toml_table_t* tab, const char* key, int64_t default_val);

/**
 * Get double value from table
 */
double fmrb_toml_get_double(const toml_table_t* tab, const char* key, double default_val);

/**
 * Get boolean value from table
 */
bool fmrb_toml_get_bool(const toml_table_t* tab, const char* key, bool default_val);

// ============================================================================
// Path-based access (e.g., "server.database.port")
// ============================================================================

/**
 * Get value by path notation
 *
 * Example: fmrb_toml_get_by_path(root, "server.port")
 *
 * @param root Root TOML table
 * @param path Dot-separated path to value
 * @return toml_datum_t with ok=1 if found, ok=0 otherwise
 */
toml_datum_t fmrb_toml_get_by_path(const toml_table_t* root, const char* path);

// ============================================================================
// Array helpers
// ============================================================================

/**
 * Get string array elements
 *
 * @param arr TOML array
 * @param out Output array (caller must free each string)
 * @param max_count Maximum elements to retrieve
 * @return Number of elements retrieved
 */
int fmrb_toml_array_get_strings(const toml_array_t* arr, char** out, int max_count);

/**
 * Get integer array elements
 *
 * @param arr TOML array
 * @param out Output array
 * @param max_count Maximum elements to retrieve
 * @return Number of elements retrieved
 */
int fmrb_toml_array_get_ints(const toml_array_t* arr, int64_t* out, int max_count);

// ============================================================================
// Debug/dump functions
// ============================================================================

/**
 * Dump TOML table structure for debugging
 *
 * @param tab TOML table
 * @param indent Indentation level
 */
void dump_toml_table(toml_table_t *tab, int indent);

#ifdef __cplusplus
}
#endif
