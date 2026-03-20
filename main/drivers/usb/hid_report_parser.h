#ifndef HID_REPORT_PARSER_H
#define HID_REPORT_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Layout information for a single field in an HID report
 */
typedef struct {
    uint16_t bit_offset;    // Bit position within report (after Report ID byte)
    uint8_t  bit_size;      // Number of bits for this field
    int32_t  logical_min;   // Logical minimum (for scaling absolute coords)
    int32_t  logical_max;   // Logical maximum (for scaling absolute coords)
    bool     is_relative;   // true=relative, false=absolute (from INPUT item bit2)
    bool     found;         // true if this field was found in descriptor
} hid_field_info_t;

/**
 * @brief Complete mouse report layout parsed from HID Report Descriptor
 */
typedef struct {
    bool valid;              // true if descriptor was successfully parsed
    uint8_t report_id;       // Report ID (0 = no report ID)
    bool has_report_id;      // true if report uses Report ID prefix byte
    hid_field_info_t buttons;
    hid_field_info_t x;
    hid_field_info_t y;
    uint16_t report_byte_len; // Total report size in bytes (excluding Report ID)
} hid_mouse_report_layout_t;

/**
 * @brief Parse HID Report Descriptor and extract mouse report layout
 *
 * @param desc         Raw report descriptor bytes
 * @param desc_len     Length of descriptor in bytes
 * @param out_layout   Output: parsed mouse report layout
 * @return true if a mouse report was found and parsed successfully
 */
bool hid_report_parse_mouse(const uint8_t *desc, size_t desc_len,
                            hid_mouse_report_layout_t *out_layout);

/**
 * @brief Extract a field value from raw report data using layout info
 *
 * @param data      Report data bytes (after Report ID if present)
 * @param data_len  Length of report data in bytes
 * @param field     Field layout information
 * @return Extracted value with sign extension if logical_min < 0
 */
int32_t hid_report_extract_field(const uint8_t *data, size_t data_len,
                                 const hid_field_info_t *field);

#endif // HID_REPORT_PARSER_H
