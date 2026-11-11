// Host tests for the saved Wi-Fi networks (main/drivers/wifi/wifi_conf.c,
// doc/wifi_settings): the old one-network [wifi] form and the
// [[networks]] list read, add/forget with the limit of five, and the text
// written read back the same (escapes included).

#include "wifi_conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void parse(const char *text, wifi_conf_t *conf)
{
    char *buf = strdup(text);
    char err[128];
    toml_table_t *root = toml_parse(buf, err, sizeof(err));
    if (!root) printf("parse error: %s\n", err);
    wifi_conf_from_toml(root, conf, free);
    if (root) toml_free(root);
    free(buf);
}

static void test_old_form(void)
{
    wifi_conf_t c;
    parse("[wifi]\nenable = true\nssid = \"home\"\npassword = \"pw1\"\nhostname = \"box\"\n", &c);
    CHECK(c.enable, "enabled");
    CHECK(c.count == 1, "one network, got %d", c.count);
    CHECK(strcmp(c.nets[0].ssid, "home") == 0, "ssid %s", c.nets[0].ssid);
    CHECK(strcmp(c.nets[0].password, "pw1") == 0, "password");
    CHECK(strcmp(c.hostname, "box") == 0, "hostname %s", c.hostname);

    parse("[wifi]\nssid = \"home\"\npassword = \"pw1\"\n", &c);
    CHECK(!c.enable, "a [wifi] without enable stays off, as before");

    parse("[wifi]\nenable = false\nssid = \"home\"\n", &c);
    CHECK(!c.enable, "enable = false");
    CHECK(c.count == 1 && c.nets[0].password[0] == '\0', "open network");

    parse("[wifi]\nenable = true\n", &c);
    CHECK(c.enable && c.count == 0, "enabled, nothing saved");

    wifi_conf_from_toml(NULL, &c, free);
    CHECK(c.enable && c.count == 0, "no file: enabled, empty");
}

static void test_list(void)
{
    wifi_conf_t c;
    parse("[wifi]\nenable = true\nssid = \"home\"\npassword = \"a\"\n"
          "[[networks]]\nssid = \"cafe\"\npassword = \"b\"\n"
          "[[networks]]\nssid = \"home\"\npassword = \"dup\"\n"
          "[[networks]]\nssid = \"secret\"\nhidden = true\n"
          "[[networks]]\nssid = \"x4\"\n[[networks]]\nssid = \"x5\"\n[[networks]]\nssid = \"x6\"\n", &c);
    CHECK(c.count == 5, "five kept, got %d", c.count);
    CHECK(strcmp(c.nets[0].ssid, "home") == 0 && strcmp(c.nets[0].password, "a") == 0,
          "the old [wifi] one first, the repeat dropped");
    CHECK(strcmp(c.nets[1].ssid, "cafe") == 0, "cafe second");
    CHECK(c.nets[2].hidden && !c.nets[1].hidden, "hidden flag");
    CHECK(wifi_conf_find(&c, "x6") < 0, "the sixth dropped");

    parse("[[networks]]\nssid = \"only\"\npassword = \"p\"\n", &c);
    CHECK(c.enable && c.count == 1, "a list alone is enabled");

    char longpw[100];
    memset(longpw, 'p', 70);
    longpw[70] = '\0';
    char text[256];
    snprintf(text, sizeof(text), "[[networks]]\nssid = \"a\"\npassword = \"%s\"\n"
             "[[networks]]\nssid = \"012345678901234567890123456789012\"\n", longpw);
    parse(text, &c);
    CHECK(c.count == 0, "too long a password or ssid drops the entry, got %d", c.count);
}

static void test_add_forget(void)
{
    wifi_conf_t c;
    parse("[wifi]\nenable = true\nssid = \"home\"\npassword = \"a\"\n", &c);
    CHECK(!wifi_conf_add(&c, "home", "a"), "same first entry: no change");
    CHECK(wifi_conf_add(&c, "cafe", "b"), "new one");
    CHECK(c.count == 2 && strcmp(c.nets[0].ssid, "cafe") == 0, "newest first");
    CHECK(wifi_conf_add(&c, "home", "a2"), "a new password");
    CHECK(c.count == 2 && strcmp(c.nets[0].ssid, "home") == 0 &&
          strcmp(c.nets[0].password, "a2") == 0, "moved first with the new password");
    wifi_conf_add(&c, "n3", "");
    wifi_conf_add(&c, "n4", "");
    wifi_conf_add(&c, "n5", "");
    CHECK(c.count == 5, "five");
    wifi_conf_add(&c, "n6", "");
    CHECK(c.count == 5 && wifi_conf_find(&c, "cafe") < 0, "the oldest went");
    CHECK(wifi_conf_find(&c, "home") == 4, "home now last");
    CHECK(wifi_conf_forget(&c, "n4"), "forget");
    CHECK(c.count == 4 && wifi_conf_find(&c, "n4") < 0, "gone");
    CHECK(!wifi_conf_forget(&c, "nothere"), "forget unknown");
    CHECK(!wifi_conf_add(&c, "", "x"), "empty ssid refused");

    parse("[[networks]]\nssid = \"h\"\nhidden = true\n", &c);
    wifi_conf_add(&c, "h", "pw");
    CHECK(c.nets[0].hidden, "hidden kept on add");
}

static void test_round_trip(void)
{
    wifi_conf_t c, d;
    wifi_conf_init(&c);
    strcpy(c.hostname, "box");
    wifi_conf_add(&c, "plain", "");
    wifi_conf_add(&c, "q\"uote\\back", "p\"w\\");
    wifi_conf_add(&c, "tab\there", "x");
    wifi_conf_add(&c, "日本語", "パス");
    c.nets[3].hidden = true;
    char buf[2048];
    size_t n = wifi_conf_to_text(&c, buf, sizeof(buf));
    CHECK(n < sizeof(buf), "fits");
    parse(buf, &d);
    CHECK(d.enable && strcmp(d.hostname, "box") == 0, "head");
    CHECK(d.count == c.count, "count %d vs %d", d.count, c.count);
    for (int i = 0; i < c.count && i < d.count; i++) {
        CHECK(strcmp(c.nets[i].ssid, d.nets[i].ssid) == 0, "ssid %d", i);
        CHECK(strcmp(c.nets[i].password, d.nets[i].password) == 0, "password %d", i);
        CHECK(c.nets[i].hidden == d.nets[i].hidden, "hidden %d", i);
    }
    char small[16];
    size_t need = wifi_conf_to_text(&c, small, sizeof(small));
    CHECK(need == n && strlen(small) == sizeof(small) - 1, "short buffer: size reported, text cut");
    c.enable = false;
    wifi_conf_to_text(&c, buf, sizeof(buf));
    parse(buf, &d);
    CHECK(!d.enable, "enable = false written");
    wifi_conf_wipe(&c);
    CHECK(c.count == 0 && c.nets[0].password[0] == '\0', "wiped");
}

int main(void)
{
    test_old_form();
    test_list();
    test_add_forget();
    test_round_trip();
    printf("wifi_conf: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
