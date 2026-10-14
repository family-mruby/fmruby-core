// Host tests for the secret-file rules (main/kernel/fmrb_secret_path.c).
//
// The BLE console and devctl both ask these functions whether a remote
// client may read, write, delete or rename a path. A mistake here is silent
// (a password handed out, or a setting that cannot be written), so the whole
// table runs here under the host compiler with no device.

#include "fmrb_secret_path.h"

#include <stdio.h>
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

static const char *cls_name(fmrb_path_class_t c)
{
    switch (c) {
    case FMRB_PATH_PLAIN: return "plain";
    case FMRB_PATH_SETTINGS: return "settings";
    case FMRB_PATH_PASSWORD: return "password";
    case FMRB_PATH_SECRET: return "secret";
    }
    return "?";
}

static void expect_class(const char *path, fmrb_path_class_t want)
{
    fmrb_path_class_t got = fmrb_path_classify(path);
    CHECK(got == want, "%s: %s, want %s", path, cls_name(got), cls_name(want));
}

static void test_classify(void)
{
    // The secrets.
    expect_class("/home/wifi.toml", FMRB_PATH_PASSWORD);
    expect_class("/etc/wifi.toml", FMRB_PATH_PASSWORD);
    expect_class("/home/wifi.toml.part", FMRB_PATH_PASSWORD);
    expect_class("/home/asterism/fmruby-aaaaaa.tar", FMRB_PATH_SECRET);
    expect_class("/home/asterism/other.key", FMRB_PATH_SECRET);
    expect_class("/home/asterism/config.toml.part", FMRB_PATH_SECRET);
    expect_class("/home/asterism/sub/x.tar", FMRB_PATH_SECRET);
    // The API keys (the tts key), kept like the WiFi password.
    expect_class("/home/secrets.toml", FMRB_PATH_PASSWORD);
    expect_class("/etc/secrets.toml", FMRB_PATH_PASSWORD);
    expect_class("/home/secrets.toml.part", FMRB_PATH_PASSWORD);
    expect_class("/etc/secrets.toml.part", FMRB_PATH_PASSWORD);

    // The same files under other spellings the file HAL accepts.
    expect_class("home/wifi.toml", FMRB_PATH_PASSWORD);
    expect_class("//home//wifi.toml", FMRB_PATH_PASSWORD);
    expect_class("/home/./wifi.toml", FMRB_PATH_PASSWORD);
    expect_class("/./etc/wifi.toml/", FMRB_PATH_PASSWORD);
    expect_class("/flash/home/wifi.toml", FMRB_PATH_PASSWORD);
    expect_class("/flash/etc/wifi.toml", FMRB_PATH_PASSWORD);
    expect_class("/flash/home/asterism/a.tar", FMRB_PATH_SECRET);
    expect_class("/flash/home/secrets.toml", FMRB_PATH_PASSWORD);
    expect_class("/flash/etc/secrets.toml", FMRB_PATH_PASSWORD);
    expect_class("//etc/./secrets.toml/", FMRB_PATH_PASSWORD);
    expect_class("/home/x/../secrets.toml", FMRB_PATH_SECRET);
    expect_class("/home/x/../wifi.toml", FMRB_PATH_SECRET);
    expect_class("/app/../home/asterism/a.tar", FMRB_PATH_SECRET);
    expect_class("/app/..", FMRB_PATH_SECRET);

    // Readable settings.
    expect_class("/home/asterism/config.toml", FMRB_PATH_SETTINGS);
    expect_class("/home/asterism", FMRB_PATH_SETTINGS);
    expect_class("/home/asterism/", FMRB_PATH_SETTINGS);
    expect_class("/flash/home/asterism/config.toml", FMRB_PATH_SETTINGS);

    // Ordinary files, including near misses.
    expect_class("/", FMRB_PATH_PLAIN);
    expect_class("", FMRB_PATH_PLAIN);
    expect_class("/home", FMRB_PATH_PLAIN);
    expect_class("/etc", FMRB_PATH_PLAIN);
    expect_class("/home/notes.txt", FMRB_PATH_PLAIN);
    expect_class("/home/my_wifi.toml", FMRB_PATH_PLAIN);
    expect_class("/home/sub/wifi.toml", FMRB_PATH_PLAIN);
    expect_class("/app/wifi.toml", FMRB_PATH_PLAIN);
    expect_class("/home/asterism_profile.toml", FMRB_PATH_PLAIN);
    expect_class("/home/asterismx/a.tar", FMRB_PATH_PLAIN);
    expect_class("/etc/system_conf.toml", FMRB_PATH_PLAIN);
    expect_class("/mnt/sd/home/wifi.toml", FMRB_PATH_PLAIN);
    expect_class("/flash/flash/home/wifi.toml", FMRB_PATH_PLAIN);
    expect_class("/app/..x", FMRB_PATH_PLAIN);
    expect_class("/home/.wifi.toml", FMRB_PATH_PLAIN);
    expect_class("/home/my_secrets.toml", FMRB_PATH_PLAIN);
    expect_class("/home/sub/secrets.toml", FMRB_PATH_PLAIN);
    expect_class("/app/secrets.toml", FMRB_PATH_PLAIN);
    expect_class("/etc/services.toml", FMRB_PATH_PLAIN);
    expect_class("/home/services.toml", FMRB_PATH_PLAIN);
}

static void test_contains(void)
{
    CHECK(fmrb_path_contains_secret("/"), "/");
    CHECK(fmrb_path_contains_secret(""), "empty");
    CHECK(fmrb_path_contains_secret("/flash"), "/flash");
    CHECK(fmrb_path_contains_secret("/home"), "/home");
    CHECK(fmrb_path_contains_secret("/home/"), "/home/");
    CHECK(fmrb_path_contains_secret("/etc"), "/etc");
    CHECK(fmrb_path_contains_secret("/home/asterism"), "/home/asterism");
    CHECK(fmrb_path_contains_secret("/home/x/.."), "/home/x/..");
    CHECK(!fmrb_path_contains_secret("/app"), "/app");
    CHECK(!fmrb_path_contains_secret("/home/sub"), "/home/sub");
    CHECK(!fmrb_path_contains_secret("/home/asterism/config.toml"), "config.toml");
    CHECK(!fmrb_path_contains_secret("/mnt/sd"), "/mnt/sd");
}

static const char *verdict_name(fmrb_remote_fs_verdict_t v)
{
    switch (v) {
    case FMRB_REMOTE_FS_ALLOW: return "allow";
    case FMRB_REMOTE_FS_DENY_SECRET: return "deny-secret";
    case FMRB_REMOTE_FS_DENY_CLOSED: return "deny-closed";
    }
    return "?";
}

static void expect(fmrb_remote_fs_op_t op, const char *path, const char *to, bool open,
                   fmrb_remote_fs_verdict_t want)
{
    static const char *const ops[] = { "GET", "PUT", "RM", "RENAME" };
    fmrb_remote_fs_verdict_t got = fmrb_remote_fs_check(op, path, to, open);
    CHECK(got == want, "%s %s%s%s (window %s): %s, want %s", ops[op], path,
          to ? " -> " : "", to ? to : "", open ? "open" : "closed",
          verdict_name(got), verdict_name(want));
}

static void test_get(void)
{
    // The keys are never read, open window or not.
    expect(FMRB_REMOTE_FS_GET, "/home/asterism/a.tar", NULL, false, FMRB_REMOTE_FS_DENY_SECRET);
    expect(FMRB_REMOTE_FS_GET, "/home/asterism/a.tar", NULL, true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(FMRB_REMOTE_FS_GET, "/home/x/../asterism/a.tar", NULL, true, FMRB_REMOTE_FS_DENY_SECRET);
    // The WiFi password only while the owner has opened the window.
    expect(FMRB_REMOTE_FS_GET, "/home/wifi.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(FMRB_REMOTE_FS_GET, "/home/wifi.toml", NULL, true, FMRB_REMOTE_FS_ALLOW);
    expect(FMRB_REMOTE_FS_GET, "/etc/wifi.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(FMRB_REMOTE_FS_GET, "/etc/wifi.toml", NULL, true, FMRB_REMOTE_FS_ALLOW);
    expect(FMRB_REMOTE_FS_GET, "/flash/etc/wifi.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
    // The API keys the same way.
    expect(FMRB_REMOTE_FS_GET, "/home/secrets.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(FMRB_REMOTE_FS_GET, "/home/secrets.toml", NULL, true, FMRB_REMOTE_FS_ALLOW);
    expect(FMRB_REMOTE_FS_GET, "/etc/secrets.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(FMRB_REMOTE_FS_GET, "/etc/secrets.toml", NULL, true, FMRB_REMOTE_FS_ALLOW);
    expect(FMRB_REMOTE_FS_GET, "/flash/etc/secrets.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(FMRB_REMOTE_FS_GET, "/home/secrets.toml.part", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
    // Everything else reads as before.
    expect(FMRB_REMOTE_FS_GET, "/home/asterism/config.toml", NULL, false, FMRB_REMOTE_FS_ALLOW);
    expect(FMRB_REMOTE_FS_GET, "/app/demo/hello.app.rb", NULL, false, FMRB_REMOTE_FS_ALLOW);
    expect(FMRB_REMOTE_FS_GET, "/etc/system_conf.toml", NULL, false, FMRB_REMOTE_FS_ALLOW);
    expect(FMRB_REMOTE_FS_GET, "/home/notes.txt", NULL, false, FMRB_REMOTE_FS_ALLOW);
}

static void test_put_rm(void)
{
    const fmrb_remote_fs_op_t ops[] = { FMRB_REMOTE_FS_PUT, FMRB_REMOTE_FS_RM };
    for (int i = 0; i < 2; i++) {
        fmrb_remote_fs_op_t op = ops[i];
        expect(op, "/home/wifi.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
        expect(op, "/home/wifi.toml", NULL, true, FMRB_REMOTE_FS_ALLOW);
        expect(op, "/etc/wifi.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
        expect(op, "/home/secrets.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
        expect(op, "/home/secrets.toml", NULL, true, FMRB_REMOTE_FS_ALLOW);
        expect(op, "/etc/secrets.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
        expect(op, "/home/services.toml", NULL, false, FMRB_REMOTE_FS_ALLOW);
        expect(op, "/home/asterism/config.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
        expect(op, "/home/asterism/config.toml", NULL, true, FMRB_REMOTE_FS_ALLOW);
        expect(op, "/home/asterism/a.tar", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
        expect(op, "/home/asterism/a.tar", NULL, true, FMRB_REMOTE_FS_ALLOW);
        expect(op, "/home/x/../wifi.toml", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
        expect(op, "/home/notes.txt", NULL, false, FMRB_REMOTE_FS_ALLOW);
        expect(op, "/app/demo/hello.app.rb", NULL, false, FMRB_REMOTE_FS_ALLOW);
    }
    // Removing the (empty) directory itself is a settings change too.
    expect(FMRB_REMOTE_FS_RM, "/home/asterism", NULL, false, FMRB_REMOTE_FS_DENY_CLOSED);
}

static void test_rename(void)
{
    const fmrb_remote_fs_op_t R = FMRB_REMOTE_FS_RENAME;
    // Ordinary files rename as before.
    expect(R, "/home/a.txt", "/home/b.txt", false, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/app/x", "/app/y", false, FMRB_REMOTE_FS_ALLOW);
    // Into a secret or a setting: only while the window is open.
    expect(R, "/home/upload.tar", "/home/asterism/a.tar", false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(R, "/home/upload.tar", "/home/asterism/a.tar", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/w.toml", "/home/wifi.toml", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/a.toml", "/home/asterism/config.toml", false, FMRB_REMOTE_FS_DENY_CLOSED);
    // Between secrets: a settings change.
    expect(R, "/home/asterism/a.tar.part", "/home/asterism/a.tar", false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(R, "/home/asterism/a.tar.part", "/home/asterism/a.tar", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/wifi.toml.part", "/home/wifi.toml", true, FMRB_REMOTE_FS_ALLOW);
    // Out of a secret to a readable name: never.
    expect(R, "/home/wifi.toml", "/home/w.txt", true, FMRB_REMOTE_FS_DENY_SECRET);
    // A key into a WiFi name would make it readable in the window: never.
    expect(R, "/home/asterism/a.tar", "/home/wifi.toml", true, FMRB_REMOTE_FS_DENY_SECRET);
    // The WiFi file can go among the keys (kept more strongly).
    expect(R, "/home/wifi.toml", "/home/asterism/w.part", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/etc/wifi.toml", "/home/w.txt", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/home/asterism/a.tar", "/home/a.tar", true, FMRB_REMOTE_FS_DENY_SECRET);
    // The API keys rank with the WiFi password.
    expect(R, "/home/secrets.toml.part", "/home/secrets.toml", false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(R, "/home/secrets.toml.part", "/home/secrets.toml", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/s.toml", "/home/secrets.toml", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/secrets.toml", "/home/s.txt", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/etc/secrets.toml", "/home/services.toml", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/flash/etc/secrets.toml", "/app/k", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/home/secrets.toml", "/etc/secrets.toml", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/secrets.toml", "/home/wifi.toml", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/secrets.toml", "/home/asterism/k.part", true, FMRB_REMOTE_FS_ALLOW);
    expect(R, "/home/asterism/a.tar", "/home/secrets.toml", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/home/asterism/a.tar", "/home/asterism/config.toml", true, FMRB_REMOTE_FS_DENY_SECRET);
    // Nor a directory that holds secrets.
    expect(R, "/home", "/home2", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/etc", "/etc2", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/home/asterism", "/home/old", true, FMRB_REMOTE_FS_DENY_SECRET);
    expect(R, "/flash/home", "/app/h", true, FMRB_REMOTE_FS_DENY_SECRET);
    // A readable setting can go to a readable name, with the window open.
    expect(R, "/home/asterism/config.toml", "/home/c.toml", false, FMRB_REMOTE_FS_DENY_CLOSED);
    expect(R, "/home/asterism/config.toml", "/home/c.toml", true, FMRB_REMOTE_FS_ALLOW);
}

static void expect_summary(const char *toml, const char *src, bool conn, const char *ip,
                           const char *want)
{
    char out[256];
    fmrb_wifi_summary_json(toml, src, conn, ip, out, sizeof(out));
    CHECK(strcmp(out, want) == 0, "summary: %s\n   want %s", out, want);
    CHECK(strstr(out, "password") == NULL && strstr(out, "hunter2") == NULL,
          "summary carries the password: %s", out);
}

static void test_summary(void)
{
    const char *wifi =
        "# WiFi\n[wifi]\nenable = true\n#ssid = \"old\"\nssid = \"Home Net\"\npassword = \"hunter2\"\n";
    expect_summary(wifi, "/etc/wifi.toml", true, "192.0.2.7",
                   "{\"src\":\"/etc/wifi.toml\",\"on\":true,\"ssid\":\"Home Net\",\"conn\":true,\"ip\":\"192.0.2.7\"}");
    // The password line first, and the ssid escaped both ways.
    expect_summary("[wifi]\npassword = \"hunter2\"\nssid = \"a\\\"b\\\\c\\u00e9\"\nenable=false\n",
                   "/home/wifi.toml", false, "",
                   "{\"src\":\"/home/wifi.toml\",\"on\":false,\"ssid\":\"a\\\"b\\\\c\xc3\xa9\",\"conn\":false,\"ip\":\"\"}");
    // No file.
    expect_summary(NULL, "", false, NULL,
                   "{\"src\":\"\",\"on\":false,\"ssid\":\"\",\"conn\":false,\"ip\":\"\"}");
    // "ssid2" and "password_ssid" are other keys; a literal string is taken as it is.
    expect_summary("ssid2 = \"x\"\npassword_ssid = \"hunter2\"\nssid = 'lit'\n", "/etc/wifi.toml", false, "",
                   "{\"src\":\"/etc/wifi.toml\",\"on\":false,\"ssid\":\"lit\",\"conn\":false,\"ip\":\"\"}");
    // A small buffer is cut short and stays terminated.
    char small[20];
    memset(small, 'x', sizeof(small));
    size_t n = fmrb_wifi_summary_json(wifi, "/etc/wifi.toml", true, "192.0.2.7", small, sizeof(small));
    CHECK(n < sizeof(small) && small[n] == '\0', "small buffer: n=%zu", n);
}

static void test_setup_mode(void)
{
    // Off on this board: never, whatever the files and the button say.
    CHECK(fmrb_setup_mode_reason(false, false, false) == FMRB_SETUP_NONE, "off, nothing");
    CHECK(fmrb_setup_mode_reason(false, false, true) == FMRB_SETUP_NONE, "off, button");
    CHECK(fmrb_setup_mode_reason(false, true, true) == FMRB_SETUP_NONE, "off, wifi + button");
    // On: no WiFi settings, or the button.
    CHECK(fmrb_setup_mode_reason(true, true, false) == FMRB_SETUP_NONE, "on, wifi");
    CHECK(fmrb_setup_mode_reason(true, false, false) == FMRB_SETUP_NO_WIFI, "on, no wifi");
    CHECK(fmrb_setup_mode_reason(true, true, true) == FMRB_SETUP_BUTTON, "on, wifi + button");
    CHECK(fmrb_setup_mode_reason(true, false, true) == FMRB_SETUP_BUTTON, "on, button, no wifi");
    CHECK(strcmp(fmrb_setup_reason_text(FMRB_SETUP_NO_WIFI), "no WiFi settings") == 0, "text no wifi");
    CHECK(strcmp(fmrb_setup_reason_text(FMRB_SETUP_BUTTON), "button held at boot") == 0, "text button");
    // The WiFi settings files are secrets of the WiFi kind.
    expect_class(FMRB_WIFI_TOML_HOME_PATH, FMRB_PATH_PASSWORD);
    expect_class(FMRB_WIFI_TOML_ETC_PATH, FMRB_PATH_PASSWORD);
}

int main(void)
{
    test_classify();
    test_contains();
    test_get();
    test_put_rm();
    test_rename();
    test_summary();
    test_setup_mode();
    if (g_fail) {
        printf("secret_path: %d of %d checks FAILED\n", g_fail, g_count);
        return 1;
    }
    printf("secret_path: all %d checks passed\n", g_count);
    return 0;
}
