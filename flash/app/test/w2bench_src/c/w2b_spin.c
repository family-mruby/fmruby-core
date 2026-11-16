/*
 * w2b_spin (C): runs a loop that never ends in on_update, so the firmware's
 * instruction limit stops it (W2: how long the limit lets one call run,
 * read from the log's times of "W2B spin go" and the stop line).
 */
#include <stdint.h>
#include "fmrb_boundary.h"
#include "fmrb_sdk_version.h"

FMRB_EVENT_BUFFER();
FMRB_SDK_VERSION_SECTION();

volatile int32_t counter;

int32_t on_update(void)
{
    fmrb_log_info_raw("W2B spin go", 11);
    for (;;) counter++;
    return 100;
}
