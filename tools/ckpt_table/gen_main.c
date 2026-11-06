// The checkpoint table maker (doc/checkpoint/flash_table.md, Ff).
//
// Opens one VM the way the firmware opens a movable app's (the same libmruby,
// the same gem ports, fmrb_app_checkpoint_prepare_vm), takes its baseline for
// a table and writes it as C source (mrb_checkpoint_write_table): the names,
// the snapshots of the permanent classes and the mrblib irep hashes the
// firmware links in as constant data, so that each VM keeps only the
// addresses. Linked by tools/ckpt_table/ckpt_table.rb against whatever the
// ports call and this program lacks as stand-ins that return 0: opening a VM
// only defines classes, so nothing of the platform runs here.
//
//   ckpt_table_gen OUT.c
//   ckpt_table_gen --measure     (linked with the table: what a VM's baseline
//                                 costs with it, as the firmware logs it)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mruby.h>
#include <mruby/checkpoint.h>
#include "fmrb_err.h"
#include "fmrb_hal_file.h"

mrb_state *mrb_open_with_custom_alloc(void *mem, size_t bytes);
void fmrb_app_checkpoint_prepare_vm(mrb_state *mrb);

// The maker's VM sees no files. A gem that reads one while it loads (a
// setting, an icon) must not take what the stand-ins (which return 0, which
// is FMRB_OK) leave in its output arguments: these say "not found" instead,
// and the firmware's VM keeps whatever it read itself (a difference from the
// table, kept by that VM).
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle)
{
    (void)path; (void)flags; (void)out_handle;
    return FMRB_ERR_NOT_FOUND;
}
fmrb_err_t fmrb_hal_file_stat(const char *path, fmrb_file_info_t *info)
{
    (void)path; (void)info;
    return FMRB_ERR_NOT_FOUND;
}
fmrb_err_t fmrb_hal_file_opendir(const char *path, fmrb_dir_t *out_handle)
{
    (void)path; (void)out_handle;
    return FMRB_ERR_NOT_FOUND;
}
fmrb_err_t fmrb_hal_file_read(fmrb_file_t handle, void *buffer, size_t size, size_t *bytes_read)
{
    (void)handle; (void)buffer; (void)size;
    if (bytes_read) *bytes_read = 0;
    return FMRB_ERR_FAILED;
}
fmrb_err_t fmrb_hal_file_size(fmrb_file_t handle, uint32_t *size)
{
    (void)handle;
    if (size) *size = 0;
    return FMRB_ERR_FAILED;
}
fmrb_err_t fmrb_hal_file_tell(fmrb_file_t handle, uint32_t *position)
{
    (void)handle;
    if (position) *position = 0;
    return FMRB_ERR_FAILED;
}
fmrb_err_t fmrb_hal_file_readdir(fmrb_dir_t handle, fmrb_file_info_t *info)
{
    (void)handle; (void)info;
    return FMRB_ERR_NOT_FOUND;
}
fmrb_err_t fmrb_hal_file_getcwd(char *buffer, size_t size)
{
    if (buffer && size) buffer[0] = 0;
    return FMRB_ERR_FAILED;
}

// The per-task pool accessors of the estalloc build (lib/patch alloc.c): one
// VM, one pool.
static void *current_est;
void *fmrb_get_current_est(void) { return current_est; }
void fmrb_set_current_est(void *est) { current_est = est; }

static void write_text(const char *text, size_t len, void *ud)
{
    fwrite(text, 1, len, (FILE *)ud);
}

// --measure: the program linked once more with the table it wrote (the
// firmware's weak reference, app/fmrb_app_checkpoint.c), so the baseline
// is taken the way a movable app's VM takes it, and its cost in the pool is
// printed as the firmware logs it. The host's pointers are 64-bit: the
// device's parts are smaller (doc/checkpoint/report/f6_s3.md).
extern const mrb_checkpoint_table fmrb_checkpoint_table __attribute__((weak));
int mrb_get_estalloc_stats(void *est, size_t *total, size_t *used, size_t *free_out, int32_t *frag);
int mrb_get_estalloc_usage(void *est, size_t *peak, size_t *largest, size_t *last_fail, size_t *fails);

static size_t est_used(void)
{
    size_t total = 0, used = 0, free_bytes = 0;
    int32_t frag = 0;
    mrb_get_estalloc_stats(current_est, &total, &used, &free_bytes, &frag);
    return used;
}

static size_t est_peak(void)
{
    size_t peak = 0, largest = 0, last_fail = 0, fails = 0;
    mrb_get_estalloc_usage(current_est, &peak, &largest, &last_fail, &fails);
    return peak;
}

static void print_part(const char *part, size_t count, size_t bytes, void *ud)
{
    (void)ud;
    printf("  %-20s %6zu entries %8zu bytes\n", part, count, bytes);
}

static void count_name(const char *name, int is_func, void *ud)
{
    (void)name; (void)is_func;
    (*(unsigned *)ud)++;
}

static int measure(mrb_state *mrb)
{
    if (&fmrb_checkpoint_table == NULL) {
        fprintf(stderr, "ckpt_table_gen: --measure needs the table linked in\n");
        return 1;
    }
    size_t before = est_used();
    fmrb_app_checkpoint_prepare_vm(mrb);
    mrb_checkpoint_set_table(mrb, &fmrb_checkpoint_table);
    size_t peak_before = est_peak();
    size_t at_baseline = est_used();
    mrb_checkpoint_baseline(mrb);
    size_t after = est_used(), peak = est_peak();
    unsigned names = 0;
    mrb_checkpoint_each_name(mrb, count_name, &names);
    const char *why = mrb_checkpoint_table_status(mrb);
    printf("the baseline (%u names) took %zu bytes of the pool (%zu more while it was made); "
           "of them the gem's setting up %zu; table: %s\n",
           names, after - before, peak > peak_before ? peak - after : 0, at_baseline - before,
           why ? why : "used");
    size_t total = mrb_checkpoint_baseline_size(mrb, print_part, NULL);
    printf("  %-20s %6s         %8zu bytes\n", "(parts)", "", total);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: ckpt_table_gen OUT.c | --measure\n");
        return 2;
    }
    // Larger than any app pool (the baseline for a table keeps the names
    // too), under estalloc's limit (24-bit addresses: 16 MB).
    size_t size = 8u << 20;
    void *pool = malloc(size);
    mrb_state *mrb = pool ? mrb_open_with_custom_alloc(pool, size) : NULL;
    if (!mrb) {
        fprintf(stderr, "ckpt_table_gen: cannot open a VM\n");
        return 1;
    }
    // A gem whose initialization raised leaves the gems after it out: the
    // table would lack their names (every VM would keep them itself)
    if (mrb->exc) {
        mrb_print_error(mrb);
        fprintf(stderr, "ckpt_table_gen: opening the VM raised\n");
        return 1;
    }
    if (strcmp(argv[1], "--measure") == 0) return measure(mrb);
    fmrb_app_checkpoint_prepare_vm(mrb);
    mrb_checkpoint_prepare_table(mrb);
    mrb_checkpoint_baseline(mrb);
    FILE *out = fopen(argv[1], "w");
    if (!out) {
        fprintf(stderr, "ckpt_table_gen: cannot write %s\n", argv[1]);
        return 1;
    }
    char why[200];
    int r = mrb_checkpoint_write_table(mrb, "fmrb_checkpoint_table", write_text, out, why, sizeof(why));
    fclose(out);
    if (r != 0) {
        fprintf(stderr, "ckpt_table_gen: %s\n", why);
        return 1;
    }
    return 0;
}
