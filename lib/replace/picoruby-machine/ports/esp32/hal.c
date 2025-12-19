/*
  Original source code from mruby/mrubyc:
    Copyright (C) 2015- Kyushu Institute of Technology.
    Copyright (C) 2015- Shimane IT Open-Innovation Center.
  Modified source code for picoruby/microruby:
    Copyright (C) 2025 HASUMI Hitoshi.

  This file is distributed under BSD 3-Clause License.
*/

#include "hal.h"

//================================================================
/*!@brief
  Set C funcall flag to control tick execution during C->Ruby calls

  NOTE: hal_init() is implemented in machine.c for ESP32.
        This file only implements functions not in machine.c.
*/
void
mrb_set_in_c_funcall(mrb_state *mrb, int flag)
{
  (void)mrb;
  (void)flag;
  // No-op for ESP32 build - tick management is handled differently than POSIX
}
