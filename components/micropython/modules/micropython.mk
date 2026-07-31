# Pulls the fmruby C module into the embed port generation, so its qstrs and
# its MP_REGISTER_MODULE entry land in mp_embed/genhdr/. Only
# "rake micropython:gen" reads this file.
#
# Just fmrb_module.c is listed: the qstr extractor preprocesses every source it
# is given with the host compiler, which has no ESP-IDF headers, so the module
# file deliberately depends on nothing beyond py/, fmrb_hid_msg.h (stdint only)
# and the bridge header. Everything that needs the firmware headers lives in
# fmrb_bridge.c, which the component compiles but the generator never sees.
#
# The .c files are not copied into mp_embed either way -- they stay here and
# are compiled by the component's CMakeLists like any other source.

FMRB_MOD_DIR := $(USERMOD_DIR)
# modules -> micropython -> components -> repository root
FMRB_CORE_DIR := $(FMRB_MOD_DIR)/../../..

SRC_USERMOD += $(FMRB_MOD_DIR)/fmrb_module.c

CFLAGS_USERMOD += -I$(FMRB_MOD_DIR)
CFLAGS_USERMOD += -I$(FMRB_CORE_DIR)/components/fmrb_msg
