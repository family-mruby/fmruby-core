#include "fmrb_sysinfo.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_mac.h"
#endif

static uint8_t g_bt_mac[6];
static bool    g_bt_mac_known = false;

void fmrb_sysinfo_set_bt_mac(const uint8_t mac[6])
{
    if (!mac) return;
    memcpy(g_bt_mac, mac, sizeof(g_bt_mac));
    g_bt_mac_known = true;
}

bool fmrb_sysinfo_bt_mac_str(char *buf, size_t len)
{
    if (!buf || len < 18) {
        if (buf && len > 0) buf[0] = '\0';
        return false;
    }

    // What the BLE host reported, if it has synced. NimBLE hands the address
    // over least significant byte first, and an address is written the other
    // way round.
    if (g_bt_mac_known) {
        snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 g_bt_mac[5], g_bt_mac[4], g_bt_mac[3],
                 g_bt_mac[2], g_bt_mac[1], g_bt_mac[0]);
        return true;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    // A machine with its own radio derives the BT MAC from the base MAC in
    // efuse. One without (the P4 talks to a separate radio chip) fails here,
    // and there is nothing to show until BLE syncs.
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
        snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return true;
    }
#endif

    snprintf(buf, len, "-");
    return false;
}
