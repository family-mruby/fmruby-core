// Measurement probe for the blue flash-write flicker (doc/flash_write_flicker/).
//
// Built only with -DFMRB_FLASH_PROBE=ON (P4). It hooks four places through
// linker --wrap, without touching ESP-IDF:
//
//  - spi_flash_disable_interrupts_caches_and_other_cpu / ..._enable_...:
//    the window in which the cache is suspended and non-IRAM interrupts are
//    off on both cores (every SPI1 flash erase/write without auto-suspend).
//  - esp_flash_erase_region / esp_flash_write: the flash operations behind a
//    file save, with their sizes and wall time.
//  - dw_gdma_channel_enable_ctrl: the DPI driver re-arms the scan-out DMA
//    from its "full transfer done" ISR once per frame, so the gap between two
//    calls is the time the DSI bridge went without a new frame.
//  - esp_intr_alloc: the DSI bridge interrupt is given to a shim that counts
//    the underrun bit before passing the call to the driver's own handler.
//
// Counters are read and logged from task context by flash_probe_log().

#include "flash_flicker_probe.h"

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "esp_flash.h"
#include "esp_private/esp_clk.h"
#include "soc/interrupts.h"
#include "hal/mipi_dsi_brg_ll.h"
#include "fmrb_log.h"

static const char *TAG = "flash_probe";

// ---- cache-off windows ------------------------------------------------------
typedef struct {
    uint32_t start_cyc;
    uint32_t depth;
    uint32_t count;
    uint64_t total_cyc;
    uint32_t max_cyc;
    uint32_t hist[5];   // <1ms, 1-5ms, 5-20ms, 20-50ms, >=50ms
} cache_win_t;

static DRAM_ATTR cache_win_t s_win;
static DRAM_ATTR uint32_t s_cyc_per_ms = 360000;

void __real_spi_flash_disable_interrupts_caches_and_other_cpu(void);
void __real_spi_flash_enable_interrupts_caches_and_other_cpu(void);

void IRAM_ATTR __wrap_spi_flash_disable_interrupts_caches_and_other_cpu(void)
{
    __real_spi_flash_disable_interrupts_caches_and_other_cpu();
    // After the lock: only one window can be open at a time.
    if (s_win.depth++ == 0) s_win.start_cyc = esp_cpu_get_cycle_count();
}

void IRAM_ATTR __wrap_spi_flash_enable_interrupts_caches_and_other_cpu(void)
{
    if (s_win.depth && --s_win.depth == 0) {
        uint32_t d = esp_cpu_get_cycle_count() - s_win.start_cyc;
        s_win.count++;
        s_win.total_cyc += d;
        if (d > s_win.max_cyc) s_win.max_cyc = d;
        uint32_t ms = d / s_cyc_per_ms;
        int b = ms < 1 ? 0 : ms < 5 ? 1 : ms < 20 ? 2 : ms < 50 ? 3 : 4;
        s_win.hist[b]++;
    }
    __real_spi_flash_enable_interrupts_caches_and_other_cpu();
}

// ---- flash operations --------------------------------------------------------
typedef struct {
    uint32_t count;
    uint64_t bytes;
    uint64_t total_us;
    uint32_t max_us;
} flash_op_t;

static flash_op_t s_erase, s_write;

static void note_op(flash_op_t *op, uint32_t len, int64_t t0)
{
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    op->count++;
    op->bytes += len;
    op->total_us += us;
    if (us > op->max_us) op->max_us = us;
}

esp_err_t __real_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len);
esp_err_t __real_esp_flash_write(esp_flash_t *chip, const void *buffer, uint32_t address, uint32_t length);

esp_err_t __wrap_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t r = __real_esp_flash_erase_region(chip, start, len);
    note_op(&s_erase, len, t0);
    return r;
}

esp_err_t __wrap_esp_flash_write(esp_flash_t *chip, const void *buffer, uint32_t address, uint32_t length)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t r = __real_esp_flash_write(chip, buffer, address, length);
    note_op(&s_write, length, t0);
    return r;
}

// ---- scan-out DMA re-arm (once per frame) ------------------------------------
typedef struct {
    uint32_t last_cyc;
    uint32_t frames;
    uint32_t max_gap_cyc;
    uint32_t late;          // gaps longer than 1.5 frames
    uint32_t lost_frames;   // sum over late gaps of (gap / frame - 1)
} scan_t;

static DRAM_ATTR scan_t s_scan;
static DRAM_ATTR uint32_t s_frame_cyc = 0;   // learned from the first gaps

struct dw_gdma_channel_t;
esp_err_t __real_dw_gdma_channel_enable_ctrl(struct dw_gdma_channel_t *chan, bool en_or_dis);

esp_err_t IRAM_ATTR __wrap_dw_gdma_channel_enable_ctrl(struct dw_gdma_channel_t *chan, bool en_or_dis)
{
    if (en_or_dis) {
        uint32_t now = esp_cpu_get_cycle_count();
        if (s_scan.frames) {
            uint32_t gap = now - s_scan.last_cyc;
            if (s_frame_cyc == 0 || gap < s_frame_cyc) s_frame_cyc = gap;   // shortest = one frame
            if (gap > s_scan.max_gap_cyc) s_scan.max_gap_cyc = gap;
            if (gap > s_frame_cyc + s_frame_cyc / 2) {
                s_scan.late++;
                s_scan.lost_frames += gap / s_frame_cyc - 1;
            }
        }
        s_scan.frames++;
        s_scan.last_cyc = now;
    }
    return __real_dw_gdma_channel_enable_ctrl(chan, en_or_dis);
}

// ---- DSI bridge interrupt: count underruns -------------------------------------
static DRAM_ATTR intr_handler_t s_brg_handler;
static DRAM_ATTR void *s_brg_arg;
static DRAM_ATTR uint32_t s_brg_underrun;
static DRAM_ATTR uint32_t s_brg_calls;

static void IRAM_ATTR brg_shim(void *arg)
{
    (void)arg;
    s_brg_calls++;
    if (MIPI_DSI_BRIDGE.int_st.val & MIPI_DSI_BRG_LL_EVENT_UNDERRUN) s_brg_underrun++;
    s_brg_handler(s_brg_arg);
}

esp_err_t __real_esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg, intr_handle_t *ret_handle);

esp_err_t __wrap_esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg, intr_handle_t *ret_handle)
{
    if (source == ETS_DSI_BRIDGE_INTR_SOURCE && handler && !s_brg_handler) {
        s_brg_handler = handler;
        s_brg_arg = arg;
        return __real_esp_intr_alloc(source, flags, brg_shim, NULL, ret_handle);
    }
    return __real_esp_intr_alloc(source, flags, handler, arg, ret_handle);
}

// ---- report --------------------------------------------------------------------
// Each line covers the interval since the previous call; the counters are
// cleared after printing (racy against the ISRs by a count at most, which is
// fine for a measurement). Intervals with only short flash reads and no late
// frame print nothing.
static uint32_t s_brg_underrun_seen;

void flash_probe_log(void)
{
    static bool s_said_hello;
    if (!s_said_hello && esp_flash_default_chip) {
        s_said_hello = true;
        uint32_t id = 0;
        esp_flash_read_id(esp_flash_default_chip, &id);
        FMRB_LOGI(TAG, "flash chip_id=0x%06lx read_id=0x%06lx size=%lu cpu=%luMHz",
                  (unsigned long)esp_flash_default_chip->chip_id, (unsigned long)id,
                  (unsigned long)esp_flash_default_chip->size,
                  (unsigned long)(esp_clk_cpu_freq() / 1000000));
    }
    s_cyc_per_ms = (uint32_t)(esp_clk_cpu_freq() / 1000);
    const uint32_t cpu = s_cyc_per_ms / 1000;   // cycles per us
    bool busy = s_erase.count || s_write.count || s_scan.late ||
                (s_win.hist[1] + s_win.hist[2] + s_win.hist[3] + s_win.hist[4]) ||
                s_brg_underrun != s_brg_underrun_seen;
    if (busy) {
        FMRB_LOGI(TAG, "cache-off n=%lu total=%luus max=%luus hist[<1,<5,<20,<50,>=50ms]=%lu/%lu/%lu/%lu/%lu",
                  (unsigned long)s_win.count, (unsigned long)(s_win.total_cyc / cpu),
                  (unsigned long)(s_win.max_cyc / cpu),
                  (unsigned long)s_win.hist[0], (unsigned long)s_win.hist[1], (unsigned long)s_win.hist[2],
                  (unsigned long)s_win.hist[3], (unsigned long)s_win.hist[4]);
        FMRB_LOGI(TAG, "erase n=%lu %lluB total=%lluus max=%luus | write n=%lu %lluB total=%lluus max=%luus",
                  (unsigned long)s_erase.count, (unsigned long long)s_erase.bytes,
                  (unsigned long long)s_erase.total_us, (unsigned long)s_erase.max_us,
                  (unsigned long)s_write.count, (unsigned long long)s_write.bytes,
                  (unsigned long long)s_write.total_us, (unsigned long)s_write.max_us);
        FMRB_LOGI(TAG, "scan frames=%lu frame=%luus late=%lu lost=%lu maxgap=%luus | brg irq=%lu underrun=%lu",
                  (unsigned long)s_scan.frames, (unsigned long)(s_frame_cyc / cpu),
                  (unsigned long)s_scan.late, (unsigned long)s_scan.lost_frames,
                  (unsigned long)(s_scan.max_gap_cyc / cpu),
                  (unsigned long)s_brg_calls, (unsigned long)s_brg_underrun);
        s_brg_underrun_seen = s_brg_underrun;
    }
    s_win.count = 0; s_win.total_cyc = 0; s_win.max_cyc = 0;
    for (int i = 0; i < 5; i++) s_win.hist[i] = 0;
    s_erase = (flash_op_t){0};
    s_write = (flash_op_t){0};
    s_scan.late = 0; s_scan.lost_frames = 0; s_scan.max_gap_cyc = 0;
}
