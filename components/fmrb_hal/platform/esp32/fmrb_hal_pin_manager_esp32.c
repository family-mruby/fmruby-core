#include "fmrb_hal_pin_manager.h"
#include "fmrb_pin_assign.h"
#include "fmrb_log.h"
#include "fmrb_attr.h"
#include <string.h>

static const char *TAG = "pin_manager";

// ---- The pins GPIO must leave alone (fmrb_pin_manager_is_gpio_reserved) ----
//
// Every pin the board's system uses, from fmrb_pin_assign.h, folded into one
// 64-bit constant at build time (no RAM). It covers the table's system pins
// and the ones the table does not hold (SDMMC, SDIO, I2S, the system I2C ...).
// The buttons and the pins left to apps (Grove = I2C2, headers) stay out.
// NC (-1) adds nothing.
#define PIN_BIT(p) (((int)(p) >= 0 && (int)(p) < 64) ? (1ULL << ((unsigned)(p) & 63u)) : 0ULL)

#define RESERVED_CHIP \
    (PIN_BIT(FMRB_PIN_RESTRICTED_BOOT) | PIN_BIT(FMRB_PIN_RESTRICTED_JTAG) | \
     PIN_BIT(FMRB_PIN_RESTRICTED_USB_DN) | PIN_BIT(FMRB_PIN_RESTRICTED_USB_DP) | \
     PIN_BIT(FMRB_PIN_RESTRICTED_PSRAM0) | PIN_BIT(FMRB_PIN_RESTRICTED_PSRAM1) | \
     PIN_BIT(FMRB_PIN_RESTRICTED_PSRAM2) | PIN_BIT(FMRB_PIN_RESTRICTED_STRAP1) | \
     PIN_BIT(FMRB_PIN_RESTRICTED_STRAP2))

#define RESERVED_BOARD \
    (PIN_BIT(FMRB_PIN_SD_CS) | PIN_BIT(FMRB_PIN_SD_MOSI) | PIN_BIT(FMRB_PIN_SD_SCLK) | \
     PIN_BIT(FMRB_PIN_SD_MISO) | PIN_BIT(FMRB_PIN_SD_DETECT) | \
     PIN_BIT(FMRB_PIN_GFX_SPI_MOSI) | PIN_BIT(FMRB_PIN_GFX_SPI_MISO) | \
     PIN_BIT(FMRB_PIN_GFX_SPI_SCLK) | PIN_BIT(FMRB_PIN_GFX_SPI_CS) | \
     PIN_BIT(FMRB_PIN_GFX_SPI_INTR) | \
     PIN_BIT(FMRB_PIN_USB_POWER) | PIN_BIT(FMRB_PIN_STATUS_LED) | \
     PIN_BIT(FMRB_PIN_ERROR_LED) | PIN_BIT(FMRB_PIN_WROVER_RESET) | \
     PIN_BIT(FMRB_PIN_I2C1_SDA) | PIN_BIT(FMRB_PIN_I2C1_SCL))

#ifdef FMRB_PIN_GFX_UART_TX
#define RESERVED_LINK_UART \
    (PIN_BIT(FMRB_PIN_GFX_UART_TX) | PIN_BIT(FMRB_PIN_GFX_UART_RX) | \
     PIN_BIT(FMRB_PIN_GFX_UART_RTS) | PIN_BIT(FMRB_PIN_GFX_UART_CTS))
#else
#define RESERVED_LINK_UART 0ULL
#endif

#ifdef FMRB_SD_HOST_SDMMC
#define RESERVED_SDMMC \
    (PIN_BIT(FMRB_PIN_SD_CLK) | PIN_BIT(FMRB_PIN_SD_CMD) | PIN_BIT(FMRB_PIN_SD_D0) | \
     PIN_BIT(FMRB_PIN_SD_D1) | PIN_BIT(FMRB_PIN_SD_D2) | PIN_BIT(FMRB_PIN_SD_D3))
#else
#define RESERVED_SDMMC 0ULL
#endif

#ifdef FMRB_PIN_LCD_SPI_MOSI
#define RESERVED_LCD_SPI \
    (PIN_BIT(FMRB_PIN_LCD_SPI_MOSI) | PIN_BIT(FMRB_PIN_LCD_SPI_SCLK) | \
     PIN_BIT(FMRB_PIN_LCD_SPI_CS) | PIN_BIT(FMRB_PIN_LCD_SPI_DC) | \
     PIN_BIT(FMRB_PIN_LCD_SPI_RST))
#else
#define RESERVED_LCD_SPI 0ULL
#endif

#ifdef FMRB_PIN_LCD_BACKLIGHT
#define RESERVED_BACKLIGHT PIN_BIT(FMRB_PIN_LCD_BACKLIGHT)
#else
#define RESERVED_BACKLIGHT 0ULL
#endif

#ifdef FMRB_PIN_KBD_I2C_SDA
#define RESERVED_INPUT_DEVICES \
    (PIN_BIT(FMRB_PIN_KBD_I2C_SDA) | PIN_BIT(FMRB_PIN_KBD_I2C_SCL) | \
     PIN_BIT(FMRB_PIN_KBD_INT) | PIN_BIT(FMRB_PIN_TOUCH_I2C_SDA) | \
     PIN_BIT(FMRB_PIN_TOUCH_I2C_SCL) | PIN_BIT(FMRB_PIN_TOUCH_INT))
#else
#define RESERVED_INPUT_DEVICES 0ULL
#endif

#ifdef FMRB_PIN_I2S_MCLK
#define RESERVED_I2S \
    (PIN_BIT(FMRB_PIN_I2S_MCLK) | PIN_BIT(FMRB_PIN_I2S_SCLK) | PIN_BIT(FMRB_PIN_I2S_LRCK) | \
     PIN_BIT(FMRB_PIN_I2S_DOUT) | PIN_BIT(FMRB_PIN_I2S_DIN))
#else
#define RESERVED_I2S 0ULL
#endif

#ifdef FMRB_PIN_AUDIO_AMP_EN
#define RESERVED_AMP PIN_BIT(FMRB_PIN_AUDIO_AMP_EN)
#else
#define RESERVED_AMP 0ULL
#endif

#ifdef FMRB_PIN_SDIO_CLK
#define RESERVED_SDIO \
    (PIN_BIT(FMRB_PIN_SDIO_CLK) | PIN_BIT(FMRB_PIN_SDIO_CMD) | PIN_BIT(FMRB_PIN_SDIO_D0) | \
     PIN_BIT(FMRB_PIN_SDIO_D1) | PIN_BIT(FMRB_PIN_SDIO_D2) | PIN_BIT(FMRB_PIN_SDIO_D3))
#else
#define RESERVED_SDIO 0ULL
#endif

#ifdef FMRB_PIN_RADIO_EN
#define RESERVED_RADIO_EN PIN_BIT(FMRB_PIN_RADIO_EN)
#else
#define RESERVED_RADIO_EN 0ULL
#endif

#ifdef FMRB_PIN_ETH_RMII_0
#define RESERVED_ETH \
    (PIN_BIT(FMRB_PIN_ETH_RMII_0) | PIN_BIT(FMRB_PIN_ETH_RMII_1) | \
     PIN_BIT(FMRB_PIN_ETH_RMII_2) | PIN_BIT(FMRB_PIN_ETH_RMII_3) | \
     PIN_BIT(FMRB_PIN_ETH_RMII_4) | PIN_BIT(FMRB_PIN_ETH_RMII_5) | \
     PIN_BIT(FMRB_PIN_ETH_RMII_6) | PIN_BIT(FMRB_PIN_ETH_RMII_7) | \
     PIN_BIT(FMRB_PIN_ETH_RMII_8))
#else
#define RESERVED_ETH 0ULL
#endif

#ifdef FMRB_PIN_IR_TX
#define RESERVED_IR_TX PIN_BIT(FMRB_PIN_IR_TX)
#else
#define RESERVED_IR_TX 0ULL
#endif

#ifdef FMRB_PIN_IR_RX
#define RESERVED_IR_RX PIN_BIT(FMRB_PIN_IR_RX)
#else
#define RESERVED_IR_RX 0ULL
#endif

#ifdef FMRB_HW_ATOM_DISPLAY
#define RESERVED_HDMI \
    (PIN_BIT(FMRB_PIN_HDMI_SPI_MOSI) | PIN_BIT(FMRB_PIN_HDMI_SPI_MISO) | \
     PIN_BIT(FMRB_PIN_HDMI_SPI_SCLK) | PIN_BIT(FMRB_PIN_HDMI_SPI_CS) | \
     PIN_BIT(FMRB_PIN_HDMI_I2C_SDA) | PIN_BIT(FMRB_PIN_HDMI_I2C_SCL))
#else
#define RESERVED_HDMI 0ULL
#endif

#ifdef FMRB_PIN_BUTTON_UP
#define BUTTON_UP_BIT PIN_BIT(FMRB_PIN_BUTTON_UP)
#else
#define BUTTON_UP_BIT 0ULL
#endif
#ifdef FMRB_PIN_BUTTON_DOWN
#define BUTTON_DOWN_BIT PIN_BIT(FMRB_PIN_BUTTON_DOWN)
#else
#define BUTTON_DOWN_BIT 0ULL
#endif
#ifdef FMRB_PIN_BUTTON_ENTER
#define BUTTON_ENTER_BIT PIN_BIT(FMRB_PIN_BUTTON_ENTER)
#else
#define BUTTON_ENTER_BIT 0ULL
#endif
#define BUTTONS (BUTTON_UP_BIT | BUTTON_DOWN_BIT | BUTTON_ENTER_BIT)

// The buttons are masked out last: an app may read them as an input.
#define GPIO_RESERVED \
    ((RESERVED_CHIP | RESERVED_BOARD | RESERVED_LINK_UART | RESERVED_SDMMC | \
      RESERVED_LCD_SPI | RESERVED_BACKLIGHT | RESERVED_INPUT_DEVICES | RESERVED_I2S | \
      RESERVED_AMP | RESERVED_SDIO | RESERVED_RADIO_EN | RESERVED_ETH | \
      RESERVED_IR_TX | RESERVED_IR_RX | RESERVED_HDMI) & ~BUTTONS)

_Static_assert(FMRB_PIN_MAX <= 64, "the GPIO reserved mask is 64 bits");

// The SDIO names repeat esp_hosted's settings; a board whose sdkconfig moves
// them must move these too.
#if defined(FMRB_PIN_SDIO_CLK) && defined(CONFIG_ESP_HOSTED_SDIO_PIN_CLK)
_Static_assert(FMRB_PIN_SDIO_CLK == CONFIG_ESP_HOSTED_SDIO_PIN_CLK &&
               FMRB_PIN_SDIO_CMD == CONFIG_ESP_HOSTED_SDIO_PIN_CMD &&
               FMRB_PIN_SDIO_D0 == CONFIG_ESP_HOSTED_SDIO_PIN_D0 &&
               FMRB_PIN_SDIO_D1 == CONFIG_ESP_HOSTED_SDIO_PIN_D1 &&
               FMRB_PIN_SDIO_D2 == CONFIG_ESP_HOSTED_SDIO_PIN_D2 &&
               FMRB_PIN_SDIO_D3 == CONFIG_ESP_HOSTED_SDIO_PIN_D3,
               "FMRB_PIN_SDIO_* differ from the esp_hosted SDIO pins in sdkconfig");
#endif
#if defined(FMRB_PIN_RADIO_EN) && defined(CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE)
_Static_assert(FMRB_PIN_RADIO_EN == CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE,
               "FMRB_PIN_RADIO_EN differs from the esp_hosted reset pin in sdkconfig");
#endif

// In PSRAM: a table the pin manager reads only when a driver claims or
// releases a pin (task context, never from an ISR or while the cache is off),
// and internal RAM is the scarcer of the two. This also pays for the DSI
// driver's second DMA link list the double-buffered display added
// (doc/fullscreen_hires/report/h1.md).
FMRB_EXT_RAM_BSS_ATTR static fmrb_pin_status_t s_pins[FMRB_PIN_MAX];

static void register_system_pin(int pin)
{
    if (pin >= 0 && pin < FMRB_PIN_MAX) {
        s_pins[pin].usage = FMRB_PIN_SYSTEM_EXCLUSIVE;
        s_pins[pin].owner = NULL;
    }
}

void fmrb_pin_manager_init(void)
{
    memset(s_pins, 0, sizeof(s_pins));

    // ESP32-S3 WROOM package restricted pins
    register_system_pin(FMRB_PIN_RESTRICTED_BOOT);
    register_system_pin(FMRB_PIN_RESTRICTED_JTAG);
    register_system_pin(FMRB_PIN_RESTRICTED_USB_DN);
    register_system_pin(FMRB_PIN_RESTRICTED_USB_DP);
    register_system_pin(FMRB_PIN_RESTRICTED_PSRAM0);
    register_system_pin(FMRB_PIN_RESTRICTED_PSRAM1);
    register_system_pin(FMRB_PIN_RESTRICTED_PSRAM2);
    register_system_pin(FMRB_PIN_RESTRICTED_STRAP1);
    register_system_pin(FMRB_PIN_RESTRICTED_STRAP2);

    // Board-specific system-reserved pins

    // SD Card SPI
    register_system_pin(FMRB_PIN_SD_CS);
    register_system_pin(FMRB_PIN_SD_MOSI);
    register_system_pin(FMRB_PIN_SD_SCLK);
    register_system_pin(FMRB_PIN_SD_MISO);
    register_system_pin(FMRB_PIN_SD_DETECT);

    // Graphics-Audio SPI
    register_system_pin(FMRB_PIN_GFX_SPI_MOSI);
    register_system_pin(FMRB_PIN_GFX_SPI_MISO);
    register_system_pin(FMRB_PIN_GFX_SPI_SCLK);
    register_system_pin(FMRB_PIN_GFX_SPI_CS);
    register_system_pin(FMRB_PIN_GFX_SPI_INTR);

    // System GPIOs
    register_system_pin(FMRB_PIN_USB_POWER);
    register_system_pin(FMRB_PIN_STATUS_LED);
    register_system_pin(FMRB_PIN_ERROR_LED);
    register_system_pin(FMRB_PIN_WROVER_RESET);

    // Buttons
#ifdef FMRB_PIN_BUTTON_UP
    register_system_pin(FMRB_PIN_BUTTON_UP);
#endif
#ifdef FMRB_PIN_BUTTON_DOWN
    register_system_pin(FMRB_PIN_BUTTON_DOWN);
#endif
    register_system_pin(FMRB_PIN_BUTTON_ENTER);

    // A panel on SPI driven by this chip (boards that draw locally on a small
    // LCD). Only the boards that have one define these.
#ifdef FMRB_PIN_LCD_SPI_MOSI
    register_system_pin(FMRB_PIN_LCD_SPI_MOSI);
    register_system_pin(FMRB_PIN_LCD_SPI_SCLK);
    register_system_pin(FMRB_PIN_LCD_SPI_CS);
    register_system_pin(FMRB_PIN_LCD_SPI_DC);
    register_system_pin(FMRB_PIN_LCD_SPI_RST);
#endif

    // I2C pins: not system-reserved. Acquired dynamically via hw_proxy I2C init.
    // Kernel acquires first (e.g., for RTC), preventing user apps from conflicting.

#ifdef FMRB_HW_ATOM_DISPLAY
    // HDMI SPI
    register_system_pin(FMRB_PIN_HDMI_SPI_MOSI);
    register_system_pin(FMRB_PIN_HDMI_SPI_MISO);
    register_system_pin(FMRB_PIN_HDMI_SPI_SCLK);
    register_system_pin(FMRB_PIN_HDMI_SPI_CS);
    register_system_pin(FMRB_PIN_HDMI_I2C_SDA);
    register_system_pin(FMRB_PIN_HDMI_I2C_SCL);
#endif

    FMRB_LOGI(TAG, "Pin manager initialized");
}

fmrb_pin_status_t fmrb_pin_manager_get_status(int pin)
{
    fmrb_pin_status_t empty = { .usage = FMRB_PIN_UNUSED, .owner = NULL };
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return empty;
    }
    return s_pins[pin];
}

fmrb_err_t fmrb_pin_manager_acquire(int pin, fmrb_pin_usage_t usage, void *owner)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (s_pins[pin].usage != FMRB_PIN_UNUSED) {
        if (s_pins[pin].owner == owner && s_pins[pin].usage == usage) {
            return FMRB_OK;  // already acquired by same owner for same usage
        }
        FMRB_LOGW(TAG, "Pin %d already in use (usage=%d)", pin, s_pins[pin].usage);
        return FMRB_ERR_BUSY;
    }
    s_pins[pin].usage = usage;
    s_pins[pin].owner = owner;
    return FMRB_OK;
}

void fmrb_pin_manager_release(int pin)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return;
    }
    // Do not release system pins
    if (s_pins[pin].usage == FMRB_PIN_SYSTEM_EXCLUSIVE) {
        return;
    }
    s_pins[pin].usage = FMRB_PIN_UNUSED;
    s_pins[pin].owner = NULL;
}

void fmrb_pin_manager_release_by_owner(void *owner)
{
    for (int i = 0; i < FMRB_PIN_MAX; i++) {
        if (s_pins[i].owner == owner && s_pins[i].usage != FMRB_PIN_SYSTEM_EXCLUSIVE) {
            FMRB_LOGI(TAG, "Releasing pin %d (usage=%d)", i, s_pins[i].usage);
            s_pins[i].usage = FMRB_PIN_UNUSED;
            s_pins[i].owner = NULL;
        }
    }
}

bool fmrb_pin_manager_is_available(int pin)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return false;
    }
    return s_pins[pin].usage == FMRB_PIN_UNUSED;
}

bool fmrb_pin_manager_is_system_input(int pin)
{
    if (pin < 0) {
        return false;
    }
    // GPIO_NUM_NC (-1) never matches a pin >= 0.
#ifdef FMRB_PIN_BUTTON_UP
    if (pin == FMRB_PIN_BUTTON_UP) return true;
#endif
#ifdef FMRB_PIN_BUTTON_DOWN
    if (pin == FMRB_PIN_BUTTON_DOWN) return true;
#endif
#ifdef FMRB_PIN_BUTTON_ENTER
    if (pin == FMRB_PIN_BUTTON_ENTER) return true;
#endif
    return false;
}

bool fmrb_pin_manager_is_gpio_reserved(int pin)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return false;
    }
    return ((GPIO_RESERVED >> pin) & 1ULL) != 0;
}

fmrb_pin_status_t fmrb_pin_manager_get_app_status(int pin)
{
    fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
    if (st.usage == FMRB_PIN_UNUSED && fmrb_pin_manager_is_gpio_reserved(pin)) {
        st.usage = FMRB_PIN_SYSTEM_EXCLUSIVE;
    }
    return st;
}

fmrb_pin_gpio_verdict_t fmrb_pin_manager_claim_gpio(int pin, fmrb_pin_gpio_use_t use,
                                                    void *caller)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return FMRB_PIN_GPIO_NO_RANGE;
    }
    if (use != FMRB_PIN_GPIO_READ && fmrb_pin_manager_is_gpio_reserved(pin)) {
        return FMRB_PIN_GPIO_NO_SYSTEM;
    }
    fmrb_pin_gpio_verdict_t v = fmrb_pin_gpio_judge(
        s_pins[pin], fmrb_pin_manager_is_system_input(pin), use, caller);
    if (v == FMRB_PIN_GPIO_OK && s_pins[pin].usage == FMRB_PIN_UNUSED) {
        s_pins[pin].usage = FMRB_PIN_USER_GPIO;
        s_pins[pin].owner = caller;
    }
    return v;
}
