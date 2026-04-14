#include "fmrb_hal_pin_manager.h"
#include "fmrb_pin_assign.h"
#include "fmrb_log.h"
#include <string.h>

static const char *TAG = "pin_manager";

static fmrb_pin_status_t s_pins[FMRB_PIN_MAX];

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

    // FS Proxy UART
    register_system_pin(FMRB_PIN_FSPROXY_TX);
    register_system_pin(FMRB_PIN_FSPROXY_RX);

    // System GPIOs
    register_system_pin(FMRB_PIN_USB_POWER);
    register_system_pin(FMRB_PIN_STATUS_LED);
    register_system_pin(FMRB_PIN_WROVER_RESET);

    // Buttons
#ifdef FMRB_PIN_BUTTON_UP
    register_system_pin(FMRB_PIN_BUTTON_UP);
#endif
#ifdef FMRB_PIN_BUTTON_DOWN
    register_system_pin(FMRB_PIN_BUTTON_DOWN);
#endif
    register_system_pin(FMRB_PIN_BUTTON_ENTER);

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
