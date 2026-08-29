/*
 * Heap for the wasm FreeRTOS build.
 *
 * ESP-IDF puts the kernel's allocations on heap_caps (heap_idf.c); a wasm module
 * has one linear memory and one malloc, so both pvPortMalloc() and
 * heap_caps_malloc() land there. The only thing worth keeping is the running
 * total, so xPortGetFreeHeapSize() and heap_caps_get_free_size() can answer with
 * something meaningful for the M1| memory snapshots the firmware logs.
 *
 * "Free" here means "of the budget below, still unspent". wasm memory grows on
 * demand, so there is no real free-size to report; the budget is a stated
 * ceiling that makes the numbers comparable across runs.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "esp_heap_caps.h"

#ifndef portWASM_HEAP_BUDGET_BYTES
    #define portWASM_HEAP_BUDGET_BYTES    ( 64u * 1024u * 1024u )
#endif

/* Every block carries its size so the accounting survives free(). */
typedef struct
{
    size_t xSize;
    size_t xPadding; /* keeps the returned pointer 16-byte aligned */
} BlockHeader_t;

static size_t xBytesInUse;
static size_t xPeakBytesInUse;

static void * prvAlloc( size_t xWantedSize )
{
    BlockHeader_t * pxHeader;

    if( xWantedSize == 0 )
    {
        return NULL;
    }

    pxHeader = malloc( xWantedSize + sizeof( BlockHeader_t ) );

    if( pxHeader == NULL )
    {
        return NULL;
    }

    pxHeader->xSize = xWantedSize;
    xBytesInUse += xWantedSize;

    if( xBytesInUse > xPeakBytesInUse )
    {
        xPeakBytesInUse = xBytesInUse;
    }

    return ( void * ) ( pxHeader + 1 );
}

static void prvFree( void * pv )
{
    BlockHeader_t * pxHeader;

    if( pv == NULL )
    {
        return;
    }

    pxHeader = ( ( BlockHeader_t * ) pv ) - 1;
    xBytesInUse -= pxHeader->xSize;
    free( pxHeader );
}

/* ------------------------------------------------------- FreeRTOS heap API */

void * pvPortMalloc( size_t xWantedSize )
{
    return prvAlloc( xWantedSize );
}

void vPortFree( void * pv )
{
    prvFree( pv );
}

void * pvPortCalloc( size_t xNum,
                     size_t xSize )
{
    void * pv = prvAlloc( xNum * xSize );

    if( pv != NULL )
    {
        memset( pv, 0, xNum * xSize );
    }

    return pv;
}

void vPortInitialiseBlocks( void )
{
}

size_t xPortGetFreeHeapSize( void )
{
    return ( xBytesInUse < portWASM_HEAP_BUDGET_BYTES )
           ? ( portWASM_HEAP_BUDGET_BYTES - xBytesInUse )
           : 0u;
}

size_t xPortGetMinimumEverFreeHeapSize( void )
{
    return ( xPeakBytesInUse < portWASM_HEAP_BUDGET_BYTES )
           ? ( portWASM_HEAP_BUDGET_BYTES - xPeakBytesInUse )
           : 0u;
}

/* --------------------------------------------------------- heap_caps stubs */

void * heap_caps_malloc( size_t size,
                         uint32_t caps )
{
    ( void ) caps;
    return prvAlloc( size );
}

void * heap_caps_calloc( size_t n,
                         size_t size,
                         uint32_t caps )
{
    ( void ) caps;
    return pvPortCalloc( n, size );
}

void heap_caps_free( void * ptr )
{
    prvFree( ptr );
}

size_t heap_caps_get_free_size( uint32_t caps )
{
    ( void ) caps;
    return xPortGetFreeHeapSize();
}

size_t heap_caps_get_largest_free_block( uint32_t caps )
{
    ( void ) caps;
    return xPortGetFreeHeapSize();
}
