#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "toml.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An app's metadata: its .app.toml sidecar and the "#---fmrb" comment fence
 * at the top of its .rb, read together (doc/app-distribution/spec.md 3.1).
 *
 * Both forms take the same keys with the same meanings. When both are there
 * they are merged key by key, and the sidecar wins a key both of them set.
 * This is the one reader: the spawner, the launcher, the desktops, the Shell
 * and checkpoint all ask here.
 */
typedef struct {
    toml_table_t *sidecar;  /**< the .app.toml, NULL when absent or unparsable */
    toml_table_t *comment;  /**< the comment fence, NULL when absent or broken */
    bool has_sidecar;       /**< a sidecar file exists (even if it did not parse) */
    bool has_comment;       /**< the .rb opens a comment fence (even if broken) */
} fmrb_app_meta_t;

/**
 * The sidecar path of an app: its last extension replaced by ".toml"
 * (/app/x/foo.app.rb -> /app/x/foo.app.toml). False when it does not fit.
 */
bool fmrb_app_meta_sidecar_path(const char *app_path, char *out, size_t n);

/**
 * Read both forms of app_path's metadata into m. True when the app has
 * either form (a file that is an app rather than a plain script); m is
 * filled either way and must be released with fmrb_app_meta_free.
 * The comment fence is read from .rb files only.
 */
bool fmrb_app_meta_load(const char *app_path, fmrb_app_meta_t *m);

/** Release what fmrb_app_meta_load allocated. */
void fmrb_app_meta_free(fmrb_app_meta_t *m);

/**
 * The table that answers key: the sidecar when it sets the key, else the
 * comment when it does, else NULL. Use it with the fmrb_toml_get_* getters
 * (which take NULL as "not set").
 */
const toml_table_t *fmrb_app_meta_table(const fmrb_app_meta_t *m, const char *key);

/**
 * Visit every top-level key of the merged metadata once, as text: a string
 * without its quotes, a boolean as "true"/"false", a number as written, an
 * array as its items joined by one space. Tables are skipped.
 */
typedef void (*fmrb_app_meta_visit_t)(void *ud, const char *key, const char *val, size_t len);
void fmrb_app_meta_each(const fmrb_app_meta_t *m, fmrb_app_meta_visit_t visit, void *ud);

/**
 * The merged metadata of app_path packed for the Spinel bridge:
 * [count u8] then per key [klen u8][key][vlen u16 LE][value].
 * Returns the bytes written, or -1 when the file has neither form.
 * Keys or values that do not fit are left out.
 */
int fmrb_app_meta_pack(const char *app_path, uint8_t *buf, size_t cap);

#ifdef __cplusplus
}
#endif
