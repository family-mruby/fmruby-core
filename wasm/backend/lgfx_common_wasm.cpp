/*
 * lgfx_common_wasm.cpp - LovyanGFX platform layer for the wasm build.
 *
 * The header side is the framebuffer platform's common.hpp (selected by the
 * FMRB_LGFX_WASM branch rake wasm:setup patches in); this supplies the
 * implementations without that platform's <linux/fb.h> baggage. Only what
 * sprite and font work reaches: the clock, the delays, and GPIO as no-ops.
 */

#include <lgfx/v1/platforms/framebuffer/common.hpp>

#include <sched.h>
#include <time.h>

namespace lgfx
{
 inline namespace v1
 {

  unsigned long millis(void)
  {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
  }

  unsigned long micros(void)
  {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
  }

  void delay(unsigned long milliseconds)
  {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (long)(milliseconds % 1000) * 1000000;
    nanosleep(&ts, nullptr);
  }

  void delayMicroseconds(unsigned int us)
  {
    auto start = micros();
    do
    {
      sched_yield();
    } while (micros() - start < us);
  }

  void gpio_hi(uint32_t) {}
  void gpio_lo(uint32_t) {}
  bool gpio_in(uint32_t) { return false; }
  void pinMode(int_fast16_t, pin_mode_t) {}
  void lgfxPinMode(int_fast16_t, pin_mode_t) {}

 }
}
