// Host-test stand-in for fmrb_log.h (test/native/file_hal.c): the log is
// dropped; the test checks what each call returns.
#pragma once

#include <stdio.h>

#define FMRB_LOGE(tag, ...) ((void)(tag))
#define FMRB_LOGW(tag, ...) ((void)(tag))
#define FMRB_LOGI(tag, ...) ((void)(tag))
#define FMRB_LOGD(tag, ...) ((void)(tag))
