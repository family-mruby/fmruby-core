/**
 * @file fmrb_gfx_cmd.h
 * @brief Submission path for graphics commands built by the language bindings.
 *
 * Every language binding (mruby, Spinel, Python, Lua, BASIC) packs a gfx_cmd_t
 * and hands it to the host task. That hand-off is the same for all of them, so
 * it lives here instead of being copied per binding.
 */
#pragma once

#include "fmrb_err.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the host queue flow-control semaphore.
 *
 * The semaphore belongs to the host task, which creates it during its own
 * init and registers it here. Until then fmrb_gfx_submit() sends without back
 * pressure, so an early command cannot deadlock on a semaphore that does not
 * exist yet.
 *
 * @param sem Counting semaphore with one token per available GFX queue slot.
 */
void fmrb_gfx_set_flow_semaphore(fmrb_semaphore_t sem);

/**
 * @brief Hand one graphics command to the host task.
 *
 * Takes a flow-control token before queueing, so app drawing stays inside its
 * share of the host queue and HID events always have room. Blocking here is
 * the intended behaviour when an app draws faster than the graphics board can
 * consume; the host task returns the token once it has taken the command.
 *
 * @param cmd Command to send. Copied into the message payload.
 * @return FMRB_OK on success, FMRB_ERR_INVALID_STATE when called outside an
 *         app task, FMRB_ERR_TIMEOUT when the token or the queue times out.
 */
fmrb_err_t fmrb_gfx_submit(const gfx_cmd_t *cmd);

#ifdef __cplusplus
}
#endif
