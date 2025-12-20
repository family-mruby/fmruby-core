/**
 * @file fmrb_lua.c
 * @brief FMRuby Lua integration wrapper implementation
 */

#include "fmrb_lua.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"

static const char *TAG = "fmrb_lua";

/**
 * Lua allocator using fmrb_sys_malloc
 *
 * Note: Using system malloc for now. In the future, we should use
 * per-task memory pools like mruby does.
 */
static void* lua_fmrb_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;
    (void)osize;

    if (nsize == 0) {
        if (ptr) {
            fmrb_sys_free(ptr);
        }
        return NULL;
    }

    // Realloc or alloc
    if (ptr) {
        // Lua doesn't provide standard realloc, so we need to manually copy
        void* new_ptr = fmrb_sys_malloc(nsize);
        if (!new_ptr) {
            return NULL;
        }

        // Copy old data
        size_t copy_size = (osize < nsize) ? osize : nsize;
        if (copy_size > 0) {
            memcpy(new_ptr, ptr, copy_size);
        }

        fmrb_sys_free(ptr);
        return new_ptr;
    } else {
        return fmrb_sys_malloc(nsize);
    }
}

/**
 * Initialize FMRuby Lua subsystem
 */
fmrb_err_t fmrb_lua_init(void) {
    FMRB_LOGI(TAG, "Lua subsystem initialized");
    return FMRB_OK;
}

/**
 * Create new Lua state with FMRuby memory allocator
 */
lua_State* fmrb_lua_newstate(void) {
    lua_State* L = lua_newstate(lua_fmrb_alloc, NULL);
    if (!L) {
        FMRB_LOGE(TAG, "Failed to create Lua state");
        return NULL;
    }

    FMRB_LOGI(TAG, "Lua state created");
    return L;
}

/**
 * Close Lua state and free resources
 */
void fmrb_lua_close(lua_State* L) {
    if (L) {
        lua_close(L);
        FMRB_LOGI(TAG, "Lua state closed");
    }
}

/**
 * Open standard Lua libraries
 */
void fmrb_lua_openlibs(lua_State* L) {
    if (L) {
        luaL_openlibs(L);
        FMRB_LOGI(TAG, "Lua standard libraries opened");
    }
}
