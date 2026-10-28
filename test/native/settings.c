// Host test of the PC's settings and notices (doc/linux_native L14): the
// parts that need no bus, as they are in native/sys/.
//
//   the notices   linux_notify.c's store: durations from the freedesktop
//                 timeouts, ids, replacing, the order (newest first), the
//                 text the desktop reads (fields, line breaks as 0x1F, tabs
//                 as spaces, cut at a whole character), ending in time, the
//                 oldest going for a fifth, dismissing and closing, and the
//                 NotificationClosed signals owed (only for the Linux ones)
//   Wi-Fi         linux_net.c: one entry per SSID (the strongest; active and
//                 saved kept), hidden ones dropped, the order; the security a
//                 new connection gets from the access point's flags; the
//                 outcome from the device's state reason
//   Bluetooth     linux_bt.c: which devices are listed
//   the volume    sys_linux_pw.c: wpctl's and pactl's output, the steps
//
// The bus parts (NetworkManager, BlueZ, the notification service) run in
// the compositor's check against stand-ins (test/native/wl/wl_settings_check.rb).
//
// Usage: native_settings_test

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linux_bt.h"
#include "linux_net.h"
#include "linux_notify.h"
#include "sys_linux.h"

static int g_fail = 0;
static int g_count = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_count++;                                                    \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: ", __func__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
            g_fail++;                                                 \
        }                                                             \
    } while (0)

static char g_buf[8192];

static const char *list(void)
{
    int32_t n = linux_notify_list(g_buf, sizeof(g_buf));
    if (n < 0 || (size_t)n >= sizeof(g_buf)) return "";
    return g_buf;
}

static void test_notices(void)
{
    CHECK(linux_notify_duration(-1) == 5000, "default %d", linux_notify_duration(-1));
    CHECK(linux_notify_duration(0) == 10000, "never %d", linux_notify_duration(0));
    CHECK(linux_notify_duration(500) == 2000, "short %d", linux_notify_duration(500));
    CHECK(linux_notify_duration(3000) == 3000, "3 s %d", linux_notify_duration(3000));
    CHECK(linux_notify_duration(60000) == 10000, "long %d", linux_notify_duration(60000));

    linux_notify_reset();
    uint64_t t = 1000000;
    uint32_t g0 = linux_notify_gen_at(t);
    CHECK(linux_notify_list(NULL, 0) == 0, "empty");
    uint32_t a = linux_notify_post_at("linux", "foot", "Build done", "all 12 tests\npassed", -1, 0, t);
    CHECK(a != 0 && linux_notify_gen_at(t) != g0, "posted: id %u", a);
    CHECK(strcmp(list(), "1\tlinux\tfoot\tBuild done\tall 12 tests\x1fpassed\n") == 0, "the text: [%s]", list());
    uint32_t b = linux_notify_post_at("app", "Clock", "Tab\there", "", 3000, 0, t + 10);
    CHECK(b == a + 1, "ids %u %u", a, b);
    const char *want = "2\tapp\tClock\tTab here\t\n1\tlinux";
    CHECK(strncmp(list(), want, strlen(want)) == 0, "newest first, tab as space: [%s]", list());
    // Replacing keeps the id and moves it to the top.
    uint32_t a2 = linux_notify_post_at("linux", "foot", "Build failed", "", -1, a, t + 20);
    CHECK(a2 == a && strncmp(list(), "1\tlinux\tfoot\tBuild failed", 25) == 0, "replaced: [%s]", list());
    // replaces of one that is gone: a new one with that id (the rules).
    uint32_t c = linux_notify_post_at("volume", "", "70", "", 1500, 99, t + 30);
    CHECK(c == 99, "a replaces of none: %u", c);
    // Ending in time: c (1.5 s asked, 2 s the least) first, b (3 s), a (5 s
    // from t + 20).
    uint32_t g1 = linux_notify_gen_at(t + 1000);
    CHECK(linux_notify_gen_at(t + 2100) != g1 && strstr(list(), "volume") == NULL, "the volume ended: [%s]", list());
    linux_notify_gen_at(t + 3100);
    CHECK(strstr(list(), "Clock") == NULL && strstr(list(), "Build failed") != NULL, "Clock ended: [%s]", list());
    linux_notify_gen_at(t + 5100);
    CHECK(list()[0] == '\0', "all ended: [%s]", list());
    // Closed signals owed: the Linux one only (expired, 1).
    uint32_t id = 0, reason = 0;
    int n = 0;
    bool first = linux_notify_take_closed(&id, &reason);
    while (linux_notify_take_closed(&id, &reason)) n++;
    CHECK(first && n == 0, "one closed signal (%d more)", n);
    CHECK(id == a && reason == 1, "closed %u reason %u", id, reason);

    // A fifth pushes the oldest out; dismissing and closing.
    linux_notify_reset();
    for (int i = 0; i < 5; i++) {
        char s[16];
        snprintf(s, sizeof(s), "n%d", i);
        linux_notify_post_at("linux", "x", s, "", -1, 0, t + (uint64_t)i);
    }
    CHECK(strstr(list(), "\tn0\t") == NULL && strstr(list(), "\tn4\t") != NULL, "four kept: [%s]", list());
    CHECK(linux_notify_take_closed(&id, &reason) && id == 1 && reason == 1, "the oldest closed %u %u", id, reason);
    CHECK(linux_notify_dismiss(2) && strstr(list(), "\tn1\t") == NULL, "dismissed: [%s]", list());
    CHECK(linux_notify_take_closed(&id, &reason) && id == 2 && reason == 2, "dismissed signal %u %u", id, reason);
    CHECK(linux_notify_close(3) && linux_notify_take_closed(&id, &reason) && id == 3 && reason == 3,
          "closed by a call %u %u", id, reason);
    CHECK(!linux_notify_dismiss(77), "no such notice");
    // An app's notice owes no signal.
    linux_notify_reset();
    uint32_t ap = linux_notify_post_at("app", "Game", "Hi", "", -1, 0, t);
    linux_notify_dismiss(ap);
    CHECK(!linux_notify_take_closed(&id, &reason), "no signal for an app's notice");

    // Cut at a whole character: 200 times "あ" (3 bytes) into 128 bytes.
    linux_notify_reset();
    char big[700];
    big[0] = '\0';
    for (int i = 0; i < 200; i++) strcat(big, "\xe3\x81\x82");
    linux_notify_post_at("linux", "x", big, "", -1, 0, t);
    const char *l = list();
    const char *s3 = strchr(strchr(strchr(l, '\t') + 1, '\t') + 1, '\t') + 1;
    size_t sl = (size_t)(strchr(s3, '\t') - s3);
    CHECK(sl == 126 && sl % 3 == 0, "the summary cut to %zu bytes", sl);
    // The length is asked first (more than cap: nothing written).
    char small[8] = "zzzz";
    CHECK(linux_notify_list(small, sizeof(small)) > 8 && strcmp(small, "zzzz") == 0, "too small: untouched");
    linux_notify_reset();
}

static void test_wifi(void)
{
    linux_net_ap_t ap[8];
    memset(ap, 0, sizeof(ap));
    strcpy(ap[0].ssid, "cafe");
    ap[0].strength = 40;
    strcpy(ap[1].ssid, "home");
    ap[1].strength = 60;
    ap[1].saved = true;
    strcpy(ap[2].ssid, "cafe");
    ap[2].strength = 80;
    ap[2].secured = true;
    strcpy(ap[3].ssid, "");      // hidden
    ap[3].strength = 99;
    strcpy(ap[4].ssid, "office");
    ap[4].strength = 30;
    ap[4].active = true;
    strcpy(ap[5].ssid, "home");
    ap[5].strength = 20;
    ap[5].active = false;
    strcpy(ap[6].ssid, "attic");
    ap[6].strength = 60;
    int n = linux_net_ap_tidy(ap, 7);
    CHECK(n == 4, "four networks: %d", n);
    CHECK(strcmp(ap[0].ssid, "office") == 0 && ap[0].active, "the active one first: %s", ap[0].ssid);
    CHECK(strcmp(ap[1].ssid, "cafe") == 0 && ap[1].strength == 80 && ap[1].secured, "the strongest cafe: %s %u",
          ap[1].ssid, ap[1].strength);
    CHECK(strcmp(ap[2].ssid, "attic") == 0 && strcmp(ap[3].ssid, "home") == 0 && ap[3].saved,
          "same strength by name, saved kept: %s %s", ap[2].ssid, ap[3].ssid);

    CHECK(strcmp(linux_net_key_mgmt(0, 0, 0), "") == 0, "open");
    CHECK(linux_net_key_mgmt(1, 0, 0) == NULL, "WEP not set up");
    CHECK(strcmp(linux_net_key_mgmt(1, 0, 0x188), "wpa-psk") == 0, "WPA2 PSK");
    CHECK(strcmp(linux_net_key_mgmt(1, 0x144, 0), "wpa-psk") == 0, "WPA1 PSK");
    CHECK(strcmp(linux_net_key_mgmt(1, 0, 0x588), "wpa-psk") == 0, "WPA2/3 mixed: psk");
    CHECK(strcmp(linux_net_key_mgmt(1, 0, 0x488), "sae") == 0, "WPA3 only: sae");
    CHECK(linux_net_key_mgmt(1, 0, 0x288) == NULL, "802.1X not set up");

    CHECK(strcmp(linux_net_failure_code(7, false), "need_password") == 0, "no secrets, none given");
    CHECK(strcmp(linux_net_failure_code(7, true), "auth_failed") == 0, "no secrets, one given");
    CHECK(strcmp(linux_net_failure_code(8, true), "auth_failed") == 0, "supplicant");
    CHECK(strcmp(linux_net_failure_code(53, false), "no_network") == 0, "ssid not found");
    CHECK(strcmp(linux_net_failure_code(36, false), "failed") == 0, "other");
}

static void test_bt(void)
{
    CHECK(linux_bt_known(true, false, false) && linux_bt_known(false, true, false) &&
          linux_bt_known(false, false, true), "paired, bonded or trusted");
    CHECK(!linux_bt_known(false, false, false), "a device only seen");
}

static void test_volume(void)
{
    double v = 0;
    bool m = true;
    CHECK(sys_linux_pw_parse_wpctl("Volume: 0.40\n", &v, &m) && v > 0.39 && v < 0.41 && !m, "wpctl %.2f %d", v, m);
    CHECK(sys_linux_pw_parse_wpctl("Volume: 1.00 [MUTED]\n", &v, &m) && v > 0.99 && m, "wpctl muted");
    CHECK(!sys_linux_pw_parse_wpctl("Could not connect to PipeWire\n", &v, &m), "wpctl error");
    int pc = 0;
    CHECK(sys_linux_pw_parse_pactl_volume("Volume: front-left: 26214 /  40% / -23.88 dB,   front-right: 26214 /  40% "
                                          "/ -23.88 dB\n        balance 0.00\n", &pc) && pc == 40, "pactl %d", pc);
    CHECK(sys_linux_pw_parse_pactl_volume("Volume: mono: 65536 / 100% / 0.00 dB\n", &pc) && pc == 100, "pactl mono");
    CHECK(!sys_linux_pw_parse_pactl_volume("Connection failure\n", &pc), "pactl error");
    CHECK(sys_linux_pw_parse_pactl_mute("Mute: yes\n", &m) && m, "pactl mute yes");
    CHECK(sys_linux_pw_parse_pactl_mute("Mute: no\n", &m) && !m, "pactl mute no");
    CHECK(sys_linux_pw_step(0.4, 10) == 4 && sys_linux_pw_step(0.0, 10) == 0 && sys_linux_pw_step(1.5, 10) == 10 &&
          sys_linux_pw_step(0.66, 10) == 7, "steps");
}

int main(void)
{
    test_notices();
    test_wifi();
    test_bt();
    test_volume();
    printf("native settings: %d/%d checks passed\n", g_count - g_fail, g_count);
    return g_fail == 0 ? 0 : 1;
}
