# mruby ABI defines shared by every CMake component that includes mruby
# headers (picoruby-esp32, main).
#
# MUST stay in sync with the rake side. The rake-side single source of truth is
# the build_config files (lib/add/family_mruby_linux.rb / family_mruby_esp32.rb /
# family_mruby_esp32p4.rb): ABI defines live there, BEFORE the gembox loads,
# because a define added from a gem's mrbgem.rake does not reach that gem's own
# sources (its compiler is cloned before the mrbgem.rake body runs) and would
# split the ABI inside libmruby itself. mruby-task's mrbgem.rake additionally
# injects MRB_USE_TASK_SCHEDULER build-wide.
#
# These macros change sizeof(mrb_state) and mrb_value (boxing, method/iv/const
# caches, task state). If the rake-built libmruby and a CMake-built source
# disagree on any of them, struct offsets shift and the VM is corrupted at
# runtime (e.g. the tick top-half writing mrb->task.switching 0x200 short of
# where libmruby has it). A boot-time guard in fmrb_app_init() compares
# sizeof(mrb_state) across the boundary and aborts on mismatch.

macro(fmrb_add_mruby_abi_defines)
  add_compile_definitions(
    MRB_INT64=1
    MRB_NO_BOXING
    MRB_UTF8_STRING
    MRB_USE_TASK_SCHEDULER
    MRB_TICK_UNIT=5
    MRB_TIMESLICE_TICK_COUNT=10
    # Remote debugger VM hook, enabled on every target. Adds two function
    # pointers to mrb_state; must match the rake side (family_mruby_linux.rb /
    # family_mruby_esp32.rb / family_mruby_esp32p4.rb). Keep all four in sync.
    # Cost when no debugger is attached is one NULL test per instruction fetch.
    MRB_USE_DEBUG_HOOK
  )
  if(IDF_TARGET STREQUAL "linux")
    add_compile_definitions(MRB_BASELINE_PROFILE=1)
  else()
    add_compile_definitions(
      MRB_32BIT
      MRB_CONSTRAINED_BASELINE_PROFILE=1
    )
  endif()
endmacro()
