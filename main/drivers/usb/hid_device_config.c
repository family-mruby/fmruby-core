#include <string.h>
#include "hid_device_config.h"
#include "fmrb_toml.h"
#include "fmrb_log.h"

static const char *TAG = "hid_config";

typedef struct {
    uint16_t vid;
    uint16_t pid;
    char name[32];
    bool skip_control_transfer;
    hid_mouse_report_layout_t layout;
    uint8_t copy_len;  // report_len + 1 if has_report_id
} hid_mouse_config_entry_t;

static hid_mouse_config_entry_t g_mouse_entries[HID_DEVICE_CONFIG_MAX_ENTRIES];
static int g_mouse_entry_count = 0;

static void parse_field(const toml_table_t *tab, const char *key, hid_field_info_t *field)
{
    const toml_table_t *ftab = toml_table_in(tab, key);
    if (!ftab) {
        field->found = false;
        return;
    }

    field->bit_offset = (uint16_t)fmrb_toml_get_int(ftab, "offset", 0);
    field->bit_size = (uint8_t)fmrb_toml_get_int(ftab, "size", 8);
    field->logical_min = (int32_t)fmrb_toml_get_int(ftab, "min", -127);
    field->logical_max = (int32_t)fmrb_toml_get_int(ftab, "max", 127);
    field->is_relative = fmrb_toml_get_bool(ftab, "relative", true);
    field->found = true;
}

void hid_device_config_init(void)
{
    g_mouse_entry_count = 0;

    char errbuf[128];
    toml_table_t *root = fmrb_toml_load_file(HID_DEVICE_CONFIG_PATH, errbuf, sizeof(errbuf));
    if (!root) {
        FMRB_LOGI(TAG, "No HID device config file (%s), using defaults", HID_DEVICE_CONFIG_PATH);
        return;
    }

    toml_array_t *mouse_arr = toml_array_in(root, "mouse");
    if (!mouse_arr) {
        FMRB_LOGI(TAG, "No [[mouse]] entries in config");
        toml_free(root);
        return;
    }

    int n = toml_array_nelem(mouse_arr);
    FMRB_LOGI(TAG, "Loading %d mouse device config(s) from %s", n, HID_DEVICE_CONFIG_PATH);

    for (int i = 0; i < n && g_mouse_entry_count < HID_DEVICE_CONFIG_MAX_ENTRIES; i++) {
        const toml_table_t *entry = toml_table_at(mouse_arr, i);
        if (!entry) continue;

        hid_mouse_config_entry_t *e = &g_mouse_entries[g_mouse_entry_count];
        memset(e, 0, sizeof(*e));

        e->vid = (uint16_t)fmrb_toml_get_int(entry, "vid", 0);
        e->pid = (uint16_t)fmrb_toml_get_int(entry, "pid", 0);
        if (e->vid == 0 && e->pid == 0) {
            FMRB_LOGW(TAG, "Skipping entry %d: no vid/pid", i);
            continue;
        }

        const char *name = fmrb_toml_get_string(entry, "name", "");
        strncpy(e->name, name, sizeof(e->name) - 1);
        // String is freed when toml_free(root) is called below

        e->skip_control_transfer = fmrb_toml_get_bool(entry, "skip_control_transfer", false);

        int64_t report_id = fmrb_toml_get_int(entry, "report_id", -1);
        int64_t report_len = fmrb_toml_get_int(entry, "report_len", 3);

        e->layout.valid = true;
        if (report_id >= 0) {
            e->layout.has_report_id = true;
            e->layout.report_id = (uint8_t)report_id;
        } else {
            e->layout.has_report_id = false;
            e->layout.report_id = 0;
        }
        e->layout.report_byte_len = (uint16_t)report_len;

        parse_field(entry, "buttons", &e->layout.buttons);
        parse_field(entry, "x", &e->layout.x);
        parse_field(entry, "y", &e->layout.y);

        // Calculate copy length: report data + report ID byte if present
        e->copy_len = (uint8_t)(report_len + (e->layout.has_report_id ? 1 : 0));
        if (e->copy_len > 64) e->copy_len = 64;

        FMRB_LOGI(TAG, "  [%d] VID=0x%04X PID=0x%04X \"%s\" report_id=%s(%d) len=%d %s",
                  g_mouse_entry_count, e->vid, e->pid, e->name,
                  e->layout.has_report_id ? "yes" : "no",
                  e->layout.report_id,
                  (int)report_len,
                  e->layout.x.is_relative ? "rel" : "abs");

        g_mouse_entry_count++;
    }

    toml_free(root);
    FMRB_LOGI(TAG, "Loaded %d mouse device config(s)", g_mouse_entry_count);
}

bool hid_device_config_find_mouse(uint16_t vid, uint16_t pid,
                                   hid_mouse_report_layout_t *layout_out,
                                   uint8_t *copy_len_out)
{
    for (int i = 0; i < g_mouse_entry_count; i++) {
        if (g_mouse_entries[i].vid == vid && g_mouse_entries[i].pid == pid) {
            *layout_out = g_mouse_entries[i].layout;
            *copy_len_out = g_mouse_entries[i].copy_len;
            FMRB_LOGI(TAG, "Config match: VID=0x%04X PID=0x%04X \"%s\"",
                      vid, pid, g_mouse_entries[i].name);
            return true;
        }
    }
    return false;
}

bool hid_device_config_skip_control_transfer(uint16_t vid, uint16_t pid)
{
    for (int i = 0; i < g_mouse_entry_count; i++) {
        if (g_mouse_entries[i].vid == vid && g_mouse_entries[i].pid == pid) {
            return g_mouse_entries[i].skip_control_transfer;
        }
    }
    return false;
}
