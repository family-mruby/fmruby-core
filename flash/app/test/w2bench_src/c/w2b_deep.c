/*
 * w2b_deep (C): recursion of growing depth, one depth per on_update
 * (25, 50, 100, 200, ...), logging each depth before it goes; the firmware
 * stops the app at the depth whose frames no longer fit (W2: which stack
 * runs out first, the interpreter's 16 KB or the app's own).
 */
#include <stdint.h>
#include "fmrb_boundary.h"
#include "fmrb_sdk_version.h"

FMRB_EVENT_BUFFER();
FMRB_SDK_VERSION_SECTION();

static int32_t depth = 25;

static int32_t len(const char *s) { int32_t n = 0; while (s[n]) n++; return n; }

__attribute__((noinline)) static int32_t down(int32_t n, int32_t a, int32_t b)
{
    if (n == 0) return a + b;
    return (down(n - 1, b, a + n) ^ n) * 3 + 1;
}

int32_t on_update(void)
{
    char line[40] = "W2B deep ";
    char t[12]; int k = 0, o = len(line);
    int32_t v = depth;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) line[o++] = t[--k];
    line[o] = 0;
    fmrb_log_info_raw(line, o);
    int32_t r = down(depth, 1, 2);
    if (r == 0) fmrb_log_info_raw("never", 5);
    depth *= 2;
    return 100;
}
