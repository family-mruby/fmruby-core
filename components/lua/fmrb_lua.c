#include "fmrb_lua.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include <string.h>

static const char *TAG = "fmrb_lua";

/**
 * Lua allocator using per-task memory pool
 *
 * Uses fmrb_malloc/fmrb_free with the memory pool from the task context.
 * The context is passed through the ud (user data) parameter.
 */
static void* lua_fmrb_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    fmrb_app_task_context_t* ctx = (fmrb_app_task_context_t*)ud;

    if (!ctx) {
        return NULL;
    }

    if (nsize == 0) {
        // Free memory
        if (ptr) {
            fmrb_free(ctx->mem_handle, ptr);
        }
        return NULL;
    }

    if (ptr) {
        // Realloc: allocate new, copy, free old
        void* new_ptr = fmrb_malloc(ctx->mem_handle, nsize);
        if (!new_ptr) {
            return NULL;
        }

        // Copy old data
        size_t copy_size = (osize < nsize) ? osize : nsize;
        if (copy_size > 0) {
            memcpy(new_ptr, ptr, copy_size);
        }

        fmrb_free(ctx->mem_handle, ptr);
        return new_ptr;
    }

    // New allocation
    return fmrb_malloc(ctx->mem_handle, nsize);
}

/**
 * Initialize FMRuby Lua subsystem
 */
fmrb_err_t fmrb_lua_init(void) {
    FMRB_LOGI(TAG, "Lua subsystem initialized");
    return FMRB_OK;
}

/**
 * Create new Lua state with per-task memory pool
 */
lua_State* fmrb_lua_newstate(fmrb_app_task_context_t* ctx) {
    if (!ctx) {
        FMRB_LOGE(TAG, "Context is NULL");
        return NULL;
    }

    lua_State* L = lua_newstate(lua_fmrb_alloc, ctx);
    if (!L) {
        FMRB_LOGE(TAG, "Failed to create Lua state for task %s", ctx->app_name);
        return NULL;
    }

    FMRB_LOGI(TAG, "Lua state created for task %s (pool=%d)",
              ctx->app_name, ctx->mempool_id);
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
