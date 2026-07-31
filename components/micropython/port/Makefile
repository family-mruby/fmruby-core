# Drives the MicroPython "embed" port to produce a self-contained set of C
# sources under components/micropython/mp_embed/. That tree is committed, so
# the day-to-day firmware build (rake build:linux / build:esp32) never needs
# make or python3 -- only "rake micropython:gen" does.
#
# Run through "rake micropython:gen" rather than directly.

# Location of the MicroPython submodule, relative to this directory.
MICROPYTHON_TOP = ../micropython

# Where the generated package is written (relative to this directory).
PACKAGE_DIR = ../mp_embed

# Pull in the MicroPython C modules that live in this component, so their
# qstrs and MP_REGISTER_MODULE entries end up in the generated headers.
# py.mk scans $(USER_C_MODULES)/*/micropython.mk, hence the component root.
USER_C_MODULES = $(abspath ..)

include $(MICROPYTHON_TOP)/ports/embed/embed.mk
