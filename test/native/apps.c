// Host test of the Linux PC's applications for the launcher (doc/linux_native
// L9b): native/sys/linux_apps.c itself, which is plain POSIX (and libpng for
// the icons, when the host has it: NATIVE_HAVE_PNG).
//
// Checked: the Exec field split by the Desktop Entry rules (quoting, field
// codes); the entries of a made-up XDG tree under /tmp (XDG_DATA_HOME first,
// the first desktop file id winning, a subdirectory's id, NoDisplay, Hidden,
// OnlyShowIn, NotShowIn, TryExec, a missing program, Type=Link,
// Terminal=true run in the terminal, Name[ja]); the list's text; an icon
// made from a hicolor PNG and the group's mark; starting an entry (no shell,
// a session of its own, XDG_CURRENT_DESKTOP, the working directory); a
// window's name by its process and by its app id; and the end, which leaves
// no process of the started session (a background child included).
//
// Everything is under a directory of its own in /tmp (HOME, XDG_DATA_HOME,
// XDG_DATA_DIRS, PATH's first directory); nothing outside it is written.
//
// Usage: native_apps_test

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if NATIVE_HAVE_PNG
#include <png.h>
#endif

#include "linux_apps.h"

static int g_fail = 0;
static int g_count = 0;
static char g_root[96];

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

static void nap(int ms)
{
    struct timespec ts = { 0, (long)ms * 1000000L };
    nanosleep(&ts, NULL);
}

static void write_file(const char *rel, const char *text, mode_t mode)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_root, rel);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    for (char *p = dir + strlen(g_root) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir, 0700);
            *p = '/';
        }
    }
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        printf("cannot write %s\n", path);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
    chmod(path, mode);
}

static char *read_file(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "r");
    if (f == NULL) return NULL;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static bool argv_is(char **argv, const char *const *want)
{
    int i = 0;
    for (; want[i] != NULL; i++) {
        if (argv == NULL || argv[i] == NULL || strcmp(argv[i], want[i]) != 0) return false;
    }
    return argv != NULL && argv[i] == NULL;
}

static void show(char **argv, char *out, size_t cap)
{
    out[0] = '\0';
    for (int i = 0; argv && argv[i]; i++) {
        size_t n = strlen(out);
        snprintf(out + n, cap - n, "[%s]", argv[i]);
    }
}

static void test_split(void)
{
    struct {
        const char *exec;
        const char *want[8];
    } cases[] = {
        { "foo %U", { "foo", NULL } },
        { "foo --open=%u bar", { "foo", "--open=", "bar", NULL } },
        { "\"/opt/my app/run\" %i --name %c", { "/opt/my app/run", "--icon", "app-icon", "--name", "My App", NULL } },
        { "sh -c \"echo \\\"hi\\\" \\$HOME\"", { "sh", "-c", "echo \"hi\" $HOME", NULL } },
        { "app 100%% %k", { "app", "100%", "/x/app.desktop", NULL } },
        { "  spaced\targs  ", { "spaced", "args", NULL } },
        { "app %f %F %d %D %n %N %v %m", { "app", NULL } },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char **a = linux_apps_split_exec(cases[i].exec, "My App", "app-icon", "/x/app.desktop");
        char got[256];
        show(a, got, sizeof(got));
        CHECK(argv_is(a, cases[i].want), "split \"%s\" gave %s", cases[i].exec, got);
        linux_apps_free_argv(a);
    }
    CHECK(linux_apps_split_exec("", NULL, NULL, NULL) == NULL, "empty Exec gives no argv");
    CHECK(linux_apps_split_exec("%U", NULL, NULL, NULL) == NULL, "only a field code gives no argv");
}

#if NATIVE_HAVE_PNG
// A 48x48 PNG: opaque red in the left half, transparent in the right.
static void write_png(const char *rel)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_root, rel);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    for (char *p = dir + strlen(g_root) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir, 0700);
            *p = '/';
        }
    }
    static uint8_t px[48 * 48 * 4];
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 48; x++) {
            uint8_t *p = &px[(y * 48 + x) * 4];
            p[0] = 255;
            p[1] = 0;
            p[2] = 0;
            p[3] = x < 24 ? 255 : 0;
        }
    }
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    img.width = 48;
    img.height = 48;
    img.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&img, path, 0, px, 0, NULL)) {
        printf("cannot write %s\n", path);
        exit(2);
    }
}
#endif

static void make_tree(void)
{
    // The programs: a fake app that says what it sees and leaves a child
    // in the background, and a fake terminal (so Terminal=true is decided
    // by this tree, not by what the host has).
    write_file("bin/fakeapp",
               "#!/bin/sh\n"
               "echo \"$XDG_CURRENT_DESKTOP|$(pwd)|$1\" > \"$FAKE_OUT\"\n"
               "sleep 30 &\n"
               "exec sleep 31\n",
               0755);
    write_file("bin/foot", "#!/bin/sh\nexit 0\n", 0755);
    write_file("home/applications/fake.desktop",
               "[Desktop Entry]\nType=Application\nName=Fake\nName[ja]=\xe5\x81\xbd\xe7\x89\xa9\n"
               "Name[de]=Falsch\nExec=fakeapp first %F\nIcon=fakeicon\nStartupWMClass=FakeClass\n"
               "Path=/tmp\n"
               "[Desktop Action other]\nName=Other\nExec=other\n",
               0644);
    write_file("sys1/applications/fake.desktop",
               "[Desktop Entry]\nType=Application\nName=Shadowed\nExec=fakeapp\n", 0644);
    write_file("sys1/applications/hidden.desktop",
               "[Desktop Entry]\nType=Application\nName=NoDisplay\nExec=fakeapp\nNoDisplay=true\n", 0644);
    write_file("sys1/applications/gone.desktop",
               "[Desktop Entry]\nType=Application\nName=Hidden\nExec=fakeapp\nHidden=true\n", 0644);
    write_file("sys1/applications/kde.desktop",
               "[Desktop Entry]\nType=Application\nName=KdeOnly\nExec=fakeapp\nOnlyShowIn=KDE;\n", 0644);
    write_file("sys1/applications/ours.desktop",
               "[Desktop Entry]\nType=Application\nName=Ours Only\nExec=fakeapp\nOnlyShowIn=KDE;family-mruby;\n",
               0644);
    write_file("sys1/applications/notus.desktop",
               "[Desktop Entry]\nType=Application\nName=NotUs\nExec=fakeapp\nNotShowIn=family-mruby;\n", 0644);
    write_file("sys1/applications/tryexec.desktop",
               "[Desktop Entry]\nType=Application\nName=TryExec\nExec=fakeapp\nTryExec=no-such-program-here\n",
               0644);
    write_file("sys1/applications/noprog.desktop",
               "[Desktop Entry]\nType=Application\nName=NoProgram\nExec=no-such-program-here --x\n", 0644);
    write_file("sys1/applications/link.desktop", "[Desktop Entry]\nType=Link\nName=Link\nURL=http://x\n", 0644);
    write_file("sys1/applications/term.desktop",
               "[Desktop Entry]\nType=Application\nName=In A Terminal\nExec=fakeapp\nTerminal=true\n", 0644);
    write_file("sys2/applications/sub/nested.desktop",
               "[Desktop Entry]\nType=Application\nName=Nested\nExec=fakeapp\nIcon=no-such-icon\n", 0644);
#if NATIVE_HAVE_PNG
    write_png("sys1/icons/hicolor/48x48/apps/fakeicon.png");
#endif
}

static char g_list[16384];

static int32_t list(int mode, const char *lang)
{
    int32_t n = linux_apps_list(mode, lang, g_list, sizeof(g_list) - 1);
    if (n >= 0 && (size_t)n < sizeof(g_list)) g_list[n] = '\0';
    return n;
}

// The line of the list with this id, or NULL.
static const char *line_of(const char *id)
{
    static char line[512];
    char needle[300];
    snprintf(needle, sizeof(needle), "\t%s\t", id);
    const char *p = strstr(g_list, needle);
    if (p == NULL) return NULL;
    const char *a = p;
    while (a > g_list && a[-1] != '\n') a--;
    const char *b = strchr(p + 1, '\n');
    snprintf(line, sizeof(line), "%.*s", (int)(b ? b - a : (long)strlen(a)), a);
    return line;
}

// Live processes whose session is sid.
static int session_count(pid_t sid)
{
    DIR *d = opendir("/proc");
    int n = 0;
    struct dirent *de;
    while (d && (de = readdir(d)) != NULL) {
        char *end;
        long pid = strtol(de->d_name, &end, 10);
        if (*end != '\0' || pid <= 0) continue;
        char path[64], buf[512];
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        FILE *f = fopen(path, "r");
        if (f == NULL) continue;
        size_t len = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[len] = '\0';
        char *p = strrchr(buf, ')');
        char st;
        long ppid, pgrp, sess;
        if (p && sscanf(p + 1, " %c %ld %ld %ld", &st, &ppid, &pgrp, &sess) == 4 && sess == sid && st != 'Z') n++;
    }
    if (d) closedir(d);
    return n;
}

static void test_tree(void)
{
    char path[600];
    make_tree();
    snprintf(path, sizeof(path), "%s/home", g_root);
    setenv("XDG_DATA_HOME", path, 1);
    char dirs[600];
    snprintf(dirs, sizeof(dirs), "%s/sys1:%s/sys2", g_root, g_root);
    setenv("XDG_DATA_DIRS", dirs, 1);
    char p[1200];
    snprintf(p, sizeof(p), "%s/bin:/usr/bin:/bin", g_root);
    setenv("PATH", p, 1);
    snprintf(path, sizeof(path), "%s/out", g_root);
    setenv("FAKE_OUT", path, 1);

    CHECK(!linux_apps_available(), "not available before the start");
    CHECK(linux_apps_list(LINUX_APPS_LIST, "ja", g_list, sizeof(g_list)) == -1, "no list before the start");

    char icons[600];
    snprintf(icons, sizeof(icons), "%s/cache/var/cache/linux", g_root);
    linux_apps_config_t cfg = { .icon_dir = icons, .icon_fm_dir = "/var/cache/linux", .desktop_name = "family-mruby" };
    list(LINUX_APPS_HEADER, "ja");   // still not started: nothing
    linux_apps_start(&cfg);
    CHECK(linux_apps_available(), "available after the start");
    list(LINUX_APPS_LIST, "ja");
    CHECK(linux_apps_wait_scan(5000), "the read ends");
    int32_t n = list(LINUX_APPS_LIST, "ja");
    CHECK(n > 0 && strncmp(g_list, "ready\t/var/cache/linux/linux.bmp\n", 33) == 0, "header: %.40s", g_list);
    if (snprintf(path, sizeof(path), "%s/linux.bmp", icons) < 0) return;
    struct stat st;
    CHECK(stat(path, &st) == 0 && st.st_size == 54 + 1024 + 24 * 24, "the group's mark is a 24x24 8-bit BMP (%ld)",
          (long)st.st_size);

    const char *l = line_of("fake.desktop");
    CHECK(l != NULL && strncmp(l, "\xe5\x81\xbd\xe7\x89\xa9\t", 7) == 0, "Name[ja] and XDG_DATA_HOME first: %s",
          l ? l : "(none)");
    CHECK(strstr(g_list, "Shadowed") == NULL, "the same id later in XDG_DATA_DIRS is not listed");
    const char *gone[] = { "hidden.desktop", "gone.desktop", "kde.desktop", "notus.desktop", "tryexec.desktop",
                           "noprog.desktop", "link.desktop" };
    for (size_t i = 0; i < sizeof(gone) / sizeof(gone[0]); i++) {
        CHECK(line_of(gone[i]) == NULL, "%s left out", gone[i]);
    }
    CHECK(line_of("ours.desktop") != NULL, "OnlyShowIn with our name is listed");
    CHECK(line_of("term.desktop") != NULL, "Terminal=true listed when there is a terminal");
    l = line_of("sub-nested.desktop");
    CHECK(l != NULL && strcmp(l, "Nested\tsub-nested.desktop\t") == 0, "a subdirectory's id, no icon: %s",
          l ? l : "(none)");
    // Sorted by name: "In A Terminal" < "Nested" < "Ours Only" < (the
    // Japanese name, bytes above ASCII).
    const char *a = strstr(g_list, "In A Terminal"), *b = strstr(g_list, "Nested\t"),
               *c = strstr(g_list, "Ours Only"), *d = strstr(g_list, "\xe5\x81\xbd");
    CHECK(a && b && c && d && a < b && b < c && c < d, "sorted by name");
    int lines = 0;
    for (const char *q = g_list; *q; q++) lines += *q == '\n';
    CHECK(lines == 5, "4 entries and the header (%d lines)", lines);

#if NATIVE_HAVE_PNG
    l = line_of("fake.desktop");
    const char *icon = l ? strrchr(l, '\t') + 1 : "";
    CHECK(strncmp(icon, "/var/cache/linux/lx", 19) == 0, "an icon from the hicolor PNG: %s", icon);
    if (snprintf(path, sizeof(path), "%s/%s", icons, icon + strlen("/var/cache/linux/")) < 0) return;
    FILE *f = fopen(path, "rb");
    uint8_t bmp[54 + 1024 + 576];
    size_t got = f ? fread(bmp, 1, sizeof(bmp), f) : 0;
    if (f) fclose(f);
    CHECK(got == sizeof(bmp), "the icon is 24x24 8-bit (%zu bytes)", got);
    if (got == sizeof(bmp)) {
        // Rows run bottom up; row 12, the left half red (RGB332 0xE0), the
        // right half transparent (0).
        const uint8_t *row = bmp + 54 + 1024 + 12 * 24;
        CHECK(row[2] == 0xE0 && row[11] == 0xE0 && row[12] == 0x00 && row[22] == 0x00,
              "left red, right transparent: %02x %02x %02x %02x", row[2], row[11], row[12], row[22]);
    }
#endif

    // English names on asking in English.
    list(LINUX_APPS_LIST, "en");
    CHECK(linux_apps_wait_scan(5000), "the read in English ends");
    list(LINUX_APPS_LIST, "en");
    l = line_of("fake.desktop");
    CHECK(l != NULL && strncmp(l, "Fake\t", 5) == 0, "Name in English: %s", l ? l : "(none)");
    list(LINUX_APPS_LIST, "ja");
    linux_apps_wait_scan(5000);

    // Starting one.
    CHECK(linux_apps_launch("no-such.desktop") == -1, "an unknown id is refused");
    CHECK(linux_apps_launch("fake.desktop") == 0, "fake.desktop asked for");
    linux_apps_pump();
    pid_t pid = linux_apps_last_pid();
    CHECK(pid > 0, "started (pid %d)", (int)pid);
    snprintf(path, sizeof(path), "%s/out", g_root);
    char *out = NULL;
    for (int i = 0; i < 200 && (out = read_file(path)) == NULL; i++) nap(10);
    for (int i = 0; i < 50 && out != NULL && strchr(out, '\n') == NULL; i++) {
        nap(10);
        out = read_file(path);
    }
    CHECK(out != NULL && strcmp(out, "family-mruby|/tmp|first\n") == 0,
          "XDG_CURRENT_DESKTOP, Path= and the argument (no %%F): %s", out ? out : "(nothing)");
    CHECK(getsid(pid) == pid, "a session of its own");
    nap(100);
    CHECK(session_count(pid) == 2, "the program (its shell became sleep 31) and the background child: %d", session_count(pid));
    CHECK(linux_apps_running() == 2, "running %d", linux_apps_running());

    char name[64];
    CHECK(linux_apps_window_name(NULL, NULL, pid, name, sizeof(name)) &&
              strcmp(name, "\xe5\x81\xbd\xe7\x89\xa9") == 0,
          "a window of the session gets the entry's name");
    CHECK(linux_apps_window_name("fakeclass", NULL, 0, name, sizeof(name)), "by StartupWMClass, any case");
    CHECK(linux_apps_window_name("nope", "sub-nested", 0, name, sizeof(name)) && strcmp(name, "Nested") == 0,
          "by the desktop file id (the second name)");
    CHECK(!linux_apps_window_name("nope", NULL, getpid(), name, sizeof(name)), "nothing for an unknown window");
    char small[5];
    CHECK(linux_apps_window_name(NULL, NULL, pid, small, sizeof(small)) && strcmp(small, "\xe5\x81\xbd") == 0,
          "a short name is cut at a character: %s", small);

    // The end.
    int ended = linux_apps_end_all(500);
    CHECK(ended == 2, "the end signalled %d processes", ended);
    CHECK(session_count(pid) == 0, "nothing of the session left: %d", session_count(pid));
    CHECK(linux_apps_running() == 0, "nothing running");
    CHECK(linux_apps_end_all(500) == 0, "a second end has nothing to do");
}

int main(void)
{
    snprintf(g_root, sizeof(g_root), "/tmp/fmrb-apps-test-%d", (int)getpid());
    if (mkdir(g_root, 0700) != 0) {
        printf("cannot make %s\n", g_root);
        return 2;
    }
    setenv("HOME", g_root, 1);

    test_split();
    test_tree();

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    if (system(cmd) != 0) printf("cannot remove %s\n", g_root);
    if (g_fail) {
        printf("native apps: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("native apps: %d checks passed (Exec, the entries and their rules, Name[ja], %s, starting, "
           "window names, the end)\n", g_count, NATIVE_HAVE_PNG ? "icons" : "no libpng: icons not checked");
    return 0;
}
