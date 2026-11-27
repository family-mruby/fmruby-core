// Host test of hw_proxy's file ops (main/drivers/hw_proxy/hw_proxy_file.c):
// the op table and what each file system change does, with the file HAL's
// calls stood in for here (no RTOS, no flash).
//
// Checked: every file op is classified as one (hw_proxy_op_is_file, the
// dispatch in hw_proxy.c) and nothing else is; remove, rename, mkdir, rmdir,
// utime and chmod do their change on resolved host paths, under the file HAL
// lock taken once and given back; a failing change answers FMRB_ERR_FAILED;
// sync, format and erase are handed to the HAL with what the caller gave.
//
// Usage: native_hw_proxy_test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hw_proxy_internal.h"

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

// ----- The file HAL, stood in for -----

static int g_locks, g_unlocks, g_depth, g_max_depth;
static const void *g_last_arg;
static int g_last_call;  // which stand-in was called last

bool fmrb_hal_file_lock(uint32_t timeout_ms) {
    (void)timeout_ms;
    g_locks++;
    if (++g_depth > g_max_depth) g_max_depth = g_depth;
    return true;
}
void fmrb_hal_file_unlock(void) { g_unlocks++; g_depth--; }

enum { CALL_NONE, CALL_SYNC, CALL_MKFS, CALL_ERASE };

fmrb_err_t fmrb_hal_file_sync(fmrb_file_t h) { g_last_call = CALL_SYNC; g_last_arg = h; return FMRB_OK; }
fmrb_err_t fmrb_hal_file_mkfs(const char *p) { g_last_call = CALL_MKFS; g_last_arg = p; return FMRB_OK; }
fmrb_err_t fmrb_hal_file_erase(const char *v) { g_last_call = CALL_ERASE; g_last_arg = v; return FMRB_ERR_NOT_SUPPORTED; }

// The rest hw_proxy_file.c calls; not exercised here.
fmrb_err_t fmrb_hal_file_open(const char *p, uint32_t f, fmrb_file_t *o) { (void)p; (void)f; (void)o; return FMRB_ERR_FAILED; }
fmrb_err_t fmrb_hal_file_close(fmrb_file_t h) { (void)h; return FMRB_ERR_FAILED; }
fmrb_err_t fmrb_hal_file_read(fmrb_file_t h, void *b, size_t s, size_t *r) { (void)h; (void)b; (void)s; (void)r; return FMRB_ERR_FAILED; }
fmrb_err_t fmrb_hal_file_write(fmrb_file_t h, const void *b, size_t s, size_t *w) { (void)h; (void)b; (void)s; (void)w; return FMRB_ERR_FAILED; }
fmrb_err_t fmrb_hal_file_seek(fmrb_file_t h, int32_t o, fmrb_seek_mode_t m) { (void)h; (void)o; (void)m; return FMRB_ERR_FAILED; }
fmrb_err_t fmrb_hal_file_tell(fmrb_file_t h, uint32_t *p) { (void)h; (void)p; return FMRB_ERR_FAILED; }
fmrb_err_t fmrb_hal_file_size(fmrb_file_t h, uint32_t *s) { (void)h; (void)s; return FMRB_ERR_FAILED; }
fmrb_err_t fmrb_hal_file_stat(const char *p, fmrb_file_info_t *i) { (void)p; (void)i; return FMRB_ERR_FAILED; }

// ----- Helpers -----

static char g_t[64];

static void tpath(char *out, size_t n, const char *rel) { snprintf(out, n, "%s/%s", g_t, rel); }

static bool exists(const char *rel) {
    char p[256];
    struct stat st;
    tpath(p, sizeof(p), rel);
    return stat(p, &st) == 0;
}

static void touch(const char *rel) {
    char p[256];
    tpath(p, sizeof(p), rel);
    FILE *f = fopen(p, "w");
    if (f) { fputs("x", f); fclose(f); }
}

static fmrb_err_t run(hw_proxy_op_t op, void *params) {
    hw_proxy_request_t req = { .op = op, .params = params, .result = FMRB_ERR_INVALID_STATE };
    g_locks = g_unlocks = g_depth = g_max_depth = 0;
    g_last_call = CALL_NONE;
    g_last_arg = NULL;
    hw_proxy_file_execute(&req);
    return req.result;
}

// One lock around the VFS call, given back.
static bool locked_once(void) { return g_locks == 1 && g_unlocks == 1 && g_max_depth == 1; }

// ----- Tests -----

static void test_classification(void) {
    static const hw_proxy_op_t file_ops[] = {
        HW_PROXY_OP_FILE_OPEN, HW_PROXY_OP_FILE_CLOSE, HW_PROXY_OP_FILE_READ, HW_PROXY_OP_FILE_WRITE,
        HW_PROXY_OP_FILE_SEEK, HW_PROXY_OP_FILE_TELL, HW_PROXY_OP_FILE_SIZE, HW_PROXY_OP_FILE_STAT,
        HW_PROXY_OP_DIR_OPEN, HW_PROXY_OP_DIR_READ, HW_PROXY_OP_DIR_CLOSE, HW_PROXY_OP_DIR_STAT,
        HW_PROXY_OP_FILE_REMOVE, HW_PROXY_OP_FILE_RENAME, HW_PROXY_OP_DIR_MAKE, HW_PROXY_OP_DIR_REMOVE,
        HW_PROXY_OP_FILE_UTIME, HW_PROXY_OP_FILE_CHMOD, HW_PROXY_OP_FILE_SYNC, HW_PROXY_OP_FS_FORMAT,
        HW_PROXY_OP_FS_ERASE,
    };
    static const hw_proxy_op_t other_ops[] = {
        HW_PROXY_OP_I2C_INIT, HW_PROXY_OP_I2C_READ, HW_PROXY_OP_I2C_WRITE, HW_PROXY_OP_I2C_RELEASE,
        HW_PROXY_OP_GPIO_CLAIM, HW_PROXY_OP_GPIO_CONFIG, HW_PROXY_OP_GPIO_READ, HW_PROXY_OP_GPIO_WRITE,
        HW_PROXY_OP_GPIO_PULL, HW_PROXY_OP_RMT_INIT, HW_PROXY_OP_RMT_WRITE,
    };
    for (size_t i = 0; i < sizeof(file_ops) / sizeof(file_ops[0]); i++) {
        CHECK(hw_proxy_op_is_file(file_ops[i]), "op 0x%02x is a file op", (unsigned)file_ops[i]);
    }
    for (size_t i = 0; i < sizeof(other_ops) / sizeof(other_ops[0]); i++) {
        CHECK(!hw_proxy_op_is_file(other_ops[i]), "op 0x%02x is not a file op", (unsigned)other_ops[i]);
    }
    // Each new file op has a case of its own: an unknown one answers
    // FMRB_ERR_INVALID_PARAM, so none of them may.
    hw_proxy_path_params_t pp = { .path = "/nonexistent/hw_proxy_test" };
    CHECK(run((hw_proxy_op_t)0x4F, &pp) == FMRB_ERR_INVALID_PARAM, "an unknown file op is refused");
}

static void test_changes(void) {
    char a[256], b[256], d[256];
    tpath(a, sizeof(a), "a.txt");
    tpath(b, sizeof(b), "b.txt");
    tpath(d, sizeof(d), "dir");

    touch("a.txt");
    hw_proxy_file_rename_params_t rp = { .old_path = a, .new_path = b };
    CHECK(run(HW_PROXY_OP_FILE_RENAME, &rp) == FMRB_OK && !exists("a.txt") && exists("b.txt"), "rename");
    CHECK(locked_once(), "rename under the lock");
    CHECK(run(HW_PROXY_OP_FILE_RENAME, &rp) == FMRB_ERR_FAILED && locked_once(), "rename of nothing fails, lock given back");

    hw_proxy_path_value_params_t vp = { .path = b, .value = 1000000 };
    struct stat st;
    CHECK(run(HW_PROXY_OP_FILE_UTIME, &vp) == FMRB_OK && stat(b, &st) == 0 && st.st_mtime == 1000000, "utime");
    CHECK(locked_once(), "utime under the lock");
    vp.value = 0600;
    CHECK(run(HW_PROXY_OP_FILE_CHMOD, &vp) == FMRB_OK && stat(b, &st) == 0 && (st.st_mode & 0777) == 0600, "chmod");
    CHECK(locked_once(), "chmod under the lock");

    hw_proxy_path_params_t pp = { .path = b };
    CHECK(run(HW_PROXY_OP_FILE_REMOVE, &pp) == FMRB_OK && !exists("b.txt"), "remove");
    CHECK(locked_once(), "remove under the lock");
    CHECK(run(HW_PROXY_OP_FILE_REMOVE, &pp) == FMRB_ERR_FAILED && locked_once(), "remove of nothing fails");

    pp.path = d;
    CHECK(run(HW_PROXY_OP_DIR_MAKE, &pp) == FMRB_OK && exists("dir"), "mkdir");
    CHECK(locked_once(), "mkdir under the lock");
    CHECK(run(HW_PROXY_OP_DIR_MAKE, &pp) == FMRB_ERR_FAILED, "mkdir of one there fails");
    CHECK(run(HW_PROXY_OP_DIR_REMOVE, &pp) == FMRB_OK && !exists("dir"), "rmdir");
    CHECK(locked_once(), "rmdir under the lock");
    CHECK(run(HW_PROXY_OP_DIR_REMOVE, &pp) == FMRB_ERR_FAILED, "rmdir of nothing fails");
}

static void test_handed_to_hal(void) {
    int dummy;
    hw_proxy_file_sync_params_t sp = { .handle = &dummy };
    CHECK(run(HW_PROXY_OP_FILE_SYNC, &sp) == FMRB_OK && g_last_call == CALL_SYNC && g_last_arg == &dummy,
          "sync goes to fmrb_hal_file_sync with the handle");
    hw_proxy_path_params_t pp = { .path = "/flash" };
    CHECK(run(HW_PROXY_OP_FS_FORMAT, &pp) == FMRB_OK && g_last_call == CALL_MKFS && g_last_arg == pp.path,
          "format goes to fmrb_hal_file_mkfs with the path");
    pp.path = "storage";
    CHECK(run(HW_PROXY_OP_FS_ERASE, &pp) == FMRB_ERR_NOT_SUPPORTED && g_last_call == CALL_ERASE &&
          g_last_arg == pp.path, "erase goes to fmrb_hal_file_erase, its answer kept");
}

int main(void) {
    snprintf(g_t, sizeof(g_t), "/tmp/fmrb_hw_proxy_test_XXXXXX");
    if (mkdtemp(g_t) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    test_classification();
    test_changes();
    test_handed_to_hal();
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_t);
    if (system(cmd) != 0) printf("warning: %s left behind\n", g_t);
    printf("hw_proxy file ops: %d checks, %d failed\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
