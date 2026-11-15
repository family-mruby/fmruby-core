/*
 * launch_wasm.h - the page's door for opening an app by its path
 * (launch_wasm.c).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Take a launch the page asked for, if there is one, and answer it. Called
 * from the input poller task. */
void fmrb_wasm_launch_poll(void);

#ifdef __cplusplus
}
#endif
