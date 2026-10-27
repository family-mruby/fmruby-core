/*
 * linux_pty.c - Linux programs under pseudo terminals (linux_pty.h,
 * doc/linux_native L10).
 *
 * Each program is a session of its own: the child calls setsid() and takes
 * the terminal's slave as its controlling terminal, so Ctrl+C reaches the
 * foreground job through the terminal's process group and ending the program
 * can find everything it started by the session id.
 *
 * The slots are touched only by the machine's tasks, and the cooperative
 * port runs one task at a time, so they take no lock. The one other thread
 * here, the ender (end_session), gets its pid by value and touches no slot.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "linux_pty.h"

extern char **environ;

typedef struct {
    bool used;
    bool reaped;       /* waitpid has collected the child */
    bool eof;          /* the terminal reported its end (EIO) */
    int32_t id;
    pid_t pid;         /* the child; also its session and group id */
    int master;
    int code;
    void *owner;
} pty_slot_t;

static pty_slot_t s_slots[LINUX_PTY_MAX];
static uint32_t s_gen;

/* How long a program has to end on SIGHUP before its session is killed. */
#define END_GRACE_MS 300
#define END_STEP_MS 10

/* The environment handed to children: the program's own, with TERM,
 * COLUMNS and LINES of the terminal. Built before fork (the child may only
 * make async-signal-safe calls). */
#define ENV_MAX 512
static char *s_envp[ENV_MAX + 4];
static char s_env_cols[32];
static char s_env_lines[32];
static char s_env_term[] = "TERM=dumb";

static void set_why(char *why, size_t cap, const char *what, int err)
{
    if (why != NULL && cap > 0) {
        snprintf(why, cap, "%s: %s", what, strerror(err));
    }
}

static pty_slot_t *find(int32_t id)
{
    if (id <= 0) return NULL;
    pty_slot_t *s = &s_slots[id % LINUX_PTY_MAX];
    return (s->used && s->id == id) ? s : NULL;
}

static bool env_skip(const char *e)
{
    return strncmp(e, "TERM=", 5) == 0 || strncmp(e, "COLUMNS=", 8) == 0 ||
           strncmp(e, "LINES=", 6) == 0;
}

static void build_env(uint16_t cols, uint16_t rows)
{
    int n = 0;
    for (char **e = environ; e != NULL && *e != NULL && n < ENV_MAX; e++) {
        if (!env_skip(*e)) s_envp[n++] = *e;
    }
    snprintf(s_env_cols, sizeof(s_env_cols), "COLUMNS=%u", (unsigned)cols);
    snprintf(s_env_lines, sizeof(s_env_lines), "LINES=%u", (unsigned)rows);
    s_envp[n++] = s_env_term;
    s_envp[n++] = s_env_cols;
    s_envp[n++] = s_env_lines;
    s_envp[n] = NULL;
}

static int status_code(int st)
{
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return 1;
}

/* The child, between fork and exec: async-signal-safe calls only. */
static void child_exec(int slave, const char *cwd, char *const argv[], pid_t parent)
{
    /* Signals as a fresh program expects them: the machine's threads may
     * block some, and an ignored one would stay ignored across exec. */
    sigset_t all;
    sigemptyset(&all);
    sigprocmask(SIG_SETMASK, &all, NULL);
    for (int sig = 1; sig < NSIG; sig++) {
        signal(sig, SIG_DFL);
    }
    if (setsid() < 0) _exit(126);
    if (ioctl(slave, TIOCSCTTY, 0) < 0) _exit(126);
    /* The machine going away (a crash included) hangs the session up. */
    prctl(PR_SET_PDEATHSIG, SIGHUP);
    if (getppid() != parent) _exit(126);
    if (dup2(slave, 0) < 0 || dup2(slave, 1) < 0 || dup2(slave, 2) < 0) _exit(126);
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3u, ~0u, 0) != 0)
#endif
    {
        for (int fd = 3; fd < 4096; fd++) close(fd);
    }
    if (chdir(cwd) != 0) _exit(126);
    execve(argv[0], argv, s_envp);
    _exit(127);
}

fmrb_err_t linux_pty_start(const linux_pty_spec_t *spec, int32_t *id, char *why, size_t why_cap)
{
    if (spec == NULL || spec->cwd == NULL || id == NULL) return FMRB_ERR_INVALID_PARAM;
    struct stat st;
    if (stat(spec->cwd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        set_why(why, why_cap, spec->cwd, errno ? errno : ENOTDIR);
        return FMRB_ERR_NOT_FOUND;
    }
    int slot = -1;
    for (int i = 0; i < LINUX_PTY_MAX; i++) {
        if (!s_slots[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        set_why(why, why_cap, "programs", EAGAIN);
        return FMRB_ERR_NO_RESOURCE;
    }

    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0) {
        set_why(why, why_cap, "posix_openpt", errno);
        return FMRB_ERR_FAILED;
    }
    char name[64];
    if (grantpt(master) != 0 || unlockpt(master) != 0 ||
        ptsname_r(master, name, sizeof(name)) != 0) {
        set_why(why, why_cap, "ptsname", errno);
        close(master);
        return FMRB_ERR_FAILED;
    }
    int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) {
        set_why(why, why_cap, name, errno);
        close(master);
        return FMRB_ERR_FAILED;
    }
    /* The line discipline's defaults (canonical input, echo, ISIG, \n out
     * as \r\n), with UTF-8 aware erase. A command under sh -c has no job
     * control to bring a stopped job back, so Ctrl+Z does nothing there. */
    struct termios tio;
    if (tcgetattr(slave, &tio) == 0) {
        tio.c_iflag |= IUTF8;
        if (spec->command != NULL) tio.c_cc[VSUSP] = _POSIX_VDISABLE;
        tcsetattr(slave, TCSANOW, &tio);
    }
    struct winsize ws = { .ws_row = spec->rows, .ws_col = spec->cols };
    ioctl(master, TIOCSWINSZ, &ws);
    int fl = fcntl(master, F_GETFL);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);

    char *argv[4];
    char *cmd_dup = (char *)spec->command;  /* execve's argv is not const */
    if (spec->command != NULL) {
        argv[0] = (char *)"/bin/sh";
        argv[1] = (char *)"-c";
        argv[2] = cmd_dup;
        argv[3] = NULL;
    } else if (access("/bin/bash", X_OK) == 0) {
        argv[0] = (char *)"/bin/bash";
        argv[1] = (char *)"-i";
        argv[2] = NULL;
    } else {
        argv[0] = (char *)"/bin/sh";
        argv[1] = (char *)"-i";
        argv[2] = NULL;
    }
    build_env(spec->cols, spec->rows);

    const pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        set_why(why, why_cap, "fork", errno);
        close(slave);
        close(master);
        return FMRB_ERR_FAILED;
    }
    if (pid == 0) {
        child_exec(slave, spec->cwd, argv, parent);
    }
    close(slave);

    pty_slot_t *s = &s_slots[slot];
    memset(s, 0, sizeof(*s));
    s_gen++;
    if (s_gen > 0x0FFFFFFF) s_gen = 1;
    s->used = true;
    s->id = (int32_t)(s_gen * LINUX_PTY_MAX + (uint32_t)slot);
    s->pid = pid;
    s->master = master;
    s->owner = spec->owner;
    *id = s->id;
    return FMRB_OK;
}

static void reap(pty_slot_t *s)
{
    if (s->reaped) return;
    int st = 0;
    pid_t r = waitpid(s->pid, &st, WNOHANG);
    if (r == s->pid) {
        s->reaped = true;
        s->code = status_code(st);
    } else if (r < 0 && errno == ECHILD) {
        s->reaped = true;  /* someone else collected it */
        s->code = 1;
    }
}

int32_t linux_pty_read(int32_t id, void *buf, size_t cap)
{
    pty_slot_t *s = find(id);
    if (s == NULL || s->eof) return -1;
    ssize_t n = read(s->master, buf, cap);
    if (n > 0) return (int32_t)n;
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
    /* EIO: the last holder of the slave closed it. */
    s->eof = true;
    return -1;
}

int32_t linux_pty_write(int32_t id, const void *buf, size_t len)
{
    pty_slot_t *s = find(id);
    if (s == NULL || s->eof) return -1;
    if (len == 0) return 0;
    ssize_t n = write(s->master, buf, len);
    if (n >= 0) return (int32_t)n;
    if (errno == EAGAIN || errno == EINTR) return 0;
    return -1;
}

fmrb_err_t linux_pty_signal(int32_t id, int sig)
{
    pty_slot_t *s = find(id);
    if (s == NULL) return FMRB_ERR_NOT_FOUND;
    reap(s);
    if (s->reaped) return FMRB_ERR_INVALID_STATE;
    pid_t fg = tcgetpgrp(s->master);
    if (fg <= 0) fg = s->pid;
    if (kill(-fg, sig) != 0 && kill(s->pid, sig) != 0) return FMRB_ERR_FAILED;
    return FMRB_OK;
}

bool linux_pty_exited(int32_t id, int *code)
{
    pty_slot_t *s = find(id);
    if (s == NULL) return true;
    reap(s);
    if (s->reaped && code != NULL) *code = s->code;
    return s->reaped;
}

fmrb_err_t linux_pty_resize(int32_t id, uint16_t cols, uint16_t rows)
{
    pty_slot_t *s = find(id);
    if (s == NULL) return FMRB_ERR_NOT_FOUND;
    struct winsize ws = { .ws_row = rows, .ws_col = cols };
    return ioctl(s->master, TIOCSWINSZ, &ws) == 0 ? FMRB_OK : FMRB_ERR_FAILED;
}

/* SIGKILL every process of the session sid (those it started and left in
 * the background included). Returns how many were signalled. */
static int kill_session(pid_t sid)
{
    int n = 0;
    DIR *d = opendir("/proc");
    if (d == NULL) {
        return kill(-sid, SIGKILL) == 0 ? 1 : 0;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        char *end;
        long pid = strtol(de->d_name, &end, 10);
        if (*end != '\0' || pid <= 0) continue;
        char path[64], line[512];
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t len = read(fd, line, sizeof(line) - 1);
        close(fd);
        if (len <= 0) continue;
        line[len] = '\0';
        /* "pid (comm) state ppid pgrp session ...": comm may hold spaces
         * and parentheses, so count from the last ')'. */
        char *p = strrchr(line, ')');
        if (p == NULL) continue;
        char state;
        long ppid, pgrp, session;
        if (sscanf(p + 1, " %c %ld %ld %ld", &state, &ppid, &pgrp, &session) != 4) continue;
        if (session == sid && state != 'Z') {
            if (kill((pid_t)pid, SIGKILL) == 0) n++;
        }
    }
    closedir(d);
    return n;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = (long)ms * 1000000L };
    nanosleep(&ts, NULL);
}

typedef struct {
    pid_t pid;
    bool reaped;
} ender_arg_t;

/* The ender: off the machine's threads, so it may wait. Gives the session
 * END_GRACE_MS to go on SIGHUP, kills what is left, and reaps the child. */
static void *end_session(void *arg)
{
    ender_arg_t a = *(ender_arg_t *)arg;
    free(arg);
    if (!a.reaped) {
        int st;
        for (int t = 0; t < END_GRACE_MS && !a.reaped; t += END_STEP_MS) {
            if (waitpid(a.pid, &st, WNOHANG) == a.pid) a.reaped = true;
            else sleep_ms(END_STEP_MS);
        }
        if (!a.reaped) {
            kill_session(a.pid);
            while (waitpid(a.pid, &st, 0) < 0 && errno == EINTR) {
            }
        }
    }
    /* Whatever the session started and left behind. */
    kill_session(a.pid);
    return NULL;
}

void linux_pty_close(int32_t id)
{
    pty_slot_t *s = find(id);
    if (s == NULL) return;
    reap(s);
    if (!s->reaped) {
        pid_t fg = tcgetpgrp(s->master);
        if (fg > 0 && fg != s->pid) {
            kill(-fg, SIGHUP);
            kill(-fg, SIGCONT);
        }
        kill(-s->pid, SIGHUP);
        kill(-s->pid, SIGCONT);
    }
    close(s->master);
    /* malloc, not the fmrb allocator: the thread is not the machine's, and
     * the record is the thread's to free. */
    ender_arg_t *a = (ender_arg_t *)malloc(sizeof(*a));
    if (a != NULL) {
        a->pid = s->pid;
        a->reaped = s->reaped;
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, 64 * 1024);
        if (pthread_create(&th, &attr, end_session, a) != 0) {
            free(a);
            a = NULL;
        }
        pthread_attr_destroy(&attr);
    }
    if (a == NULL) {
        /* No thread: end it now, and reap it if it went at once. */
        kill_session(s->pid);
        reap(s);
    }
    memset(s, 0, sizeof(*s));
    s->master = -1;
}

int linux_pty_close_owned(void *owner)
{
    int n = 0;
    for (int i = 0; i < LINUX_PTY_MAX; i++) {
        if (s_slots[i].used && s_slots[i].owner == owner) {
            linux_pty_close(s_slots[i].id);
            n++;
        }
    }
    return n;
}

void linux_pty_close_all(void)
{
    for (int i = 0; i < LINUX_PTY_MAX; i++) {
        pty_slot_t *s = &s_slots[i];
        if (!s->used) continue;
        kill_session(s->pid);
        close(s->master);
        s->used = false;
    }
}

int linux_pty_count(void)
{
    int n = 0;
    for (int i = 0; i < LINUX_PTY_MAX; i++) {
        if (s_slots[i].used) n++;
    }
    return n;
}
