// Host test of the per-app working directory (doc/api_fixes F7).
//
// Two builds of this file (test/api_fixes/run.rb):
//
//   posix   the POSIX file HAL (the simulation, the browser build and the
//           native program) with test/native/stub, in a made-up tree under
//           /tmp: the flat table and the layered one.
//   esp32   the ESP32 namespace (platform/esp32/fmrb_hal_file_path_esp32.c,
//           which needs nothing from ESP-IDF) over a stand-in stat.
//
// Checked: an absolute path resolves exactly as before F7 whatever the working
// directory is (the tables below were taken from the resolver before it);
// relative paths, ".", "..", never above "/", too long, two apps with their
// own places, an app that ends goes back to "/", a task that is not an app
// stays at "/", the process's own working directory never moves, and the
// read-only and virtual-directory answers are made on the folded path (a
// relative path cannot reach the distribution tree or /mnt/root as a write).
//
// Prints "FAIL ..." lines and "cwd <build>: N checks" at the end.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fmrb_hal_file.h"
#ifndef CWD_TEST_ESP32
#include "fmrb_hal_file_layout.h"
#endif

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

// The app layer's slot function, played by a variable: -1 is a task that is
// not an app.
static int g_slot = -1;
static int test_slot(void) { return g_slot; }

static const char *resolve(const char *path) {
    static char out[512];
    if (fmrb_hal_file_resolve_path(path, out, sizeof(out)) != FMRB_OK) {
        return "(error)";
    }
    return out;
}

static const char *cwd(void) {
    static char buf[FMRB_HAL_FILE_CWD_MAX];
    if (fmrb_hal_file_getcwd(buf, sizeof(buf)) != FMRB_OK) {
        return "(error)";
    }
    return buf;
}

static size_t vcount(const char *path) {
    size_t n = 0;
    return fmrb_hal_file_virtual_children(path, &n) ? n : (size_t)-1;
}

// Generated from the resolver as it was before F7 (absolute paths only).
typedef struct { const char *path; int err; const char *out; int vcount; int ro; int entry_ro; } abs_case_t;
#ifndef CWD_TEST_ESP32
static const abs_case_t k_posix_flat[] = {
    { "/", 0, "flash/", -1, 0, 0 },
    { "/a.txt", 0, "flash/a.txt", -1, 0, 0 },
    { "/home", 0, "flash/home", -1, 0, 0 },
    { "/home/x", 0, "flash/home/x", -1, 0, 0 },
    { "/home/x/", 0, "flash/home/x/", -1, 0, 0 },
    { "/tmp", 0, "flash/tmp", -1, 0, 0 },
    { "/tmp/a", 0, "flash/tmp/a", -1, 0, 0 },
    { "/tmp/", 0, "flash/tmp/", -1, 0, 0 },
    { "/sd", 0, "flash/sd", -1, 0, 0 },
    { "/sd/x", 0, "flash/sd/x", -1, 0, 0 },
    { "/sdx", 0, "flash/sdx", -1, 0, 0 },
    { "/mnt", 0, "flash/mnt", -1, 0, 0 },
    { "/mnt/", 0, "flash/mnt/", -1, 0, 0 },
    { "/mnt/sd", 0, "flash/mnt/sd", -1, 0, 0 },
    { "/mnt/sd/a", 0, "flash/mnt/sd/a", -1, 0, 0 },
    { "/mnt/sdx", 0, "flash/mnt/sdx", -1, 0, 0 },
    { "/flash", 0, "flash/flash", -1, 0, 0 },
    { "/flash/x", 0, "flash/flash/x", -1, 0, 0 },
    { "/flashy", 0, "flash/flashy", -1, 0, 0 },
    { "/home/../x", 0, "flash/home/../x", -1, 0, 0 },
    { "/./a", 0, "flash/./a", -1, 0, 0 },
    { "//a", 0, "flash//a", -1, 0, 0 },
    { "/a//b", 0, "flash/a//b", -1, 0, 0 },
    { "/a/./b/../c", 0, "flash/a/./b/../c", -1, 0, 0 },
    { "/..", 0, "flash/..", -1, 0, 0 },
    { "/../..", 0, "flash/../..", -1, 0, 0 },
    { "/home/../../etc", 0, "flash/home/../../etc", -1, 0, 0 },
    { "/mnt/host", 0, "flash/mnt/host", -1, 0, 0 },
    { "/mnt/host/a", 0, "flash/mnt/host/a", -1, 0, 0 },
    { "/mnt/host/../x", 0, "flash/mnt/host/../x", -1, 0, 0 },
    { "/mnt/root/etc/x", 0, "flash/mnt/root/etc/x", -1, 0, 0 },
    { "/mnt/rootx", 0, "flash/mnt/rootx", -1, 0, 0 },
    { "/app", 0, "flash/app", -1, 0, 0 },
    { "/app/usr", 0, "flash/app/usr", -1, 0, 0 },
    { "/app/usr/x", 0, "flash/app/usr/x", -1, 0, 0 },
    { "/usr/share/x", 0, "flash/usr/share/x", -1, 0, 0 },
    { "/etc/a", 0, "flash/etc/a", -1, 0, 0 },
    { "/var/x", 0, "flash/var/x", -1, 0, 0 },
    { "/data", 0, "flash/data", -1, 0, 0 },
    { "/cache/y", 0, "flash/cache/y", -1, 0, 0 },
    { "/home/a b/c.txt", 0, "flash/home/a b/c.txt", -1, 0, 0 },
};
static const abs_case_t k_posix_layered[] = {
    { "/", 0, "/nonexist-f7/sys", 7, 1, 1 },
    { "/a.txt", 0, "/nonexist-f7/sys/a.txt", -1, 1, 1 },
    { "/home", 0, "/nonexist-f7/user/home", -1, 0, 0 },
    { "/home/x", 0, "/nonexist-f7/user/home/x", -1, 0, 0 },
    { "/home/x/", 0, "/nonexist-f7/user/home/x", -1, 0, 0 },
    { "/tmp", 0, "/nonexist-f7/cache/tmp", -1, 0, 0 },
    { "/tmp/a", 0, "/nonexist-f7/cache/tmp/a", -1, 0, 0 },
    { "/tmp/", 0, "/nonexist-f7/cache/tmp", -1, 0, 0 },
    { "/sd", 0, "/nonexist-f7/sys/sd", -1, 1, 1 },
    { "/sd/x", 0, "/nonexist-f7/sys/sd/x", -1, 1, 1 },
    { "/sdx", 0, "/nonexist-f7/sys/sdx", -1, 1, 1 },
    { "/mnt", 0, "/nonexist-f7/sys/mnt", 2, 1, 1 },
    { "/mnt/", 0, "/nonexist-f7/sys/mnt", 2, 1, 1 },
    { "/mnt/sd", 0, "/nonexist-f7/sys/mnt/sd", -1, 1, 1 },
    { "/mnt/sd/a", 0, "/nonexist-f7/sys/mnt/sd/a", -1, 1, 1 },
    { "/mnt/sdx", 0, "/nonexist-f7/sys/mnt/sdx", -1, 1, 1 },
    { "/flash", 0, "/nonexist-f7/sys/flash", -1, 1, 1 },
    { "/flash/x", 0, "/nonexist-f7/sys/flash/x", -1, 1, 1 },
    { "/flashy", 0, "/nonexist-f7/sys/flashy", -1, 1, 1 },
    { "/home/../x", 0, "/nonexist-f7/sys/x", -1, 1, 1 },
    { "/./a", 0, "/nonexist-f7/sys/a", -1, 1, 1 },
    { "//a", 0, "/nonexist-f7/sys/a", -1, 1, 1 },
    { "/a//b", 0, "/nonexist-f7/sys/a/b", -1, 1, 1 },
    { "/a/./b/../c", 0, "/nonexist-f7/sys/a/c", -1, 1, 1 },
    { "/..", 0, "/nonexist-f7/sys", 7, 1, 1 },
    { "/../..", 0, "/nonexist-f7/sys", 7, 1, 1 },
    { "/home/../../etc", 0, "/nonexist-f7/user/etc", -1, 0, 0 },
    { "/mnt/host", 0, "/nonexist-f7/home", -1, 1, 1 },
    { "/mnt/host/a", 0, "/nonexist-f7/home/a", -1, 1, 1 },
    { "/mnt/host/../x", 0, "/nonexist-f7/sys/mnt/x", -1, 1, 1 },
    { "/mnt/root/etc/x", 0, "/nonexist-f7/fs/etc/x", -1, 1, 1 },
    { "/mnt/rootx", 0, "/nonexist-f7/sys/mnt/rootx", -1, 1, 1 },
    { "/app", 0, "/nonexist-f7/sys/app", 1, 1, 1 },
    { "/app/usr", 0, "/nonexist-f7/user/app/usr", -1, 0, 0 },
    { "/app/usr/x", 0, "/nonexist-f7/user/app/usr/x", -1, 0, 0 },
    { "/usr/share/x", 0, "/nonexist-f7/sys/usr/share/x", -1, 1, 1 },
    { "/etc/a", 0, "/nonexist-f7/user/etc/a", -1, 0, 0 },
    { "/var/x", 0, "/nonexist-f7/cache/var/x", -1, 0, 0 },
    { "/data", 0, "/nonexist-f7/cache/data", -1, 0, 0 },
    { "/cache/y", 0, "/nonexist-f7/cache/cache/y", -1, 0, 0 },
    { "/home/a b/c.txt", 0, "/nonexist-f7/user/home/a b/c.txt", -1, 0, 0 },
};
#else
static const abs_case_t k_esp32[] = {
    { "/", 0, "/flash/", 2, 0, 0 },
    { "/a.txt", 0, "/flash/a.txt", -1, 0, 0 },
    { "/home", 0, "/flash/home", -1, 0, 0 },
    { "/home/x", 0, "/flash/home/x", -1, 0, 0 },
    { "/home/x/", 0, "/flash/home/x/", -1, 0, 0 },
    { "/tmp", 0, "/tmp", -1, 0, 0 },
    { "/tmp/a", 0, "/tmp/a", -1, 0, 0 },
    { "/tmp/", 0, "/tmp/", -1, 0, 0 },
    { "/sd", 0, "/sd", -1, 0, 0 },
    { "/sd/x", 0, "/sd/x", -1, 0, 0 },
    { "/sdx", 0, "/flash/sdx", -1, 0, 0 },
    { "/mnt", 0, "/flash/mnt", 1, 0, 0 },
    { "/mnt/", 0, "/flash/mnt/", -1, 0, 0 },
    { "/mnt/sd", 0, "/sd", -1, 0, 0 },
    { "/mnt/sd/a", 0, "/sd/a", -1, 0, 0 },
    { "/mnt/sdx", 0, "/flash/mnt/sdx", -1, 0, 0 },
    { "/flash", 0, "/flash", -1, 0, 0 },
    { "/flash/x", 0, "/flash/x", -1, 0, 0 },
    { "/flashy", 0, "/flash/flashy", -1, 0, 0 },
    { "/home/../x", 0, "/flash/home/../x", -1, 0, 0 },
    { "/./a", 0, "/flash/./a", -1, 0, 0 },
    { "//a", 0, "/flash//a", -1, 0, 0 },
    { "/a//b", 0, "/flash/a//b", -1, 0, 0 },
    { "/a/./b/../c", 0, "/flash/a/./b/../c", -1, 0, 0 },
    { "/..", 0, "/flash/..", -1, 0, 0 },
    { "/../..", 0, "/flash/../..", -1, 0, 0 },
    { "/home/../../etc", 0, "/flash/home/../../etc", -1, 0, 0 },
    { "/mnt/host", 0, "/flash/mnt/host", -1, 0, 0 },
    { "/mnt/host/a", 0, "/flash/mnt/host/a", -1, 0, 0 },
    { "/mnt/host/../x", 0, "/flash/mnt/host/../x", -1, 0, 0 },
    { "/mnt/root/etc/x", 0, "/flash/mnt/root/etc/x", -1, 0, 0 },
    { "/mnt/rootx", 0, "/flash/mnt/rootx", -1, 0, 0 },
    { "/app", 0, "/flash/app", -1, 0, 0 },
    { "/app/usr", 0, "/flash/app/usr", -1, 0, 0 },
    { "/app/usr/x", 0, "/flash/app/usr/x", -1, 0, 0 },
    { "/usr/share/x", 0, "/flash/usr/share/x", -1, 0, 0 },
    { "/etc/a", 0, "/flash/etc/a", -1, 0, 0 },
    { "/var/x", 0, "/flash/var/x", -1, 0, 0 },
    { "/data", 0, "/flash/data", -1, 0, 0 },
    { "/cache/y", 0, "/flash/cache/y", -1, 0, 0 },
    { "/home/a b/c.txt", 0, "/flash/home/a b/c.txt", -1, 0, 0 },
};
#endif

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void check_abs_table(const char *what, const abs_case_t *t, size_t n, size_t out_len) {
    char out[512];
    for (size_t i = 0; i < n; i++) {
        memset(out, 0, sizeof(out));
        fmrb_err_t err = fmrb_hal_file_resolve_path(t[i].path, out, out_len);
        CHECK(err == t[i].err && (err != FMRB_OK || strcmp(out, t[i].out) == 0),
              "%s (cwd %s): %s -> %d \"%s\", was %d \"%s\"", what, cwd(), t[i].path, err, out,
              t[i].err, t[i].out);
        size_t c = vcount(t[i].path);
        CHECK((t[i].vcount < 0 && c == (size_t)-1) || c == (size_t)t[i].vcount,
              "%s (cwd %s): children of %s", what, cwd(), t[i].path);
#ifndef CWD_TEST_ESP32
        CHECK(fmrb_hal_file_path_is_read_only(t[i].path) == (bool)t[i].ro &&
              fmrb_hal_file_entry_is_read_only(t[i].path) == (bool)t[i].entry_ro,
              "%s (cwd %s): read-only of %s", what, cwd(), t[i].path);
#endif
    }
}

// What every build shares: no slot function, a non-app task, two apps, the
// end of an app, lengths.
static void check_common(void) {
    fmrb_hal_file_set_cwd_slot(NULL);
    CHECK(strcmp(cwd(), "/") == 0, "no slot function: \"/\" (%s)", cwd());
    CHECK(fmrb_hal_file_chdir("/home") == FMRB_ERR_NOT_SUPPORTED, "no slot function: chdir refused");
    fmrb_hal_file_set_cwd_slot(test_slot);

    g_slot = -1;
    CHECK(strcmp(cwd(), "/") == 0, "a non-app task is at \"/\"");
    CHECK(fmrb_hal_file_chdir("/home") == FMRB_ERR_NOT_SUPPORTED, "a non-app task cannot chdir");

    g_slot = 0;
    CHECK(strcmp(cwd(), "/") == 0, "an app starts at \"/\"");
    CHECK(fmrb_hal_file_chdir("/home") == FMRB_OK, "chdir /home");
    CHECK(strcmp(cwd(), "/home") == 0, "getwd /home (%s)", cwd());
    CHECK(fmrb_hal_file_chdir("/home/") == FMRB_OK && strcmp(cwd(), "/home") == 0, "trailing / folded");
    CHECK(fmrb_hal_file_chdir("/home/./../home//") == FMRB_OK && strcmp(cwd(), "/home") == 0,
          "absolute chdir folded (%s)", cwd());
    CHECK(fmrb_hal_file_chdir("..") == FMRB_OK && strcmp(cwd(), "/") == 0, "chdir .. to / (%s)", cwd());
    CHECK(fmrb_hal_file_chdir("../../..") == FMRB_OK && strcmp(cwd(), "/") == 0, "never above / (%s)", cwd());
    CHECK(fmrb_hal_file_chdir("home") == FMRB_OK && strcmp(cwd(), "/home") == 0, "relative chdir (%s)", cwd());
    CHECK(fmrb_hal_file_chdir(".") == FMRB_OK && strcmp(cwd(), "/home") == 0, "chdir . (%s)", cwd());
    CHECK(fmrb_hal_file_chdir("/nope-f7") == FMRB_ERR_NOT_FOUND, "chdir to nothing");
    CHECK(fmrb_hal_file_chdir("") == FMRB_ERR_NOT_FOUND, "chdir to \"\"");
    CHECK(fmrb_hal_file_chdir("/home/f7.txt") == FMRB_ERR_INVALID_STATE, "chdir to a file");
    CHECK(strcmp(cwd(), "/home") == 0, "a refused chdir leaves it (%s)", cwd());

    // The longest place: FMRB_HAL_FILE_CWD_MAX - 1 bytes.
    char longest[FMRB_HAL_FILE_CWD_MAX + 8];
    memset(longest, 'a', sizeof(longest));
    memcpy(longest, "/home/", 6);
    longest[FMRB_HAL_FILE_CWD_MAX - 1] = '\0';
    CHECK(fmrb_hal_file_chdir(longest) == FMRB_OK && strlen(cwd()) == FMRB_HAL_FILE_CWD_MAX - 1,
          "the longest place (%zu)", strlen(cwd()));
    char small[8];
    CHECK(fmrb_hal_file_getcwd(small, sizeof(small)) == FMRB_ERR_INVALID_PARAM, "getcwd: buffer too small");
    longest[FMRB_HAL_FILE_CWD_MAX - 1] = 'a';
    longest[FMRB_HAL_FILE_CWD_MAX] = '\0';
    CHECK(fmrb_hal_file_chdir(longest) == FMRB_ERR_INVALID_PARAM, "one byte too long");
    CHECK(strlen(cwd()) == FMRB_HAL_FILE_CWD_MAX - 1, "too long leaves it");
    CHECK(fmrb_hal_file_chdir("b") == FMRB_ERR_INVALID_PARAM, "relative chdir past the bound");
    CHECK(fmrb_hal_file_chdir("/home") == FMRB_OK, "back to /home");

    // Two apps.
    g_slot = 1;
    CHECK(strcmp(cwd(), "/") == 0, "another app is still at \"/\"");
    CHECK(fmrb_hal_file_chdir("/tmp") == FMRB_OK && strcmp(cwd(), "/tmp") == 0, "app 1 at /tmp");
    g_slot = 0;
    CHECK(strcmp(cwd(), "/home") == 0, "app 0 still at /home (%s)", cwd());
    g_slot = -1;
    CHECK(strcmp(cwd(), "/") == 0, "a non-app task still at \"/\"");

    // The end of an app.
    fmrb_hal_file_cwd_forget(0);
    g_slot = 0;
    CHECK(strcmp(cwd(), "/") == 0, "an ended app's slot is at \"/\" (%s)", cwd());
    g_slot = 1;
    CHECK(strcmp(cwd(), "/tmp") == 0, "the other app keeps its place");
    fmrb_hal_file_cwd_forget(1);
    fmrb_hal_file_cwd_forget(-1);
    fmrb_hal_file_cwd_forget(1000);
    g_slot = 1000;
    CHECK(strcmp(cwd(), "/") == 0, "a slot out of range is at \"/\"");
    CHECK(fmrb_hal_file_chdir("/home") == FMRB_ERR_NOT_SUPPORTED, "a slot out of range cannot chdir");
    g_slot = 0;
}

#ifdef CWD_TEST_ESP32

// The stand-in for the platform's stat: a name ending in ".txt" is a file,
// one containing "nope" is not there, everything else is a directory.
fmrb_err_t fmrb_hal_file_stat(const char *path, fmrb_file_info_t *info) {
    if (strstr(path, "nope") != NULL) {
        return FMRB_ERR_FAILED;
    }
    memset(info, 0, sizeof(*info));
    size_t n = strlen(path);
    bool file = n > 4 && strcmp(path + n - 4, ".txt") == 0;
    info->mode = file ? FMRB_S_IFREG : FMRB_S_IFDIR;
    info->is_dir = !file;
    return FMRB_OK;
}

static void check_esp32(void) {
    g_slot = 0;
    check_abs_table("esp32 at /", k_esp32, COUNT(k_esp32), 128);
    CHECK(fmrb_hal_file_chdir("/sd/music") == FMRB_OK, "chdir /sd/music");
    check_abs_table("esp32 at /sd/music", k_esp32, COUNT(k_esp32), 128);
    fmrb_hal_file_cwd_forget(0);

    // At "/" a relative path names what the absolute one does.
    CHECK(strcmp(resolve("a.txt"), "/flash/a.txt") == 0, "a.txt at / (%s)", resolve("a.txt"));
    CHECK(strcmp(resolve("home/a.txt"), "/flash/home/a.txt") == 0, "home/a.txt at /");
    CHECK(strcmp(resolve("./a.txt"), "/flash/a.txt") == 0, "./a.txt folded");
    CHECK(strcmp(resolve("../../a.txt"), "/flash/a.txt") == 0, "../../a.txt stays under /");
    CHECK(strcmp(resolve("tmp/a.txt"), "/tmp/a.txt") == 0, "tmp/a.txt is /tmp (was /flash/tmp)");
    CHECK(strcmp(resolve("sd/a.txt"), "/sd/a.txt") == 0, "sd/a.txt is the card");
    CHECK(strcmp(resolve("mnt/sd/a.txt"), "/sd/a.txt") == 0, "mnt/sd/a.txt is the card");
    CHECK(vcount(".") == 2 && vcount("") == 2, "\".\" at / is the virtual root");
    CHECK(vcount("mnt") == 1 && vcount("mnt/") == 1, "mnt at / is the virtual /mnt");

    CHECK(fmrb_hal_file_chdir("/tmp") == FMRB_OK, "chdir /tmp");
    CHECK(strcmp(resolve("a.txt"), "/tmp/a.txt") == 0, "a.txt at /tmp (%s)", resolve("a.txt"));
    CHECK(strcmp(resolve("../home/x"), "/flash/home/x") == 0, "../home/x at /tmp");
    CHECK(vcount("..") == 2, ".. at /tmp is the virtual root");
    CHECK(fmrb_hal_file_chdir("/mnt") == FMRB_OK && strcmp(cwd(), "/mnt") == 0, "chdir to the virtual /mnt");
    CHECK(strcmp(resolve("sd/b.txt"), "/sd/b.txt") == 0, "sd/b.txt at /mnt");
    CHECK(vcount(".") == 1, ". at /mnt is the virtual /mnt");
    CHECK(fmrb_hal_file_chdir("sd") == FMRB_OK && strcmp(cwd(), "/mnt/sd") == 0, "chdir sd from /mnt");
    CHECK(strcmp(resolve("c.txt"), "/sd/c.txt") == 0, "c.txt at /mnt/sd");

    // Longer than the device's bound: nothing is resolved.
    char longest[FMRB_HAL_FILE_CWD_MAX];
    memset(longest, 'a', sizeof(longest));
    memcpy(longest, "/home/", 6);
    longest[FMRB_HAL_FILE_CWD_MAX - 1] = '\0';
    CHECK(fmrb_hal_file_chdir(longest) == FMRB_OK, "chdir to the longest place");
    char out[128] = "x";
    CHECK(fmrb_hal_file_resolve_path("b.txt", out, sizeof(out)) == FMRB_ERR_INVALID_PARAM && out[0] == '\0',
          "a relative path past the bound does not resolve");
    CHECK(vcount("b") == (size_t)-1, "nor is it a virtual directory");
    fmrb_hal_file_cwd_forget(0);
}

int main(void) {
    check_common();
    check_esp32();
    printf("cwd esp32: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}

#else  // the POSIX file HAL

static char g_t[64];
static char g_start[512];

static bool host_exists(const char *rel) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", g_t, rel);
    struct stat st;
    return stat(p, &st) == 0;
}

static bool write_file(const char *path) {
    fmrb_file_t f;
    if (fmrb_hal_file_open(path, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &f) != FMRB_OK) {
        return false;
    }
    size_t w = 0;
    fmrb_hal_file_write(f, "x", 1, &w);
    fmrb_hal_file_close(f);
    return w == 1;
}

static bool process_cwd_is(const char *want) {
    char now[512];
    return getcwd(now, sizeof(now)) != NULL && strcmp(now, want) == 0;
}

static void check_flat(void) {
    g_slot = 0;
    check_abs_table("flat at /", k_posix_flat, COUNT(k_posix_flat), 512);
    CHECK(fmrb_hal_file_chdir("/home") == FMRB_OK, "chdir /home");
    check_abs_table("flat at /home", k_posix_flat, COUNT(k_posix_flat), 512);
    CHECK(process_cwd_is(g_t), "the process's working directory did not move");
    fmrb_hal_file_cwd_forget(0);

    CHECK(strcmp(resolve("a.txt"), "flash/a.txt") == 0, "a.txt at / (%s)", resolve("a.txt"));
    CHECK(strcmp(resolve("../a.txt"), "flash/a.txt") == 0, "../a.txt stays in flash/ (%s)", resolve("../a.txt"));
    CHECK(strcmp(resolve(""), "flash/") == 0, "\"\" at / (%s)", resolve(""));

    CHECK(fmrb_hal_file_chdir("/tmp") == FMRB_OK, "chdir /tmp");
    CHECK(strcmp(resolve("a.txt"), "flash/tmp/a.txt") == 0, "a.txt at /tmp (%s)", resolve("a.txt"));
    CHECK(write_file("a.txt") && host_exists("flash/tmp/a.txt"), "File.open(\"a.txt\", \"w\") at /tmp");
    fmrb_file_info_t info;
    CHECK(fmrb_hal_file_stat("/tmp/a.txt", &info) == FMRB_OK, "and /tmp/a.txt is there");
    CHECK(fmrb_hal_file_stat("a.txt", &info) == FMRB_OK, "and a.txt is there from /tmp");
    CHECK(fmrb_hal_file_rename("a.txt", "b.txt") == FMRB_OK && host_exists("flash/tmp/b.txt"),
          "rename relative");
    CHECK(fmrb_hal_file_remove("b.txt") == FMRB_OK && !host_exists("flash/tmp/b.txt"), "remove relative");
    CHECK(fmrb_hal_file_mkdir("../home/f7dir") == FMRB_OK && host_exists("flash/home/f7dir"), "mkdir ../home/f7dir");
    CHECK(fmrb_hal_file_rmdir("/home/f7dir") == FMRB_OK, "rmdir it");
    fmrb_dir_t d;
    CHECK(fmrb_hal_file_opendir(".", &d) == FMRB_OK && fmrb_hal_file_closedir(d) == FMRB_OK, "opendir .");

    // Another app does not see it, and absolute paths are its own.
    g_slot = 1;
    CHECK(strcmp(resolve("a.txt"), "flash/a.txt") == 0, "app 1 at / is not moved by app 0");
    CHECK(write_file("/home/f7.txt") && host_exists("flash/home/f7.txt"), "app 1 writes /home/f7.txt");
    g_slot = 0;
    CHECK(fmrb_hal_file_stat("/home/f7.txt", &info) == FMRB_OK, "app 0 at /tmp sees /home/f7.txt");
    CHECK(process_cwd_is(g_t), "the process's working directory did not move");
    fmrb_hal_file_cwd_forget(0);
}

static void mkdirs(const char *rel) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", g_t, rel);
    for (char *s = p + strlen(g_t) + 1; *s; s++) {
        if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
    }
    mkdir(p, 0755);
}

static void check_layered(void) {
    // The tables were taken with these roots, none of which is there.
    fmrb_hal_file_layout_t fixed = {
        "/nonexist-f7/sys", "/nonexist-f7/user", "/nonexist-f7/cache", NULL,
        "/nonexist-f7/home", "/nonexist-f7/fs",
    };
    CHECK(fmrb_hal_file_set_layout(&fixed) == FMRB_OK, "the fixed layout");
    g_slot = 0;
    check_abs_table("layered at /", k_posix_layered, COUNT(k_posix_layered), 512);

    // The working ones, under the made-up tree.
    char sys[128], user[128], cache[128], home[128], fs[128];
    snprintf(sys, sizeof(sys), "%s/sys", g_t);
    snprintf(user, sizeof(user), "%s/user", g_t);
    snprintf(cache, sizeof(cache), "%s/cache", g_t);
    snprintf(home, sizeof(home), "%s/hosthome", g_t);
    snprintf(fs, sizeof(fs), "%s/hostfs", g_t);
    mkdirs("sys/usr/share");
    mkdirs("sys/etc");
    mkdirs("hosthome");
    mkdirs("hostfs/etc");
    fmrb_hal_file_layout_t l = { sys, user, cache, NULL, home, fs };
    CHECK(fmrb_hal_file_set_layout(&l) == FMRB_OK, "the layout");
    CHECK(fmrb_hal_file_init() == FMRB_OK, "init");

    CHECK(fmrb_hal_file_chdir("/home") == FMRB_OK, "chdir /home (user root)");
    // With the working directory moved, the absolute answers still hold.
    CHECK(fmrb_hal_file_set_layout(&fixed) == FMRB_OK, "the fixed layout again");
    check_abs_table("layered at /home", k_posix_layered, COUNT(k_posix_layered), 512);
    CHECK(fmrb_hal_file_set_layout(&l) == FMRB_OK, "the working layout again");

    CHECK(!fmrb_hal_file_path_is_read_only("a.txt"), "a.txt at /home is writable");
    CHECK(fmrb_hal_file_path_is_read_only("../usr/a.txt"), "../usr/a.txt is the distribution");
    CHECK(fmrb_hal_file_path_is_read_only("../../../../usr/a.txt"), "and from further up");
    CHECK(fmrb_hal_file_entry_is_read_only("../usr"), "../usr cannot be removed");
    CHECK(fmrb_hal_file_open("../usr/evil.txt", FMRB_O_WRONLY | FMRB_O_CREAT, &(fmrb_file_t){0}) ==
              FMRB_ERR_READ_ONLY && !host_exists("sys/usr/evil.txt"),
          "writing ../usr/evil.txt is refused");
    CHECK(fmrb_hal_file_mkdir("../usr/evil") == FMRB_ERR_READ_ONLY, "mkdir ../usr/evil is refused");
    CHECK(write_file("mine.txt") && host_exists("user/home/mine.txt"), "mine.txt lands in the user root");
    CHECK(fmrb_hal_file_rename("mine.txt", "../usr/mine.txt") == FMRB_ERR_READ_ONLY &&
              host_exists("user/home/mine.txt"),
          "renaming into the distribution is refused");
    CHECK(vcount("..") > 0 && vcount(".") == (size_t)-1, ".. at /home is the root's list");

    CHECK(fmrb_hal_file_chdir("/usr") == FMRB_OK, "chdir into the distribution (reading is fine)");
    CHECK(fmrb_hal_file_path_is_read_only("x.txt"), "x.txt at /usr is read-only");
    CHECK(fmrb_hal_file_open("x.txt", FMRB_O_WRONLY | FMRB_O_CREAT, &(fmrb_file_t){0}) == FMRB_ERR_READ_ONLY &&
              !host_exists("sys/usr/x.txt"),
          "writing x.txt at /usr is refused");
    CHECK(!fmrb_hal_file_path_is_read_only("../home/x.txt"), "../home/x.txt at /usr is writable");

    CHECK(fmrb_hal_file_chdir("/mnt/host") == FMRB_OK, "chdir /mnt/host");
    CHECK(write_file("h.txt") && host_exists("hosthome/h.txt"), "h.txt lands in the home");
    CHECK(fmrb_hal_file_path_is_read_only("../root/etc/x"), "../root/etc/x is /mnt/root");
    CHECK(fmrb_hal_file_path_is_read_only("../../../mnt/root/etc/x"), "and from further up");
    CHECK(strcmp(resolve("../../../../etc"), resolve("/etc")) == 0, "never above / from /mnt/host");
    CHECK(fmrb_hal_file_chdir("../root/etc") == FMRB_OK && strcmp(cwd(), "/mnt/root/etc") == 0,
          "chdir ../root/etc (%s)", cwd());
    CHECK(fmrb_hal_file_open("p.txt", FMRB_O_WRONLY | FMRB_O_CREAT, &(fmrb_file_t){0}) == FMRB_ERR_READ_ONLY &&
              !host_exists("hostfs/etc/p.txt"),
          "writing p.txt under /mnt/root is refused");
    CHECK(fmrb_hal_file_chdir("/mnt") == FMRB_OK && vcount(".") == 2, "/mnt lists host and root");
    CHECK(process_cwd_is(g_t), "the process's working directory did not move");
    fmrb_hal_file_cwd_forget(0);
}

int main(void) {
    snprintf(g_t, sizeof(g_t), "/tmp/fmrb_cwd_test_XXXXXX");
    if (mkdtemp(g_t) == NULL || getcwd(g_start, sizeof(g_start)) == NULL || chdir(g_t) != 0) {
        printf("FAIL cannot make the test tree\n");
        return 1;
    }
    // The flat table roots everything in the relative "flash/".
    if (fmrb_hal_file_init() != FMRB_OK) {
        printf("FAIL init\n");
        return 1;
    }
    mkdir("flash/home", 0755);
    FILE *f = fopen("flash/home/f7.txt", "w");
    if (f) fclose(f);
    // The longest place check_common goes to.
    char longest[FMRB_HAL_FILE_CWD_MAX + 8];
    memset(longest, 'a', sizeof(longest));
    memcpy(longest, "flash/home/", 11);
    longest[5 + FMRB_HAL_FILE_CWD_MAX - 1] = '\0';
    mkdir(longest, 0755);

    check_common();
    check_flat();
    check_layered();

    if (chdir(g_start) == 0) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", g_t);
        if (system(cmd) != 0) printf("(could not remove %s)\n", g_t);
    }
    printf("cwd posix: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}

#endif
