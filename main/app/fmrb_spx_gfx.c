/**
 * @file fmrb_spx_gfx.c
 * @brief Implementation of the Spinel FmrbGfx FFI shim (fmrb_spx_gfx.h): the
 *        drawer, which the app boundary (fmrb_boundary_gfx.c) does not carry,
 *        and the generated adaptors of its record and byte results.
 *
 * The receiver arrives as a plain int: the number of the FmrbGfx object's
 * drawer (fmrb_gfx_drawer.h), which has the canvas and the drawing origin, as
 * for the boundary's functions (doc/app_boundary W4b).
 */
#include "fmrb_spx_gfx.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fmrb_app.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_err.h"
#include "fmrb_msg.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_hal.h"
#include "fmrb_task_config.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "fmrb_file_transfer_msg.h"
#include "fmrb_kernel.h"
#include "fmrb_hal_time.h"
#include "fmrb_attr.h"
#include "fmrb_gfx_drawer.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif

/* The current instance's :binstr length (runtime sp_ctx.c; the host C
   cannot include sp_ctx.h, so it is declared here). */
int *sp_ctx_ffi_bin_len(void);

/* ---- the drawer (FmrbGfx.new, _set_origin, destroy) ---------------------- */

int fmrb_spx_gfx_drawer_open(int canvas_id, int width, int height)
{
    return (int)fmrb_gfx_drawer_open(canvas_id, width, height);
}

int fmrb_spx_gfx_drawer_set_origin(int drawer, int x, int y, int w, int h)
{
    return (int)fmrb_gfx_drawer_set_origin(drawer, x, y, w, h);
}

int fmrb_spx_gfx_drawer_close(int drawer)
{
    fmrb_gfx_drawer_close(drawer);
    return 0;
}

/* The adaptors of the boundary functions that give back a record or bytes
   (generated from sig/, doc/app_boundary): Spinel takes those as :binstr. */
#include "fmrb_gen/fmrb_boundary_spx.inc"
