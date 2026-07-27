/**
 * @file basic_charset.hpp
 * @brief Family BASIC character code <-> UTF-8 conversion
 *
 * The interpreter works in Family BASIC character codes (1 byte per
 * character, core_spec sec 12 table B) so ASC / CHR$ / SCR$ / string
 * comparison follow the code table by construction. UTF-8 only exists at the
 * edges: source text coming in, console text going out. Both directions live
 * here so the firmware adapter and the host test runner share one table.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace fmrb_basic {

/// Character code used for source characters with no Family BASIC equivalent.
inline constexpr uint8_t charset_substitute = 0x3F;  // '?'

/**
 * @brief Decode one UTF-8 sequence.
 * @param src   input bytes
 * @param len   bytes available at @p src
 * @param used  receives the number of bytes consumed (>= 1)
 * @return Unicode code point, or 0xFFFD for a malformed sequence.
 */
uint32_t utf8_decode(const char* src, size_t len, size_t* used) noexcept;

/// Convert a Unicode code point to a Family BASIC code, substitute if unmapped.
uint8_t unicode_to_fbcode(uint32_t ucs) noexcept;

/**
 * @brief Encode one Family BASIC code as UTF-8.
 * @param out buffer of at least 4 bytes
 * @return number of bytes written (1 - 3)
 *
 * Codes with no Unicode equivalent (BG pattern characters) become U+FFFD.
 */
size_t fbcode_to_utf8(uint8_t code, char* out) noexcept;

}  // namespace fmrb_basic
