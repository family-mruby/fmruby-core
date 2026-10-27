// Host-test stand-in for fmrb_rtos.h (test/native/file_hal.c): the file
// HAL's locking and task identity without FreeRTOS. One thread, so the
// mutex is a no-op that always succeeds.
#pragma once

#include <stdint.h>

typedef void *fmrb_semaphore_t;
typedef struct { int unused; } fmrb_static_semaphore_t;
typedef uint32_t fmrb_tick_t;

#define FMRB_TRUE 1
#define FMRB_TICK_MAX 0xffffffffu
#define FMRB_MS_TO_TICKS(ms) ((fmrb_tick_t)(ms))

static inline fmrb_semaphore_t fmrb_semaphore_create_mutex(void) { return (fmrb_semaphore_t)1; }
static inline fmrb_semaphore_t fmrb_semaphore_create_mutex_static(fmrb_static_semaphore_t *b) { return (fmrb_semaphore_t)b; }
static inline void fmrb_semaphore_delete(fmrb_semaphore_t s) { (void)s; }
static inline int fmrb_semaphore_take(fmrb_semaphore_t s, fmrb_tick_t t) { (void)s; (void)t; return FMRB_TRUE; }
static inline int fmrb_semaphore_give(fmrb_semaphore_t s) { (void)s; return FMRB_TRUE; }
static inline void *fmrb_task_get_current(void) { return (void *)0; }
