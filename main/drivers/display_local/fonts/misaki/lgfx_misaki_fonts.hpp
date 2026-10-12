#pragma once

// Adds misaki_8 (8x8 Japanese bitmap font) to lgfx::v1::fonts / lgfx::fonts.
// LovyanGFX itself is left untouched; the alias in lgfx::fonts mirrors the
// pattern used by lgfx_fonts.hpp so user code can write
// `target->setFont(&lgfx::fonts::misaki_8)`.

// In fmruby-core this file lives in main/ (not inside the LovyanGFX
// tree), so include lgfx_fonts.hpp via the m5gfx component include path.
#include <lgfx/v1/lgfx_fonts.hpp>

namespace lgfx
{
  inline namespace v1
  {
    namespace fonts
    {
      extern const U8g2font misaki_8;
    }
  }
}
