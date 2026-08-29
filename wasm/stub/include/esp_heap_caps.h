/*
 * Stub of ESP-IDF's esp_heap_caps.h for the wasm target.
 *
 * There is one heap in a wasm module, so every capability bit collapses to the
 * same malloc. The bits are kept only so callers that ask for internal/8-bit
 * memory still compile; heap_caps_get_free_size() answers from the same
 * accounting the port keeps for xPortGetFreeHeapSize().
 */
#pragma once

#include "esp_attr.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOC_CAP_EXEC        ( 1 << 0 )
#define MALLOC_CAP_32BIT       ( 1 << 1 )
#define MALLOC_CAP_8BIT        ( 1 << 2 )
#define MALLOC_CAP_DMA         ( 1 << 3 )
#define MALLOC_CAP_SPIRAM      ( 1 << 10 )
#define MALLOC_CAP_INTERNAL    ( 1 << 11 )
#define MALLOC_CAP_DEFAULT     ( 1 << 12 )

void * heap_caps_malloc( size_t size, uint32_t caps );
void * heap_caps_aligned_alloc( size_t alignment, size_t size, uint32_t caps );
void * heap_caps_calloc( size_t n, size_t size, uint32_t caps );
void heap_caps_free( void * ptr );
size_t heap_caps_get_free_size( uint32_t caps );
size_t heap_caps_get_largest_free_block( uint32_t caps );

#ifdef __cplusplus
}
#endif
