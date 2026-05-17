#include "fmrb_debug.h"

// Mirrors fmrb_system_config_t.debug_mode default (true) so any logs
// emitted before the kernel finishes parsing system_conf.toml are visible.
static bool s_debug_mode = true;

bool fmrb_debug_mode_enabled(void)
{
    return s_debug_mode;
}

void fmrb_debug_mode_set(bool enabled)
{
    s_debug_mode = enabled;
}
