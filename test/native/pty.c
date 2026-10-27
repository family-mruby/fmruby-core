// Host test of the Linux programs under pseudo terminals (doc/linux_native
// L10): native/sys/linux_pty.c itself, which is plain POSIX.
//
// Checked: output (echo), exit codes, writing to the program's input (cat,
// read), the end of input (Ctrl+D), SIGINT by the call and by Ctrl+C typed
// into the terminal, a large output read to its end without a gap (seq 1
// 100000), the working directory, the environment (TERM, COLUMNS, LINES and
// the terminal's size), the interactive shell (cd, pwd, exit), and that
// closing a program that still runs leaves nothing behind: no zombie, and
// no process of its session (a background job included).
//
// HOME is a directory of its own under /tmp, so the interactive shell reads
// no one's startup files; every command here only reads, or writes under it.
//
// Usage: native_pty_test

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "linux_pty.h"

static int g_fail = 0;
static int g_count = 0;
static char g_home[64];

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

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void nap(int ms)
{
    struct timespec ts = { 0, (long)ms * 1000000L };
    nanosleep(&ts, NULL);
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool exited;
    int code;
} out_t;

static void out_add(out_t *o, const char *p, size_t n)
{
    if (o->len + n + 1 > o->cap) {
        o->cap = (o->len + n + 1) * 2;
        o->buf = realloc(o->buf, o->cap);
    }
    memcpy(o->buf + o->len, p, n);
    o->len += n;
    o->buf[o->len] = '\0';
}

// Read what comes for up to ms; stops once the program ended and its output
// is drained (the way the Shell does: exit first, then what is left).
static void drain(int32_t id, out_t *o, int ms)
{
    char buf[4096];
    long until = now_ms() + ms;
    while (now_ms() < until) {
        int32_t n = linux_pty_read(id, buf, sizeof(buf));
        if (n > 0) {
            out_add(o, buf, (size_t)n);
            continue;
        }
        if (linux_pty_exited(id, &o->code)) {
            while ((n = linux_pty_read(id, buf, sizeof(buf))) > 0) out_add(o, buf, (size_t)n);
            o->exited = true;
            return;
        }
        nap(1);
    }
}

static int32_t start(const char *cmd, const char *cwd)
{
    linux_pty_spec_t spec = { .command = cmd, .cwd = cwd ? cwd : g_home,
                              .cols = 80, .rows = 24, .owner = &g_count };
    int32_t id = 0;
    char why[128] = "";
    fmrb_err_t err = linux_pty_start(&spec, &id, why, sizeof(why));
    CHECK(err == FMRB_OK && id > 0, "start %s: %d %s", cmd ? cmd : "(shell)", err, why);
    return err == FMRB_OK ? id : 0;
}

static out_t run(const char *cmd, int ms)
{
    out_t o = { 0 };
    out_add(&o, "", 0);
    int32_t id = start(cmd, NULL);
    if (id) {
        drain(id, &o, ms);
        linux_pty_close(id);
    }
    return o;
}

// The processes of session sid that are not zombies, and whether pid is
// still there at all (a zombie counts).
static int session_alive(pid_t sid)
{
    int n = 0;
    DIR *d = opendir("/proc");
    struct dirent *de;
    while (d && (de = readdir(d)) != NULL) {
        char path[300], line[512];
        snprintf(path, sizeof(path), "/proc/%s/stat", de->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        size_t len = fread(line, 1, sizeof(line) - 1, f);
        fclose(f);
        line[len] = '\0';
        char *p = strrchr(line, ')');
        char st;
        long ppid, pgrp, sess;
        if (p && sscanf(p + 1, " %c %ld %ld %ld", &st, &ppid, &pgrp, &sess) == 4 &&
            sess == sid && st != 'Z') {
            n++;
        }
    }
    if (d) closedir(d);
    return n;
}

static bool pid_present(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d", (int)pid);
    return access(path, F_OK) == 0;
}

static void test_echo_and_codes(void)
{
    out_t o = run("echo hello", 3000);
    CHECK(o.exited && o.code == 0, "echo: exited %d code %d", o.exited, o.code);
    CHECK(strcmp(o.buf, "hello\r\n") == 0, "echo: got [%s]", o.buf);
    free(o.buf);

    o = run("false", 3000);
    CHECK(o.exited && o.code == 1, "false: code %d", o.code);
    free(o.buf);
    o = run("exit 7", 3000);
    CHECK(o.exited && o.code == 7, "exit 7: code %d", o.code);
    free(o.buf);
    o = run("kill -TERM $$", 3000);
    CHECK(o.exited && o.code == 128 + SIGTERM, "killed: code %d", o.code);
    free(o.buf);
    o = run("no_such_command_l10", 3000);
    CHECK(o.exited && o.code == 127 && strstr(o.buf, "not found") != NULL,
          "missing command: code %d [%s]", o.code, o.buf);
    free(o.buf);
}

static void test_input(void)
{
    // cat: the terminal echoes the line, cat prints it back; Ctrl+D ends it.
    out_t o = { 0 };
    out_add(&o, "", 0);
    int32_t id = start("cat", NULL);
    nap(100);
    CHECK(linux_pty_write(id, "abc\r", 4) == 4, "cat: write");
    drain(id, &o, 300);
    CHECK(!o.exited, "cat ended before Ctrl+D");
    CHECK(linux_pty_write(id, "\x04", 1) == 1, "cat: Ctrl+D");
    drain(id, &o, 3000);
    CHECK(o.exited && o.code == 0, "cat after Ctrl+D: exited %d code %d", o.exited, o.code);
    CHECK(strcmp(o.buf, "abc\r\nabc\r\n") == 0, "cat: got [%s]", o.buf);
    linux_pty_close(id);
    free(o.buf);

    // read into a variable; backspace (DEL) edits the line before it goes.
    memset(&o, 0, sizeof(o));
    out_add(&o, "", 0);
    id = start("read x; echo \"got:$x\"", NULL);
    nap(100);
    linux_pty_write(id, "abx\x7f" "c\r", 6);
    drain(id, &o, 3000);
    CHECK(o.exited && o.code == 0 && strstr(o.buf, "got:abc\r\n") != NULL,
          "read: code %d [%s]", o.code, o.buf);
    linux_pty_close(id);
    free(o.buf);
}

static void test_sigint(void)
{
    // By the call.
    int32_t id = start("sleep 30", NULL);
    nap(100);
    long t0 = now_ms();
    CHECK(linux_pty_signal(id, SIGINT) == FMRB_OK, "signal");
    out_t o = { 0 };
    out_add(&o, "", 0);
    drain(id, &o, 3000);
    CHECK(o.exited && o.code == 128 + SIGINT && now_ms() - t0 < 1000,
          "sleep after SIGINT: exited %d code %d in %ld ms", o.exited, o.code, now_ms() - t0);
    linux_pty_close(id);
    free(o.buf);

    // By Ctrl+C typed into the terminal (the line discipline's ISIG).
    id = start("sleep 30", NULL);
    nap(100);
    linux_pty_write(id, "\x03", 1);
    memset(&o, 0, sizeof(o));
    out_add(&o, "", 0);
    drain(id, &o, 3000);
    CHECK(o.exited && o.code == 128 + SIGINT, "sleep after ^C: exited %d code %d", o.exited, o.code);
    linux_pty_close(id);
    free(o.buf);
}

static void test_large(void)
{
    long t0 = now_ms();
    out_t o = run("seq 1 100000", 20000);
    long ms = now_ms() - t0;
    CHECK(o.exited && o.code == 0, "seq: exited %d code %d", o.exited, o.code);
    // Every number once, in order, each followed by \r\n.
    long expect = 1;
    char *p = o.buf;
    bool ok = true;
    while (*p) {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p || v != expect || end[0] != '\r' || end[1] != '\n') {
            ok = false;
            break;
        }
        expect++;
        p = end + 2;
    }
    CHECK(ok && expect == 100001, "seq: stopped at %ld of 100000 (%zu bytes)", expect - 1, o.len);
    printf("  seq 1 100000: %zu bytes in %ld ms\n", o.len, ms);
    free(o.buf);
}

static void test_cwd_env(void)
{
    char real[PATH_MAX];
    CHECK(realpath(g_home, real) != NULL, "realpath");
    out_t o = run("pwd", 3000);
    char want[PATH_MAX + 4];
    snprintf(want, sizeof(want), "%s\r\n", real);
    CHECK(strcmp(o.buf, want) == 0, "pwd: got [%s] want [%s]", o.buf, real);
    free(o.buf);

    out_t e = { 0 };
    out_add(&e, "", 0);
    int32_t id = start("pwd", "/etc");
    if (id) {
        drain(id, &e, 3000);
        linux_pty_close(id);
    }
    CHECK(strcmp(e.buf, "/etc\r\n") == 0, "pwd in /etc: [%s]", e.buf);
    free(e.buf);

    linux_pty_spec_t bad = { .command = "true", .cwd = "/no/such/dir/l10", .cols = 80, .rows = 24 };
    int32_t bid = 0;
    CHECK(linux_pty_start(&bad, &bid, NULL, 0) == FMRB_ERR_NOT_FOUND, "a missing cwd is refused");

    o = run("echo \"$TERM $COLUMNS $LINES\"; stty size", 3000);
    CHECK(strcmp(o.buf, "dumb 80 24\r\n24 80\r\n") == 0, "env: [%s]", o.buf);
    free(o.buf);
}

static void test_shell(void)
{
    out_t o = { 0 };
    out_add(&o, "", 0);
    int32_t id = start(NULL, NULL);
    nap(300);
    linux_pty_write(id, "cd /tmp; pwd\r", 13);
    drain(id, &o, 500);
    CHECK(!o.exited, "the shell ended by itself");
    linux_pty_write(id, "exit\r", 5);
    drain(id, &o, 3000);
    CHECK(o.exited && o.code == 0, "shell exit: exited %d code %d", o.exited, o.code);
    CHECK(strstr(o.buf, "/tmp\r\n") != NULL, "shell pwd: [%s]", o.buf);
    linux_pty_close(id);
    free(o.buf);
}

static void test_nothing_left(void)
{
    // A command and a background job of its session, both still running.
    int32_t id = start("sleep 100 & sleep 100", NULL);
    nap(200);
    // The child's pid is the session id; find it from the process table.
    pid_t sid = 0;
    {
        DIR *d = opendir("/proc");
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            char path[300], line[512];
            snprintf(path, sizeof(path), "/proc/%s/stat", de->d_name);
            FILE *f = fopen(path, "r");
            if (!f) continue;
            size_t len = fread(line, 1, sizeof(line) - 1, f);
            fclose(f);
            line[len] = '\0';
            char *p = strrchr(line, ')');
            char st;
            long ppid, pgrp, sess;
            if (p && sscanf(p + 1, " %c %ld %ld %ld", &st, &ppid, &pgrp, &sess) == 4 &&
                ppid == getpid() && sess == atol(de->d_name)) {
                sid = (pid_t)sess;
            }
        }
        closedir(d);
    }
    CHECK(sid > 0 && session_alive(sid) >= 2, "two sleeps running (sid %d, %d alive)", (int)sid,
          sid > 0 ? session_alive(sid) : 0);
    long t0 = now_ms();
    linux_pty_close(id);
    CHECK(now_ms() - t0 < 50, "close waited %ld ms", now_ms() - t0);
    CHECK(linux_pty_count() == 0, "a slot is left");
    int left = 1;
    bool present = true;
    for (int i = 0; i < 200 && (left > 0 || present); i++) {
        nap(10);
        left = session_alive(sid);
        present = pid_present(sid);
    }
    CHECK(left == 0, "%d process(es) of the session left", left);
    CHECK(!present, "the child %d is still there (a zombie?)", (int)sid);
    CHECK(waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD, "a child of ours is left");

    // close_owned: every program of one owner.
    int32_t a = start("sleep 100", NULL);
    int32_t b = start("sleep 100", NULL);
    (void)a; (void)b;
    CHECK(linux_pty_count() == 2, "two started");
    CHECK(linux_pty_close_owned(&g_count) == 2 && linux_pty_count() == 0, "close_owned");
    for (int i = 0; i < 100 && waitpid(-1, NULL, WNOHANG) >= 0; i++) nap(10);
    nap(400);
    CHECK(waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD, "children left after close_owned");

    // A stale handle answers "ended" and no more.
    CHECK(linux_pty_read(id, NULL, 0) == -1 && linux_pty_exited(id, NULL) &&
          linux_pty_write(id, "x", 1) == -1, "a closed handle");
}

int main(void)
{
    snprintf(g_home, sizeof(g_home), "/tmp/fmrb-pty-test-%d", (int)getpid());
    if (mkdir(g_home, 0700) != 0) {
        printf("cannot make %s\n", g_home);
        return 2;
    }
    setenv("HOME", g_home, 1);
    setenv("PS1", "$ ", 1);

    test_echo_and_codes();
    test_input();
    test_sigint();
    test_large();
    test_cwd_env();
    test_shell();
    test_nothing_left();

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_home);
    if (system(cmd) != 0) printf("cannot remove %s\n", g_home);
    if (g_fail) {
        printf("native pty: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("native pty: %d checks passed (echo, exit codes, input, Ctrl+D, SIGINT, "
           "seq 1 100000, cwd, env, shell, nothing left)\n", g_count);
    return 0;
}
