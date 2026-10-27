// Host test of the native file namespace's Linux places (doc/linux_native
// L7): the POSIX file HAL (components/fmrb_hal/platform/posix/) itself,
// compiled against test/native/stub's stand-ins for the RTOS, the pools and
// the log, over a made-up tree under /tmp:
//
//   <t>/home      plays the user's home  -> /mnt/host (read-write)
//   <t>/fs        plays Linux's "/"      -> /mnt/root (read-only)
//   <t>/outside   somewhere neither of them is: nothing may be written there
//   <t>/sys, user, cache   the distribution, user and cache roots
//
// Checked: reading and writing under /mnt/host; every change under
// /mnt/root refused with FMRB_ERR_READ_ONLY; writes through symbolic links
// that leave the home refused (and nothing lands outside); ".." cannot climb
// out; devices, FIFOs and sockets do not open (and do not block); the
// listing of "/" and "/mnt"; a place the layout leaves out.
//
// The real home is never touched: every path here is under the made-up tree.
//
// Usage: native_file_test

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "fmrb_hal_file.h"
#include "fmrb_hal_file_layout.h"

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

static char g_t[64];

static void tpath(char *out, size_t n, const char *rel)
{
    snprintf(out, n, "%s/%s", g_t, rel);
}

static void put(const char *rel, const char *text)
{
    char p[PATH_MAX];
    tpath(p, sizeof(p), rel);
    FILE *f = fopen(p, "w");
    if (f == NULL) {
        printf("cannot write %s\n", p);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static void dir(const char *rel)
{
    char p[PATH_MAX];
    tpath(p, sizeof(p), rel);
    if (mkdir(p, 0755) != 0 && errno != EEXIST) {
        printf("cannot mkdir %s\n", p);
        exit(2);
    }
}

static void link_to(const char *target, const char *rel)
{
    char p[PATH_MAX];
    tpath(p, sizeof(p), rel);
    if (symlink(target, p) != 0) {
        printf("cannot link %s\n", p);
        exit(2);
    }
}

static bool exists(const char *rel)
{
    char p[PATH_MAX];
    struct stat st;
    tpath(p, sizeof(p), rel);
    return lstat(p, &st) == 0;
}

static bool host_text_is(const char *rel, const char *text)
{
    char p[PATH_MAX];
    char buf[256] = { 0 };
    tpath(p, sizeof(p), rel);
    FILE *f = fopen(p, "r");
    if (f == NULL) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

// Write a whole file through the HAL: the open's result.
static fmrb_err_t hal_write(const char *vpath, const char *text)
{
    fmrb_file_t h = NULL;
    fmrb_err_t err = fmrb_hal_file_open(vpath, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &h);
    if (err != FMRB_OK) return err;
    size_t w = 0;
    err = fmrb_hal_file_write(h, text, strlen(text), &w);
    fmrb_hal_file_close(h);
    return err;
}

// Read a whole (small) file through the HAL; "" on failure.
static const char *hal_read(const char *vpath)
{
    static char buf[256];
    buf[0] = '\0';
    fmrb_file_t h = NULL;
    if (fmrb_hal_file_open(vpath, FMRB_O_RDONLY, &h) != FMRB_OK) return buf;
    size_t n = 0;
    fmrb_hal_file_read(h, buf, sizeof(buf) - 1, &n);
    fmrb_hal_file_close(h);
    buf[n] = '\0';
    return buf;
}

static bool lists(const char *vpath, const char *name)
{
    size_t n = 0;
    const char *const *names = fmrb_hal_file_virtual_children(vpath, &n);
    for (size_t i = 0; names != NULL && i < n; i++) {
        if (strcmp(names[i], name) == 0) return true;
    }
    return false;
}

static void set_layout(bool host, bool root)
{
    char sys[PATH_MAX], user[PATH_MAX], cache[PATH_MAX], home[PATH_MAX], fs[PATH_MAX];
    tpath(sys, sizeof(sys), "sys");
    tpath(user, sizeof(user), "user");
    tpath(cache, sizeof(cache), "cache");
    tpath(home, sizeof(home), "home");
    tpath(fs, sizeof(fs), "fs");
    fmrb_hal_file_layout_t layout = {
        .system_root = sys,
        .user_root = user,
        .cache_root = cache,
        .etc_seed = NULL,
        .host_home = host ? home : NULL,
        .host_root = root ? fs : "",
    };
    if (fmrb_hal_file_set_layout(&layout) != FMRB_OK) {
        printf("set_layout failed\n");
        exit(2);
    }
}

static void make_tree(void)
{
    snprintf(g_t, sizeof(g_t), "/tmp/fmrb-file-test-XXXXXX");
    if (mkdtemp(g_t) == NULL) {
        printf("mkdtemp failed\n");
        exit(2);
    }
    dir("sys");
    dir("sys/etc");
    put("sys/etc/system_conf.toml", "x = 1\n");
    dir("user");
    dir("cache");
    dir("home");
    dir("home/sub");
    dir("fs");
    dir("fs/etc");
    put("fs/etc/passwd", "root:x:0:0\n");
    dir("outside");
    put("outside/secret.txt", "secret\n");

    char p[PATH_MAX];
    char target[PATH_MAX];
    // Links in the home: out of it (a directory, a file, nothing) and in it.
    tpath(target, sizeof(target), "outside");
    link_to(target, "home/link_out");
    tpath(target, sizeof(target), "outside/secret.txt");
    link_to(target, "home/link_file");
    tpath(target, sizeof(target), "outside/not_there.txt");
    link_to(target, "home/dangling");
    tpath(target, sizeof(target), "home/sub");
    link_to(target, "home/link_in");
    // Things that are not files: a FIFO and a socket in "/", a device
    // reached by a link (the real /dev/null: no mknod without root).
    tpath(p, sizeof(p), "fs/fifo");
    if (mkfifo(p, 0644) != 0) {
        printf("mkfifo failed\n");
        exit(2);
    }
    tpath(p, sizeof(p), "home/fifo");
    if (mkfifo(p, 0644) != 0) {
        printf("mkfifo failed\n");
        exit(2);
    }
    link_to("/dev/null", "fs/null");
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/fs/sock", g_t);
    if (s < 0 || bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("cannot make a socket\n");
        exit(2);
    }
    close(s);
}

static void test_host_rw(void)
{
    char host[PATH_MAX];
    CHECK(fmrb_hal_file_resolve_path("/mnt/host/a.txt", host, sizeof(host)) == FMRB_OK &&
          strcmp(host + strlen(g_t), "/home/a.txt") == 0, "/mnt/host/a.txt -> <home>/a.txt (%s)", host);
    CHECK(fmrb_hal_file_resolve_path("/mnt/root/etc/passwd", host, sizeof(host)) == FMRB_OK &&
          strcmp(host + strlen(g_t), "/fs/etc/passwd") == 0, "/mnt/root/etc/passwd -> <fs>/etc/passwd (%s)", host);
    CHECK(!fmrb_hal_file_path_is_read_only("/mnt/host/a.txt"), "/mnt/host is writable");
    CHECK(fmrb_hal_file_path_is_read_only("/mnt/root/etc/passwd"), "/mnt/root is read-only");

    CHECK(hal_write("/mnt/host/a.txt", "hello\n") == FMRB_OK && host_text_is("home/a.txt", "hello\n"),
          "write /mnt/host/a.txt lands in the home");
    CHECK(strcmp(hal_read("/mnt/host/a.txt"), "hello\n") == 0, "read it back");
    fmrb_file_info_t info;
    CHECK(fmrb_hal_file_stat("/mnt/host/a.txt", &info) == FMRB_OK && info.size == 6 && !info.is_dir, "stat");
    CHECK(fmrb_hal_file_mkdir("/mnt/host/d1/d2") == FMRB_OK && exists("home/d1/d2"), "mkdir under the home");
    CHECK(hal_write("/mnt/host/new/deep/f.txt", "x") == FMRB_OK && host_text_is("home/new/deep/f.txt", "x"),
          "create with new parents");
    CHECK(fmrb_hal_file_rename("/mnt/host/a.txt", "/mnt/host/d1/b.txt") == FMRB_OK && exists("home/d1/b.txt") &&
          !exists("home/a.txt"), "rename inside the home");
    CHECK(fmrb_hal_file_utime("/mnt/host/d1/b.txt", 1000000) == FMRB_OK, "utime inside the home");
    CHECK(fmrb_hal_file_remove("/mnt/host/d1/b.txt") == FMRB_OK && !exists("home/d1/b.txt"), "remove");
    CHECK(fmrb_hal_file_rmdir("/mnt/host/d1/d2") == FMRB_OK && !exists("home/d1/d2"), "rmdir");
    CHECK(strcmp(hal_read("/mnt/root/etc/passwd"), "root:x:0:0\n") == 0, "read /mnt/root/etc/passwd");
    CHECK(hal_write("/mnt/host/link_in/ok.txt", "in") == FMRB_OK && host_text_is("home/sub/ok.txt", "in"),
          "a link that stays in the home may be written through");
    CHECK(fmrb_hal_file_remove("/mnt/host") == FMRB_ERR_READ_ONLY && exists("home"), "the home itself is not removed");
    CHECK(fmrb_hal_file_rename("/mnt/host", "/mnt/host2") == FMRB_ERR_READ_ONLY, "nor renamed");
}

static void test_root_read_only(void)
{
    CHECK(hal_write("/mnt/root/etc/passwd", "evil") == FMRB_ERR_READ_ONLY &&
          host_text_is("fs/etc/passwd", "root:x:0:0\n"), "write /mnt/root/etc/passwd refused");
    CHECK(hal_write("/mnt/root/etc/new", "evil") == FMRB_ERR_READ_ONLY && !exists("fs/etc/new"),
          "create under /mnt/root refused");
    fmrb_file_t h = NULL;
    CHECK(fmrb_hal_file_open("/mnt/root/etc/passwd", FMRB_O_RDWR, &h) == FMRB_ERR_READ_ONLY, "read-write open refused");
    CHECK(fmrb_hal_file_open("/mnt/root/etc/passwd", FMRB_O_WRONLY | FMRB_O_APPEND, &h) == FMRB_ERR_READ_ONLY,
          "append refused");
    CHECK(fmrb_hal_file_remove("/mnt/root/etc/passwd") == FMRB_ERR_READ_ONLY && exists("fs/etc/passwd"), "remove refused");
    CHECK(fmrb_hal_file_rename("/mnt/root/etc/passwd", "/mnt/root/etc/p2") == FMRB_ERR_READ_ONLY, "rename in refused");
    CHECK(fmrb_hal_file_rename("/mnt/root/etc/passwd", "/mnt/host/p") == FMRB_ERR_READ_ONLY &&
          exists("fs/etc/passwd") && !exists("home/p"), "rename out to the home refused");
    CHECK(fmrb_hal_file_rename("/mnt/host/new/deep/f.txt", "/mnt/root/etc/f") == FMRB_ERR_READ_ONLY &&
          !exists("fs/etc/f"), "rename into /mnt/root refused");
    CHECK(fmrb_hal_file_mkdir("/mnt/root/etc/d") == FMRB_ERR_READ_ONLY && !exists("fs/etc/d"), "mkdir refused");
    CHECK(fmrb_hal_file_rmdir("/mnt/root/etc") == FMRB_ERR_READ_ONLY && exists("fs/etc"), "rmdir refused");
    CHECK(fmrb_hal_file_utime("/mnt/root/etc/passwd", 1) == FMRB_ERR_READ_ONLY, "utime refused");
    CHECK(fmrb_hal_file_chmod("/mnt/root/etc/passwd", 0777) == FMRB_ERR_READ_ONLY, "chmod refused");
}

static void test_links_out(void)
{
    CHECK(hal_write("/mnt/host/link_out/new.txt", "x") == FMRB_ERR_READ_ONLY && !exists("outside/new.txt"),
          "create through a directory link out of the home refused");
    CHECK(hal_write("/mnt/host/link_file", "overwritten") == FMRB_ERR_READ_ONLY &&
          host_text_is("outside/secret.txt", "secret\n"), "write through a file link out refused");
    fmrb_file_t h = NULL;
    CHECK(fmrb_hal_file_open("/mnt/host/link_file", FMRB_O_WRONLY | FMRB_O_APPEND, &h) == FMRB_ERR_READ_ONLY &&
          host_text_is("outside/secret.txt", "secret\n"), "append through it refused");
    CHECK(hal_write("/mnt/host/dangling", "x") == FMRB_ERR_READ_ONLY && !exists("outside/not_there.txt"),
          "write through a link to nothing refused");
    CHECK(fmrb_hal_file_mkdir("/mnt/host/link_out/d") == FMRB_ERR_READ_ONLY && !exists("outside/d"),
          "mkdir through the link refused");
    CHECK(fmrb_hal_file_remove("/mnt/host/link_out/secret.txt") == FMRB_ERR_READ_ONLY && exists("outside/secret.txt"),
          "remove through the link refused");
    CHECK(fmrb_hal_file_rename("/mnt/host/link_out/secret.txt", "/mnt/host/stolen.txt") == FMRB_ERR_READ_ONLY &&
          exists("outside/secret.txt"), "rename out from behind the link refused");
    CHECK(fmrb_hal_file_rename("/mnt/host/link_in/ok.txt", "/mnt/host/link_out/ok.txt") == FMRB_ERR_READ_ONLY &&
          !exists("outside/ok.txt"), "rename to behind the link refused");
    CHECK(fmrb_hal_file_utime("/mnt/host/link_file", 1) == FMRB_ERR_READ_ONLY, "utime through the link refused");
    CHECK(fmrb_hal_file_chmod("/mnt/host/link_file", 0777) == FMRB_ERR_READ_ONLY, "chmod through the link refused");
    CHECK(fmrb_hal_file_path_is_read_only("/mnt/host/link_out/x"), "is_read_only says so too");
    // Reading may follow a link anywhere ("/" is readable anyway).
    CHECK(strcmp(hal_read("/mnt/host/link_file"), "secret\n") == 0, "reading through the link is allowed");
    // The link itself is the home's entry: removing it leaves its target.
    CHECK(fmrb_hal_file_remove("/mnt/host/link_file") == FMRB_OK && !exists("home/link_file") &&
          exists("outside/secret.txt"), "removing the link removes only the link");
}

static void test_dotdot(void)
{
    char host[PATH_MAX];
    char outside[PATH_MAX];
    tpath(outside, sizeof(outside), "outside");
    CHECK(fmrb_hal_file_resolve_path("/mnt/host/../../outside/secret.txt", host, sizeof(host)) == FMRB_OK &&
          strncmp(host, outside, strlen(outside)) != 0, ".. does not reach outside (%s)", host);
    CHECK(fmrb_hal_file_path_is_read_only("/mnt/host/../../outside/x"), "and lands in the read-only distribution");
    CHECK(hal_write("/mnt/host/../../../../../../outside/x.txt", "x") == FMRB_ERR_READ_ONLY && !exists("outside/x.txt"),
          "many .. still refused");
    CHECK(hal_write("/mnt/host/sub/../../root/etc/x", "x") == FMRB_ERR_READ_ONLY && !exists("fs/etc/x"),
          ".. from the home into /mnt/root is read-only");
    CHECK(hal_write("/mnt/host/sub/../dd.txt", "x") == FMRB_OK && host_text_is("home/dd.txt", "x"),
          ".. that stays in the home is fine");
    CHECK(fmrb_hal_file_resolve_path("/mnt/root/../../..", host, sizeof(host)) == FMRB_OK &&
          strncmp(host, outside, strlen(outside)) != 0, "/mnt/root/../../.. is the namespace root (%s)", host);
}

static void test_special(void)
{
    char host[PATH_MAX];
    fmrb_file_t h = NULL;
    CHECK(fmrb_hal_file_resolve_path("/mnt/root/fifo", host, sizeof(host)) == FMRB_ERR_NOT_SUPPORTED,
          "a FIFO does not resolve");
    CHECK(fmrb_hal_file_open("/mnt/root/fifo", FMRB_O_RDONLY, &h) != FMRB_OK, "a FIFO does not open (nor block)");
    CHECK(fmrb_hal_file_open("/mnt/root/sock", FMRB_O_RDONLY, &h) != FMRB_OK, "a socket does not open");
    CHECK(fmrb_hal_file_open("/mnt/root/null", FMRB_O_RDONLY, &h) != FMRB_OK, "a device (/dev/null) does not open");
    CHECK(fmrb_hal_file_open("/mnt/host/fifo", FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &h) != FMRB_OK,
          "a FIFO in the home does not open for writing");
    CHECK(fmrb_hal_file_open("/mnt/root/etc", FMRB_O_RDONLY, &h) != FMRB_OK, "a directory does not open as a file");
    fmrb_file_info_t info;
    CHECK(fmrb_hal_file_stat("/mnt/root/fifo", &info) != FMRB_OK, "nor stats");
    CHECK(fmrb_hal_file_stat("/mnt/root/etc", &info) == FMRB_OK && info.is_dir, "a directory stats");
    fmrb_dir_t d = NULL;
    bool seen_passwd = false;
    if (fmrb_hal_file_opendir("/mnt/root/etc", &d) == FMRB_OK) {
        fmrb_file_info_t e;
        while (fmrb_hal_file_readdir(d, &e) == FMRB_OK) {
            if (strcmp(e.name, "passwd") == 0) seen_passwd = true;
        }
        fmrb_hal_file_closedir(d);
    }
    CHECK(seen_passwd, "/mnt/root/etc lists passwd");
}

static void test_listing(void)
{
    CHECK(lists("/", "mnt") && lists("/", "home"), "/ lists mnt");
    CHECK(lists("/mnt", "host") && lists("/mnt", "root"), "/mnt lists host and root");
    set_layout(true, false);
    char host[PATH_MAX];
    CHECK(lists("/mnt", "host") && !lists("/mnt", "root"), "root left out: /mnt lists host only");
    CHECK(fmrb_hal_file_resolve_path("/mnt/root/etc/passwd", host, sizeof(host)) == FMRB_ERR_NOT_FOUND,
          "and /mnt/root does not resolve");
    CHECK(fmrb_hal_file_path_is_read_only("/mnt/root/x"), "nor is written");
    set_layout(false, false);
    CHECK(!lists("/", "mnt") && fmrb_hal_file_virtual_children("/mnt", NULL) == NULL, "both left out: no mnt");
    CHECK(hal_write("/mnt/host/x.txt", "x") != FMRB_OK && !exists("home/x.txt"), "and /mnt/host is not written");
    set_layout(true, true);
}

int main(void)
{
    make_tree();
    set_layout(true, true);
    if (fmrb_hal_file_init() != FMRB_OK) {
        printf("fmrb_hal_file_init failed\n");
        return 2;
    }
    test_host_rw();
    test_root_read_only();
    test_links_out();
    test_dotdot();
    test_special();
    test_listing();

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_t);
    if (system(cmd) != 0) printf("cannot remove %s\n", g_t);
    if (g_fail) {
        printf("native files: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("native files: %d checks passed (/mnt/host, /mnt/root, links, .., special files, listing)\n", g_count);
    return 0;
}
