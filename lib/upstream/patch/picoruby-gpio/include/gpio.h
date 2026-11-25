#ifndef GPIO_DEFINED_H_
#define GPIO_DEFINED_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IN         0b0000001
#define OUT        0b0000010
#define HIGH_Z     0b0000100
#define PULL_UP    0b0001000
#define PULL_DOWN  0b0010000
#define OPEN_DRAIN 0b0100000
#define ALT        0b1000000

/*
 * Family mruby: a port may refuse a pin (the board's pin table). Before
 * setting a pin up or writing it, the gem asks GPIO_claim; GPIO_read returns
 * -1 when the level cannot be read. Both raise in Ruby (src/mruby/gpio.c).
 */
#define GPIO_USE_READ    0  /* read the level */
#define GPIO_USE_INPUT   1  /* input direction, pull-up / pull-down */
#define GPIO_USE_OUTPUT  2  /* output direction, write, open drain, ALT */

#define GPIO_CLAIM_OK          0  /* the app owns the pin */
#define GPIO_CLAIM_OK_SHARED   1  /* allowed without owning it (a read, a button as input) */
#define GPIO_CLAIM_NO_RANGE    2  /* no such pin */
#define GPIO_CLAIM_NO_SYSTEM   3  /* a pin the system uses */
#define GPIO_CLAIM_INPUT_ONLY  4  /* a system button: input only */
#define GPIO_CLAIM_IN_USE      5  /* held by another app or for another use */
#define GPIO_CLAIM_FAILED      6  /* granted, but setting it up failed */

int GPIO_pin_num_from_char(const uint8_t *);

/* use: GPIO_USE_*. reset: put the pin back to its default state when the
 * app takes it now (GPIO.new). Returns GPIO_CLAIM_*. */
int GPIO_claim(int pin, int use, int reset);
/* Not called by the gem any more (GPIO_claim resets the pin it takes); kept
 * for the upstream ports, which still define it. */
void GPIO_init(uint8_t);
void GPIO_set_dir(uint8_t, uint8_t);
void GPIO_pull_up(uint8_t);
void GPIO_pull_down(uint8_t);
void GPIO_open_drain(uint8_t);
int GPIO_read(uint8_t);
void GPIO_write(uint8_t, uint8_t);
void GPIO_set_function(uint8_t, uint8_t);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_DEFINED_H_ */
