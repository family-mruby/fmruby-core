// Host-test stand-in for ESP-IDF's generated sdkconfig.h (test/native/clip.c
// through fmrb_attr.h). The tests build with CONFIG_IDF_TARGET_LINUX, where
// the placement attributes expand to nothing; nothing else is read here.
#pragma once
