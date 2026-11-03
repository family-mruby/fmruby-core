// Moving an mruby app with mruby-checkpoint (doc/checkpoint): the glue between
// the app runtime (fmrb_app.c, the FmrbApp binding) and the gem. Only the
// builds whose libmruby carries the gem define FMRB_HAVE_CHECKPOINT (the Linux
// simulation and the native program); nothing here exists elsewhere.
#pragma once

#ifdef FMRB_HAVE_CHECKPOINT

#include <stddef.h>
#include <mruby.h>
#include "fmrb_app.h"

// The app's VM is open (create_vm_mruby). A movable app, and an image about
// to be restored, get the gem set up here, before any program code: the
// baseline, the work-area allocator, the embedder's section (the clock and
// the tick state), the handlers. Other VMs are left alone (they pay nothing).
void fmrb_app_checkpoint_vm_opened(fmrb_app_task_context_t *ctx);

// The VM is about to close (destroy_vm, both exit paths).
void fmrb_app_checkpoint_vm_closing(fmrb_app_task_context_t *ctx);

// A movable app's source as it is compiled (shown by the image inspector when
// an image carries it; mrb_checkpoint_add_source).
void fmrb_app_checkpoint_add_source(fmrb_app_task_context_t *ctx, const char *src, size_t len);

// The Task the program runs as (mrc_create_task), so that a restored image
// finds it again.
void fmrb_app_checkpoint_host_task(fmrb_app_task_context_t *ctx, mrb_value task);

// Restore the image at ctx->filepath and run it to the end (the app task of a
// FMRB_LOAD_MODE_CHECKPOINT spawn). Returns 0 with *task the program's Task
// (its value says how it ended, as after mrb_task_run), or -1 with the reason
// in err when the image cannot be restored.
int fmrb_app_checkpoint_resume(fmrb_app_task_context_t *ctx, mrb_value *task, char *err, size_t errlen);

// _spin got APP_CONTROL "checkpoint" (picoruby-fmrb-app). Returns true when
// the request is taken (the image is written at the next safe point, once
// _spin has returned), false when it is refused (the kernel is told why).
bool fmrb_app_checkpoint_request(mrb_state *mrb, mrb_value app);

// An app asks for the image of app pid (FmrbApp.request_checkpoint, the
// shell's `checkpoint`, an app asking for its own): the same way as the debug
// server (fmrb_app_checkpoint_ask), and the kernel answers reply_to with
// APP_CONTROL "checkpoint_result" {pid, ok, path | reason}. Returns NULL when
// the kernel was asked, or why the image cannot be asked for (that answer is
// not sent again).
const char *fmrb_app_checkpoint_ask_from_app(int32_t pid, bool restart, int32_t reply_to);

// Machine.board_millis's offset for this VM (D9): non-zero in a restored app,
// which goes on from the value it had where it was written.
int64_t fmrb_app_checkpoint_millis_offset(mrb_state *mrb);

#endif
