#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "hid_report_parser.h"
#include "fmrb_log.h"

static const char *TAG = "hid_parser";

// HID item types (bType field, bits 3:2)
#define HID_ITEM_TYPE_MAIN      0
#define HID_ITEM_TYPE_GLOBAL    1
#define HID_ITEM_TYPE_LOCAL     2

// HID Main item tags (bTag field, bits 7:4)
#define HID_MAIN_INPUT          0x08
#define HID_MAIN_OUTPUT         0x09
#define HID_MAIN_COLLECTION     0x0A
#define HID_MAIN_FEATURE        0x0B
#define HID_MAIN_END_COLLECTION 0x0C

// HID Global item tags
#define HID_GLOBAL_USAGE_PAGE   0x00
#define HID_GLOBAL_LOGICAL_MIN  0x01
#define HID_GLOBAL_LOGICAL_MAX  0x02
#define HID_GLOBAL_REPORT_SIZE  0x07
#define HID_GLOBAL_REPORT_ID    0x08
#define HID_GLOBAL_REPORT_COUNT 0x09

// HID Local item tags
#define HID_LOCAL_USAGE         0x00
#define HID_LOCAL_USAGE_MIN     0x01
#define HID_LOCAL_USAGE_MAX     0x02

// Usage Pages
#define USAGE_PAGE_GENERIC_DESKTOP 0x01
#define USAGE_PAGE_BUTTON          0x09

// Generic Desktop Usages
#define USAGE_POINTER   0x01
#define USAGE_MOUSE     0x02
#define USAGE_X         0x30
#define USAGE_Y         0x31
#define USAGE_WHEEL     0x38

// Collection types
#define COLLECTION_APPLICATION  0x01

// INPUT item flags
#define INPUT_FLAG_CONSTANT     0x01  // bit 0: 1=Constant, 0=Data
#define INPUT_FLAG_RELATIVE     0x04  // bit 2: 1=Relative, 0=Absolute

// Max pending usages to track
#define MAX_PENDING_USAGES 16

// Parser state
typedef struct {
    uint16_t usage_page;
    uint32_t pending_usages[MAX_PENDING_USAGES];
    int pending_usage_count;
    uint32_t usage_min;
    uint32_t usage_max;
    int32_t logical_min;
    int32_t logical_max;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
    bool has_report_id;
    uint16_t bit_offset;
    int collection_depth;
    int mouse_collection_depth;  // depth at which Mouse/Pointer collection was found, -1 if not in mouse
} hid_parser_state_t;

// Extract signed integer from HID item data
static int32_t extract_signed(const uint8_t *data, int size)
{
    switch (size) {
        case 1: return (int8_t)data[0];
        case 2: return (int16_t)(data[0] | (data[1] << 8));
        case 4: return (int32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
        default: return 0;
    }
}

// Extract unsigned integer from HID item data
static uint32_t extract_unsigned(const uint8_t *data, int size)
{
    switch (size) {
        case 1: return data[0];
        case 2: return data[0] | (data[1] << 8);
        case 4: return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        default: return 0;
    }
}

static void handle_input_item(hid_parser_state_t *state, uint32_t flags,
                               hid_mouse_report_layout_t *layout)
{
    bool is_constant = (flags & INPUT_FLAG_CONSTANT) != 0;
    bool is_relative = (flags & INPUT_FLAG_RELATIVE) != 0;

    FMRB_LOGI(TAG, "  INPUT: flags=0x%02"PRIx32" usage_page=0x%02X count=%"PRIu32" size=%"PRIu32
              " bit_off=%u %s %s",
              flags, state->usage_page, state->report_count, state->report_size,
              state->bit_offset,
              is_constant ? "Const" : "Data",
              is_relative ? "Rel" : "Abs");

    if (!is_constant && state->mouse_collection_depth >= 0) {
        // Process each pending usage
        for (int i = 0; i < (int)state->report_count; i++) {
            uint32_t usage = 0;
            if (i < state->pending_usage_count) {
                usage = state->pending_usages[i];
            } else if (state->usage_min > 0 && state->usage_max > 0) {
                // USAGE_MINIMUM/USAGE_MAXIMUM range
                usage = state->usage_min + i;
            }

            uint16_t field_bit_offset = state->bit_offset + (uint16_t)(i * state->report_size);

            if (state->usage_page == USAGE_PAGE_GENERIC_DESKTOP) {
                if (usage == USAGE_X && !layout->x.found) {
                    layout->x.bit_offset = field_bit_offset;
                    layout->x.bit_size = (uint8_t)state->report_size;
                    layout->x.logical_min = state->logical_min;
                    layout->x.logical_max = state->logical_max;
                    layout->x.is_relative = is_relative;
                    layout->x.found = true;
                    FMRB_LOGI(TAG, "    -> X: off=%u sz=%u min=%"PRId32" max=%"PRId32" %s",
                              field_bit_offset, (uint8_t)state->report_size,
                              state->logical_min, state->logical_max,
                              is_relative ? "rel" : "abs");
                } else if (usage == USAGE_WHEEL && !layout->wheel.found) {
                    layout->wheel.bit_offset = field_bit_offset;
                    layout->wheel.bit_size = (uint8_t)state->report_size;
                    layout->wheel.logical_min = state->logical_min;
                    layout->wheel.logical_max = state->logical_max;
                    layout->wheel.is_relative = is_relative;
                    layout->wheel.found = true;
                    FMRB_LOGI(TAG, "    -> Wheel: off=%u sz=%u min=%"PRId32" max=%"PRId32" %s",
                              field_bit_offset, (uint8_t)state->report_size,
                              state->logical_min, state->logical_max,
                              is_relative ? "rel" : "abs");
                } else if (usage == USAGE_Y && !layout->y.found) {
                    layout->y.bit_offset = field_bit_offset;
                    layout->y.bit_size = (uint8_t)state->report_size;
                    layout->y.logical_min = state->logical_min;
                    layout->y.logical_max = state->logical_max;
                    layout->y.is_relative = is_relative;
                    layout->y.found = true;
                    FMRB_LOGI(TAG, "    -> Y: off=%u sz=%u min=%"PRId32" max=%"PRId32" %s",
                              field_bit_offset, (uint8_t)state->report_size,
                              state->logical_min, state->logical_max,
                              is_relative ? "rel" : "abs");
                }
            } else if (state->usage_page == USAGE_PAGE_BUTTON && !layout->buttons.found) {
                // Record buttons as a single block (all button bits together)
                layout->buttons.bit_offset = state->bit_offset;
                layout->buttons.bit_size = (uint8_t)(state->report_size * state->report_count);
                layout->buttons.logical_min = state->logical_min;
                layout->buttons.logical_max = state->logical_max;
                layout->buttons.is_relative = false;
                layout->buttons.found = true;
                FMRB_LOGI(TAG, "    -> Buttons: off=%u sz=%u",
                          state->bit_offset, layout->buttons.bit_size);
                break;  // Buttons are handled as a group, not per-usage
            }
        }
    }

    // Always advance bit_offset
    state->bit_offset += (uint16_t)(state->report_size * state->report_count);

    // Clear local state after INPUT item
    state->pending_usage_count = 0;
    state->usage_min = 0;
    state->usage_max = 0;
}

bool hid_report_parse_mouse(const uint8_t *desc, size_t desc_len,
                            hid_mouse_report_layout_t *out_layout)
{
    if (desc == NULL || desc_len == 0 || out_layout == NULL) {
        return false;
    }

    memset(out_layout, 0, sizeof(*out_layout));

    // Dump first 32 bytes of descriptor
    {
        char hex[128];
        int pos = 0;
        int dump_len = (desc_len > 32) ? 32 : (int)desc_len;
        for (int i = 0; i < dump_len && pos < (int)sizeof(hex) - 3; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", desc[i]);
        }
        FMRB_LOGI(TAG, "Report Descriptor (%d bytes): %s%s",
                  (int)desc_len, hex, desc_len > 32 ? "..." : "");
    }

    hid_parser_state_t state;
    memset(&state, 0, sizeof(state));
    state.mouse_collection_depth = -1;

    size_t offset = 0;
    while (offset < desc_len) {
        uint8_t prefix = desc[offset];

        // Long item check (prefix == 0xFE)
        if (prefix == 0xFE) {
            if (offset + 2 >= desc_len) break;
            uint8_t data_size = desc[offset + 1];
            offset += 3 + data_size;
            continue;
        }

        // Short item parsing
        int bSize = prefix & 0x03;
        int bType = (prefix >> 2) & 0x03;
        int bTag = (prefix >> 4) & 0x0F;

        // bSize: 0=0, 1=1, 2=2, 3=4 bytes
        int data_size = (bSize == 3) ? 4 : bSize;

        if (offset + 1 + data_size > desc_len) break;

        const uint8_t *item_data = &desc[offset + 1];

        if (bType == HID_ITEM_TYPE_GLOBAL) {
            switch (bTag) {
                case HID_GLOBAL_USAGE_PAGE:
                    state.usage_page = (uint16_t)extract_unsigned(item_data, data_size);
                    break;
                case HID_GLOBAL_LOGICAL_MIN:
                    state.logical_min = extract_signed(item_data, data_size);
                    break;
                case HID_GLOBAL_LOGICAL_MAX:
                    state.logical_max = extract_signed(item_data, data_size);
                    break;
                case HID_GLOBAL_REPORT_SIZE:
                    state.report_size = extract_unsigned(item_data, data_size);
                    break;
                case HID_GLOBAL_REPORT_COUNT:
                    state.report_count = extract_unsigned(item_data, data_size);
                    break;
                case HID_GLOBAL_REPORT_ID:
                    state.report_id = (uint8_t)extract_unsigned(item_data, data_size);
                    state.has_report_id = true;
                    state.bit_offset = 0;  // Reset bit offset for new report ID
                    FMRB_LOGI(TAG, "  REPORT_ID: %d", state.report_id);
                    break;
            }
        } else if (bType == HID_ITEM_TYPE_LOCAL) {
            switch (bTag) {
                case HID_LOCAL_USAGE:
                    if (state.pending_usage_count < MAX_PENDING_USAGES) {
                        state.pending_usages[state.pending_usage_count++] =
                            extract_unsigned(item_data, data_size);
                    }
                    break;
                case HID_LOCAL_USAGE_MIN:
                    state.usage_min = extract_unsigned(item_data, data_size);
                    break;
                case HID_LOCAL_USAGE_MAX:
                    state.usage_max = extract_unsigned(item_data, data_size);
                    break;
            }
        } else if (bType == HID_ITEM_TYPE_MAIN) {
            switch (bTag) {
                case HID_MAIN_COLLECTION: {
                    uint8_t collection_type = (data_size > 0) ? item_data[0] : 0;
                    if (collection_type == COLLECTION_APPLICATION &&
                        state.usage_page == USAGE_PAGE_GENERIC_DESKTOP) {
                        // Check if pending usage is Mouse or Pointer
                        for (int i = 0; i < state.pending_usage_count; i++) {
                            if (state.pending_usages[i] == USAGE_MOUSE ||
                                state.pending_usages[i] == USAGE_POINTER) {
                                state.mouse_collection_depth = state.collection_depth;
                                FMRB_LOGI(TAG, "  Mouse/Pointer collection found at depth %d",
                                          state.collection_depth);
                                break;
                            }
                        }
                    }
                    state.collection_depth++;
                    state.pending_usage_count = 0;
                    state.usage_min = 0;
                    state.usage_max = 0;
                    break;
                }
                case HID_MAIN_END_COLLECTION:
                    state.collection_depth--;
                    if (state.collection_depth <= state.mouse_collection_depth) {
                        state.mouse_collection_depth = -1;
                    }
                    break;
                case HID_MAIN_INPUT: {
                    uint32_t flags = (data_size > 0) ? extract_unsigned(item_data, data_size) : 0;
                    handle_input_item(&state, flags, out_layout);
                    break;
                }
                case HID_MAIN_OUTPUT:
                case HID_MAIN_FEATURE:
                    // Clear local state
                    state.pending_usage_count = 0;
                    state.usage_min = 0;
                    state.usage_max = 0;
                    break;
            }
        }

        offset += 1 + data_size;
    }

    // Check if we found enough fields for a mouse
    if (out_layout->x.found && out_layout->y.found) {
        out_layout->valid = true;
        out_layout->report_id = state.report_id;
        out_layout->has_report_id = state.has_report_id;
        out_layout->report_byte_len = (state.bit_offset + 7) / 8;

        FMRB_LOGI(TAG, "Parse result: valid=true report_id=%d has_report_id=%d byte_len=%u",
                  out_layout->report_id, out_layout->has_report_id, out_layout->report_byte_len);
        FMRB_LOGI(TAG, "  X: off=%u sz=%u min=%"PRId32" max=%"PRId32" %s",
                  out_layout->x.bit_offset, out_layout->x.bit_size,
                  out_layout->x.logical_min, out_layout->x.logical_max,
                  out_layout->x.is_relative ? "rel" : "abs");
        FMRB_LOGI(TAG, "  Y: off=%u sz=%u min=%"PRId32" max=%"PRId32" %s",
                  out_layout->y.bit_offset, out_layout->y.bit_size,
                  out_layout->y.logical_min, out_layout->y.logical_max,
                  out_layout->y.is_relative ? "rel" : "abs");
        if (out_layout->buttons.found) {
            FMRB_LOGI(TAG, "  Buttons: off=%u sz=%u",
                      out_layout->buttons.bit_offset, out_layout->buttons.bit_size);
        }
        return true;
    }

    FMRB_LOGI(TAG, "Parse result: valid=false (X=%s Y=%s Buttons=%s)",
              out_layout->x.found ? "found" : "missing",
              out_layout->y.found ? "found" : "missing",
              out_layout->buttons.found ? "found" : "missing");
    return false;
}

int32_t hid_report_extract_field(const uint8_t *data, size_t data_len,
                                 const hid_field_info_t *field)
{
    int32_t value = 0;
    int bit_off = field->bit_offset;
    int bit_size = field->bit_size;

    for (int i = 0; i < bit_size; i++) {
        int byte_idx = (bit_off + i) / 8;
        int bit_idx = (bit_off + i) % 8;
        if (byte_idx < (int)data_len) {
            if (data[byte_idx] & (1 << bit_idx)) {
                value |= (1 << i);
            }
        }
    }

    // Sign-extend if logical_min is negative (signed field)
    if (field->logical_min < 0 && bit_size < 32 && (value & (1 << (bit_size - 1)))) {
        value |= ~((1 << bit_size) - 1);
    }

    return value;
}
