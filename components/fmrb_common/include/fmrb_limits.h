#pragma once

// System-wide capacity limits.
//
// Split out of fmrb_task_config.h so that fmrb_mem_config.h can size the
// per-app memory pools from the very same numbers without dragging the RTOS
// headers along with them (fmrb_task_config.h includes fmrb_rtos.h, and
// fmrb_mem_config.h is included from mrbgem ports and host tools).

#define FMRB_MAX_SYSTEM_MRUBY_TASKS (4)  // Kernel, Host, System Desktop, System Overlay

// Process ids 0..FMRB_SYSTEM_PROC_COUNT-1 are those fixed system procs; every
// id above them is a user app slot. fmrb_task_config.h gives them names and
// main/app/fmrb_app.c asserts that the enum and this number still agree.
#define FMRB_SYSTEM_PROC_COUNT (4)

// Static ceiling on concurrent apps (system, mruby, lua and basic alike).
// Every per-app array in the firmware is sized by it, so raising it costs
// memory whether or not the slots are ever filled -- which is why the device
// targets stay at 9 and only the web build raises it (wasm/CMakeLists.txt and
// lib/add/family_mruby_wasm.rb have to pass the SAME value, the rule the MRB_*
// layout defines already follow: the mrbgem ports size arrays by it too).
//
// This is the ceiling, not the policy. How many apps a running system will
// actually admit is the `max_apps` key in system_conf.toml, which is clamped
// to this at load (main/kernel/fmrb_kernel.c).
#ifndef FMRB_MAX_APPS
#define FMRB_MAX_APPS (9)
#endif

#define FMRB_MAX_USER_APPS (FMRB_MAX_APPS - FMRB_SYSTEM_PROC_COUNT)

// Maximum number of mruby VMs registered for tick delivery
#define FMRB_MRB_MAX_VMS (FMRB_MAX_SYSTEM_MRUBY_TASKS + FMRB_MAX_APPS)

// Maximum number of tasks tracked by fmrb_task monitor
#define FMRB_TASK_MONITOR_MAX (FMRB_MRB_MAX_VMS + 7) // +7 for infrastructure tasks (RTC, status LED, USB host, USB HID, SPI conn check, BLE FS, Modern audio)
