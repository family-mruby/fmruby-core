#include "fmrb_gfx.h"
#include "fmrb_hal.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "fmrb_gfx";

// Global graphics context (shared across all FmrbGfx instances)
static fmrb_gfx_context_impl_t *g_gfx_context = NULL;
static fmrb_gfx_context_impl_t g_gfx_context_body;

// Helper function to check if point is within clip rectangle
static bool is_clipped(fmrb_gfx_context_impl_t *ctx, int16_t x, int16_t y) {
    if (!ctx->clip_enabled) {
        return false;
    }

    return (x < ctx->clip_rect.x ||
            y < ctx->clip_rect.y ||
            x >= ctx->clip_rect.x + ctx->clip_rect.width ||
            y >= ctx->clip_rect.y + ctx->clip_rect.height);
}

// Helper: send sync GFX command via Host Task queue.
// Builds a gfx_cmd_t with sync context, sends to Host Task, blocks until response.
static fmrb_gfx_err_t send_gfx_sync_via_host(
    gfx_cmd_t *cmd,
    uint8_t *response_data,
    uint16_t response_buf_size,
    uint32_t timeout_ms)
{
    // Sync context on caller's stack (valid while we block)
    gfx_cmd_sync_ctx_t sc;
    sc.done = fmrb_semaphore_create_binary();
    if (!sc.done) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }
    sc.response_buf = response_data;
    sc.response_len = response_buf_size;
    sc.result = -1;

    cmd->sync = &sc;

    // Metered like any other app command: these run on the app's task, so they
    // belong inside its share of the host queue.
    fmrb_err_t ret = fmrb_gfx_submit(cmd);
    if (ret != FMRB_OK) {
        fmrb_semaphore_delete(sc.done);
        return FMRB_GFX_ERR_FAILED;
    }

    // Block calling task until Host Task signals completion. sc lives on this
    // stack and the host task writes through it, so the forced kill must not
    // delete us while we are in here.
    fmrb_tick_t ticks = (timeout_ms == UINT32_MAX) ? FMRB_TICK_MAX : FMRB_MS_TO_TICKS(timeout_ms);
    fmrb_app_sync_io_begin();
    fmrb_base_type_t wait_result = fmrb_semaphore_take(sc.done, ticks);
    fmrb_app_sync_io_end();
    fmrb_semaphore_delete(sc.done);

    if (wait_result != FMRB_TRUE) {
        ESP_LOGW(TAG, "send_gfx_sync_via_host: timeout");
        return FMRB_GFX_ERR_FAILED;
    }

    return (sc.result == 0) ? FMRB_GFX_OK : FMRB_GFX_ERR_FAILED;
}

fmrb_gfx_err_t fmrb_gfx_init(const fmrb_gfx_config_t *config) {
    if (!config) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    // If global context already exists, prevent double initialization
    if (g_gfx_context != NULL) {
        ESP_LOGW(TAG, "Graphics context already initialized, reusing existing context");
        return FMRB_GFX_OK;
    }

    fmrb_gfx_context_impl_t *ctx = &g_gfx_context_body;
    memset(ctx, 0, sizeof(fmrb_gfx_context_impl_t));
    ctx->config = *config;

    // TODO: Initialize IPC transport for graphics (singleton, no handle needed)
    // fmrb_transport_config_t transport_config = {
    //     .timeout_ms = 1000,
    //     .enable_retransmit = true,
    //     .max_retries = 3,
    //     .window_size = 8
    // };

    ctx->initialized = true;
    ctx->current_target = FMRB_CANVAS_SCREEN;  // Default to main screen
    ctx->next_canvas_id = 1;  // Start canvas IDs from 1

    // Store as global context
    g_gfx_context = ctx;

    ESP_LOGI(TAG, "Graphics initialized: %dx%d, %d bpp", config->screen_width, config->screen_height, config->bits_per_pixel);
    ESP_LOGI(TAG, "init: g_gfx_context=%p, initialized=%d", g_gfx_context, ctx->initialized);

    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_deinit(void) {
    // Only deinitialize if this is the global context
    if (g_gfx_context != NULL) {
        // Deinitialize singleton transport
        fmrb_transport_deinit();

        g_gfx_context->initialized = false;
        g_gfx_context = NULL;

        ESP_LOGI(TAG, "Graphics deinitialized");
    } else {
        ESP_LOGW(TAG, "Attempted to deinit NULL global context, ignoring");
    }

    return FMRB_GFX_OK;
}

fmrb_gfx_context_t fmrb_gfx_get_global_context(void) {
    return g_gfx_context;
}


// Canvas management API implementations

fmrb_gfx_err_t fmrb_gfx_create_canvas(
    fmrb_gfx_context_t context,
    int32_t width, int32_t height,
    int16_t z_order,
    bool use_transparent,
    uint8_t transparent_color,
    fmrb_canvas_handle_t *canvas_handle)
{
    if (!context || !canvas_handle || width <= 0 || height <= 0) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_CREATE_CANVAS;
    cmd.params.create_canvas.width = width;
    cmd.params.create_canvas.height = height;
    cmd.params.create_canvas.z_order = z_order;
    cmd.params.create_canvas.use_transparent = use_transparent ? 1 : 0;
    cmd.params.create_canvas.transparent_color = transparent_color;

    uint8_t response_data[sizeof(uint16_t)];
    fmrb_gfx_err_t ret = send_gfx_sync_via_host(&cmd, response_data, sizeof(response_data), 1000);

    if (ret == FMRB_GFX_OK) {
        uint16_t canvas_id;
        memcpy(&canvas_id, response_data, sizeof(uint16_t));
        *canvas_handle = canvas_id;
        ESP_LOGI(TAG, "Canvas created: ID=%u, %dx%d", canvas_id, width, height);
    } else {
        ESP_LOGE(TAG, "Failed to create canvas: %dx%d, error=%d", width, height, ret);
    }

    return ret;
}

fmrb_gfx_err_t fmrb_gfx_delete_canvas(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle)
{
    if (!context || canvas_handle == FMRB_CANVAS_SCREEN || canvas_handle == FMRB_CANVAS_INVALID) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // If deleting current target, switch back to screen
    if (ctx->current_target == canvas_handle) {
        ctx->current_target = FMRB_CANVAS_SCREEN;
    }

    // Route through host_task batch queue so the delete stays in order with
    // any pending draws / sprite ops that reference this canvas.
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_DELETE_CANVAS;
    cmd.canvas_id = canvas_handle;

    // Unmetered on purpose: this also runs from the kernel's reap and from the
    // forced-kill path, where blocking on a semaphore that app drawing refills
    // is not acceptable. The marker keeps the host's give side balanced.
    fmrb_err_t send_ret = fmrb_gfx_submit_unmetered(&cmd);
    if (send_ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to queue delete_canvas: %d", send_ret);
        return FMRB_GFX_ERR_FAILED;
    }
    ESP_LOGI(TAG, "Canvas delete queued: ID=%u", canvas_handle);
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_set_composite_regions(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle,
    const fmrb_gfx_composite_region_t *regions,
    uint8_t count)
{
    if (!context) return FMRB_GFX_ERR_INVALID_PARAM;
    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return FMRB_GFX_ERR_NOT_INITIALIZED;
    if (canvas_handle == FMRB_CANVAS_SCREEN || canvas_handle == FMRB_CANVAS_INVALID) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }
    if (count > FMRB_GFX_MAX_COMPOSITE_REGIONS) {
        ESP_LOGW(TAG, "set_composite_regions: count %u exceeds max %u, clipping",
                 count, (unsigned)FMRB_GFX_MAX_COMPOSITE_REGIONS);
        count = FMRB_GFX_MAX_COMPOSITE_REGIONS;
    }
    if (count > 0 && !regions) return FMRB_GFX_ERR_INVALID_PARAM;

    // Route through host_task batch queue so the region update stays in order
    // with surrounding draws / present commands on the same canvas.
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_SET_COMPOSITE_REGIONS;
    cmd.canvas_id = canvas_handle;
    cmd.params.set_composite_regions.count = count;
    if (count > 0) {
        memcpy(cmd.params.set_composite_regions.regions, regions,
               (size_t)count * sizeof(fmrb_gfx_composite_region_t));
    }

    fmrb_err_t send_ret = fmrb_gfx_submit(&cmd);
    if (send_ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to queue set_composite_regions: %d", send_ret);
        return FMRB_GFX_ERR_FAILED;
    }
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_set_canvas_viewport(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle,
    uint16_t src_x, uint16_t src_y,
    uint16_t view_w, uint16_t view_h)
{
    if (!context) return FMRB_GFX_ERR_INVALID_PARAM;
    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return FMRB_GFX_ERR_NOT_INITIALIZED;
    if (canvas_handle == FMRB_CANVAS_SCREEN || canvas_handle == FMRB_CANVAS_INVALID) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    // Route through host_task batch queue so the viewport update stays in
    // order with surrounding draws / present commands on the same canvas.
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_SET_CANVAS_VIEWPORT;
    cmd.canvas_id = canvas_handle;
    cmd.params.set_canvas_viewport.src_x = src_x;
    cmd.params.set_canvas_viewport.src_y = src_y;
    cmd.params.set_canvas_viewport.view_w = view_w;
    cmd.params.set_canvas_viewport.view_h = view_h;

    fmrb_err_t send_ret = fmrb_gfx_submit(&cmd);
    if (send_ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to queue set_canvas_viewport: %d", send_ret);
        return FMRB_GFX_ERR_FAILED;
    }
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_set_sprite_clip(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle,
    uint16_t x, uint16_t y,
    uint16_t w, uint16_t h)
{
    if (!context) return FMRB_GFX_ERR_INVALID_PARAM;
    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return FMRB_GFX_ERR_NOT_INITIALIZED;
    if (canvas_handle == FMRB_CANVAS_SCREEN || canvas_handle == FMRB_CANVAS_INVALID) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    // Route through host_task batch queue so the clip update stays in order
    // with surrounding draws / present commands on the same canvas.
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_SET_SPRITE_CLIP;
    cmd.canvas_id = canvas_handle;
    cmd.params.set_sprite_clip.x = x;
    cmd.params.set_sprite_clip.y = y;
    cmd.params.set_sprite_clip.w = w;
    cmd.params.set_sprite_clip.h = h;

    fmrb_err_t send_ret = fmrb_gfx_submit(&cmd);
    if (send_ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to queue set_sprite_clip: %d", send_ret);
        return FMRB_GFX_ERR_FAILED;
    }
    return FMRB_GFX_OK;
}

// Sprite API implementations

uint16_t fmrb_gfx_create_sprite_image(
    fmrb_gfx_context_t context,
    uint16_t canvas_id,
    uint16_t width, uint16_t height,
    uint8_t transparent_color, bool use_transparent)
{
    if (!context || width == 0 || height == 0) return 0;
    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return 0;

    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_CREATE_SPRITE_IMAGE;
    cmd.canvas_id = canvas_id;
    cmd.params.create_sprite_image.width = width;
    cmd.params.create_sprite_image.height = height;
    cmd.params.create_sprite_image.transparent_color = transparent_color;
    cmd.params.create_sprite_image.use_transparent = use_transparent ? 1 : 0;

    uint8_t response_data[sizeof(uint16_t)];
    fmrb_gfx_err_t ret = send_gfx_sync_via_host(&cmd, response_data, sizeof(response_data), 1000);

    if (ret == FMRB_GFX_OK) {
        uint16_t image_id;
        memcpy(&image_id, response_data, sizeof(uint16_t));
        ESP_LOGI(TAG, "Sprite image created: id=%u, %ux%u", image_id, width, height);
        return image_id;
    }

    ESP_LOGE(TAG, "Failed to create sprite image: %ux%u, err=%d", width, height, ret);
    return 0;
}

uint16_t fmrb_gfx_create_sprite_instance(
    fmrb_gfx_context_t context,
    uint16_t canvas_id,
    const uint16_t *image_ids, uint8_t frame_count,
    int16_t x, int16_t y, int16_t z_order)
{
    if (!context || !image_ids || frame_count == 0 || frame_count > FMRB_SPRITE_MAX_FRAMES) return 0;
    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return 0;

    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_CREATE_SPRITE_INSTANCE;
    cmd.canvas_id = canvas_id;
    cmd.params.create_sprite_instance.frame_count = frame_count;
    memcpy(cmd.params.create_sprite_instance.image_ids, image_ids, sizeof(uint16_t) * frame_count);
    cmd.params.create_sprite_instance.x = x;
    cmd.params.create_sprite_instance.y = y;
    cmd.params.create_sprite_instance.z_order = z_order;

    uint8_t response_data[sizeof(uint16_t)];
    fmrb_gfx_err_t ret = send_gfx_sync_via_host(&cmd, response_data, sizeof(response_data), 1000);

    if (ret == FMRB_GFX_OK) {
        uint16_t instance_id;
        memcpy(&instance_id, response_data, sizeof(uint16_t));
        ESP_LOGD(TAG, "Sprite instance created: id=%u", instance_id);
        return instance_id;
    }

    ESP_LOGE(TAG, "Failed to create sprite instance, err=%d", ret);
    return 0;
}

// ---------- GfxBlock VM ----------

// Maximum DEFINE_PROG payload that reliably fits in one UART frame without
// relying on transport-layer fragmentation (which is untested and blocks the
// Host Task during reassembly). Derived from FMRB_LINK_FRAME_MAX_DATA(248)
// minus msgpack encoding overhead (~12B) and COBS worst case (~3B).
#define FMRB_GFX_DEFINE_PROG_SINGLE_FRAME_LIMIT 220

fmrb_gfx_err_t fmrb_gfx_define_prog(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_id,
    const uint8_t *bytecode, uint16_t bytecode_len,
    const uint8_t *strtable, uint16_t strtable_len,
    uint8_t *out_prog_id)
{
    if (!context || !out_prog_id) return FMRB_GFX_ERR_INVALID_PARAM;
    if (bytecode_len > 0 && !bytecode) return FMRB_GFX_ERR_INVALID_PARAM;
    if (strtable_len > 0 && !strtable) return FMRB_GFX_ERR_INVALID_PARAM;

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return FMRB_GFX_ERR_NOT_INITIALIZED;

    // Reject programs that would require transport-layer fragmentation.
    size_t total = sizeof(fmrb_link_graphics_define_prog_t)
                 + (size_t)bytecode_len + (size_t)strtable_len;
    if (total > FMRB_GFX_DEFINE_PROG_SINGLE_FRAME_LIMIT) {
        ESP_LOGE(TAG, "define_prog: payload %zu bytes exceeds single-frame limit %d",
                 total, FMRB_GFX_DEFINE_PROG_SINGLE_FRAME_LIMIT);
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    // Caller blocks on semaphore, so its buffers stay valid while Host Task reads them.
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_DEFINE_PROG;
    cmd.canvas_id = canvas_id;
    cmd.params.define_prog.bytecode_buf = (uint8_t *)bytecode;
    cmd.params.define_prog.bytecode_len = bytecode_len;
    cmd.params.define_prog.strtable_buf = (uint8_t *)strtable;
    cmd.params.define_prog.strtable_len = strtable_len;

    uint8_t response[1] = { FMRB_GFX_VM_INVALID_PROG_ID };
    fmrb_gfx_err_t ret = send_gfx_sync_via_host(&cmd, response, sizeof(response), 2000);
    if (ret != FMRB_GFX_OK) {
        ESP_LOGE(TAG, "define_prog: send failed: %d", ret);
        return ret;
    }
    *out_prog_id = response[0];
    if (response[0] == FMRB_GFX_VM_INVALID_PROG_ID) {
        ESP_LOGE(TAG, "define_prog: WROVER returned INVALID_PROG_ID (pool full?)");
        return FMRB_GFX_ERR_NO_MEMORY;
    }
    ESP_LOGD(TAG, "define_prog: canvas=%u prog_id=%u bc=%u st=%u",
             canvas_id, response[0], bytecode_len, strtable_len);
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_exec_prog(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_id,
    uint8_t prog_id,
    const uint8_t *reg_updates,
    uint8_t reg_count)
{
    if (!context) return FMRB_GFX_ERR_INVALID_PARAM;
    if (reg_count > 0 && !reg_updates) return FMRB_GFX_ERR_INVALID_PARAM;
    if (reg_count > 16) return FMRB_GFX_ERR_INVALID_PARAM;

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return FMRB_GFX_ERR_NOT_INITIALIZED;

    // Route through host_task queue so EXEC_PROG stays in order with any
    // surrounding gfx_cmd_t-based draw commands from the same app task.
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_EXEC_PROG;
    cmd.canvas_id = canvas_id;
    cmd.params.exec_prog.prog_id = prog_id;
    cmd.params.exec_prog.reg_count = reg_count;
    if (reg_count > 0) {
        memcpy(cmd.params.exec_prog.reg_updates, reg_updates, (size_t)reg_count * 3);
    }

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    return (ret == FMRB_OK) ? FMRB_GFX_OK : FMRB_GFX_ERR_FAILED;
}

fmrb_gfx_err_t fmrb_gfx_delete_prog(
    fmrb_gfx_context_t context,
    uint8_t prog_id)
{
    if (!context) return FMRB_GFX_ERR_INVALID_PARAM;
    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) return FMRB_GFX_ERR_NOT_INITIALIZED;

    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_DELETE_PROG;
    cmd.params.delete_prog.prog_id = prog_id;

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    return (ret == FMRB_OK) ? FMRB_GFX_OK : FMRB_GFX_ERR_FAILED;
}
