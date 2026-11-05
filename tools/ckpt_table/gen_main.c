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

#include <stdio.h>
#include <stdlib.h>
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

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: ckpt_table_gen OUT.c\n");
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
