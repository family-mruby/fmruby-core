// The simulation's stand-in for a release build's remote control guard, so
// the Config dialog's "Remote control" row (doc/remote_guard G2) can be seen
// and stepped in the sim. Off unless the sim's /etc/system_conf.toml has
//
//   sim_remote_guard = true
//
// (fmruby-core/flash/etc/system_conf.toml on the host; no shipped config has
// the key). Nothing else changes: the sim has no remote control server
// (devctl is ESP32 only), so the switch, the pairing number and "forget"
// only move the guard's state. The switch lives in memory and is lost with
// the process; the "hash" is not SHA-256, as nothing in the sim checks a
// token. ESP32 builds (release ones included), the browser build and the
// installed native package compile this to an empty function: their
// behaviour does not change. The development native program reads the key
// too (same Linux code), which is harmless.

#include "fmrb_remote_guard.h"
// sdkconfig.h (CONFIG_IDF_TARGET_LINUX) the way shared code reaches it.
#include "fmrb_attr.h"

#if defined(CONFIG_IDF_TARGET_LINUX) && !defined(FMRB_PLATFORM_WASM) && \
    !(defined(FMRB_NATIVE_PACKAGE) && FMRB_NATIVE_PACKAGE)

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fmrb_log.h"
#include "fmrb_toml.h"

static const char *TAG = "rguard_sim";

static fmrb_rguard_blob_t s_blob;
static bool s_stored = false;

static bool sim_load(void *blob, size_t len)
{
    if (!s_stored || len != sizeof(s_blob)) return false;
    memcpy(blob, &s_blob, len);
    return true;
}

static bool sim_save(const void *blob, size_t len)
{
    if (len != sizeof(s_blob)) return false;
    memcpy(&s_blob, blob, len);
    s_stored = true;
    return true;
}

static void sim_random(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(rand() & 0xFF);
}

// Not a cryptographic hash: a fold of the input, enough to tell tokens apart.
static void sim_hash(const void *in, size_t len, uint8_t out[FMRB_RGUARD_HASH_BYTES])
{
    const uint8_t *p = (const uint8_t *)in;
    memset(out, 0, FMRB_RGUARD_HASH_BYTES);
    for (size_t i = 0; i < len; i++) {
        out[i % FMRB_RGUARD_HASH_BYTES] ^= (uint8_t)(p[i] + i * 31u);
    }
}

static const fmrb_rguard_ops_t s_sim_ops = {
    .load = sim_load, .save = sim_save, .random = sim_random, .sha256 = sim_hash,
    .lock = NULL, .unlock = NULL, .cut = NULL,
};

void fmrb_rguard_sim_arm_from_conf(void)
{
    char errbuf[64];
    toml_table_t *conf = fmrb_toml_load_file("/etc/system_conf.toml", errbuf, sizeof(errbuf));
    if (!conf) return;
    bool on = fmrb_toml_get_bool(conf, "sim_remote_guard", false);
    toml_free(conf);
    if (!on) return;
    srand((unsigned)time(NULL));
    fmrb_rguard_arm(&s_sim_ops);
    FMRB_LOGW(TAG, "sim_remote_guard: Config shows the release build's remote control row "
                   "(state only, no server)");
}

#else

void fmrb_rguard_sim_arm_from_conf(void)
{
}

#endif
