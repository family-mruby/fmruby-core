// The release build's WiFi remote control guard: the switch, the pairing
// number, the tokens' hashes, and where files may be written. The rules are in the
// header (fmrb_remote_guard.h); doc/remote_guard/plan.md has the decisions.
//
// Pure C. The board supplies storage (NVS), randomness, SHA-256, a lock and
// a way to cut connections through
// fmrb_rguard_ops_t; the host tests supply fakes (test/remote_guard).

#include "fmrb_remote_guard.h"

#include <stdio.h>
#include <string.h>

#if defined(FMRB_RGUARD_HOST_TEST)
#define FMRB_EXT_RAM_BSS_ATTR
#else
#include "fmrb_attr.h"
#endif

typedef struct {
    const fmrb_rguard_ops_t *ops;   // NULL: not armed
    bool open;                      // the switch (kept in the blob's flags)
    bool pairing;                   // "Pair a PC" is open in Config
    bool locked;                    // too many wrong numbers since it was opened
    int32_t code;                   // -1: none shown
    int wrong;
    uint32_t epoch;                 // moves when the switch goes off or on revoke
    fmrb_rguard_blob_t blob;        // what load and save move
} rguard_t;

// External RAM where the board has it: read on every request, but a few
// hundred nanoseconds do not matter next to a WiFi round trip, and the
// internal RAM does not grow for a feature most boards never turn on.
FMRB_EXT_RAM_BSS_ATTR static rguard_t s_g;

static void g_lock(void)
{
    if (s_g.ops && s_g.ops->lock) s_g.ops->lock();
}

static void g_unlock(void)
{
    if (s_g.ops && s_g.ops->unlock) s_g.ops->unlock();
}

static void g_cut(void)
{
    if (s_g.ops && s_g.ops->cut) s_g.ops->cut();
}

static void new_code(void)
{
    uint32_t r = 0;
    s_g.ops->random(&r, sizeof(r));
    s_g.code = (int32_t)(r % 1000000u);
}

static bool save_blob(void)
{
    s_g.blob.magic = FMRB_RGUARD_BLOB_MAGIC;
    s_g.blob.flags = s_g.open ? FMRB_RGUARD_FLAG_OPEN : 0;
    return s_g.ops->save ? s_g.ops->save(&s_g.blob, sizeof(s_g.blob)) : true;
}

void fmrb_rguard_arm(const fmrb_rguard_ops_t *ops)
{
    if (s_g.ops || !ops || !ops->random || !ops->sha256) return;
    memset(&s_g, 0, sizeof(s_g));
    s_g.code = -1;
    // Anything that is not this version's table -- nothing stored, a short
    // read, or the first version's plaintext tokens -- starts empty and closed.
    if (!ops->load || !ops->load(&s_g.blob, sizeof(s_g.blob)) ||
        s_g.blob.magic != FMRB_RGUARD_BLOB_MAGIC) {
        memset(&s_g.blob, 0, sizeof(s_g.blob));
    }
    s_g.open = (s_g.blob.flags & FMRB_RGUARD_FLAG_OPEN) != 0;
    s_g.ops = ops;
}

void fmrb_rguard_disarm(void)
{
    memset(&s_g, 0, sizeof(s_g));
    s_g.code = -1;
}

fmrb_rguard_state_t fmrb_rguard_state(void)
{
    if (!s_g.ops) return FMRB_RGUARD_NONE;
    return s_g.open ? FMRB_RGUARD_OPEN : FMRB_RGUARD_CLOSED;
}

void fmrb_rguard_set_open(bool open)
{
    if (!s_g.ops) return;
    g_lock();
    bool was = s_g.open;
    s_g.open = open;
    if (!open) {
        s_g.pairing = false;
        s_g.locked = false;
        s_g.wrong = 0;
        s_g.code = -1;
        s_g.epoch++;
    }
    // A failed save still holds until the restart; the platform logs it.
    if (was != open) (void)save_blob();
    g_unlock();
    if (!open) g_cut();
}

void fmrb_rguard_set_pairing(bool on)
{
    if (!s_g.ops) return;
    g_lock();
    if (on && s_g.open) {
        s_g.pairing = true;
        s_g.wrong = 0;
        s_g.locked = false;
        new_code();
    } else {
        s_g.pairing = false;
        s_g.locked = false;
        s_g.wrong = 0;
        s_g.code = -1;
    }
    g_unlock();
}

bool fmrb_rguard_pairing(void)
{
    return s_g.ops && s_g.open && s_g.pairing;
}

int32_t fmrb_rguard_code(void)
{
    if (!s_g.ops || !s_g.open || !s_g.pairing) return -1;
    if (s_g.locked) return -2;
    return s_g.code;
}

uint32_t fmrb_rguard_epoch(void)
{
    return s_g.epoch;
}

int fmrb_rguard_paired(void)
{
    if (!s_g.ops) return 0;
    int n = 0;
    g_lock();
    for (int i = 0; i < FMRB_RGUARD_MAX_TOKENS; i++) {
        if (s_g.blob.slots[i].seq != 0) n++;
    }
    g_unlock();
    return n;
}

bool fmrb_rguard_revoke_all(void)
{
    if (!s_g.ops) return true;
    g_lock();
    memset(s_g.blob.slots, 0, sizeof(s_g.blob.slots));
    s_g.epoch++;
    bool ok = save_blob();
    g_unlock();
    g_cut();
    return ok;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The token's bytes, or false when it is not FMRB_RGUARD_TOKEN_HEX_LEN hex
// digits.
static bool parse_token(const char *hex, uint8_t out[FMRB_RGUARD_TOKEN_BYTES])
{
    if (strlen(hex) != FMRB_RGUARD_TOKEN_HEX_LEN) return false;
    for (int i = 0; i < FMRB_RGUARD_TOKEN_BYTES; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

fmrb_rguard_verdict_t fmrb_rguard_check(const char *token)
{
    if (!s_g.ops) return FMRB_RGUARD_ALLOW;
    if (!s_g.open) return FMRB_RGUARD_DENY_CLOSED;
    if (!token || token[0] == '\0') return FMRB_RGUARD_DENY_NO_TOKEN;
    uint8_t t[FMRB_RGUARD_TOKEN_BYTES];
    if (!parse_token(token, t)) return FMRB_RGUARD_DENY_BAD_TOKEN;
    // The board keeps only hashes: a flash dump gives no token to send.
    uint8_t h[FMRB_RGUARD_HASH_BYTES];
    s_g.ops->sha256(t, sizeof(t), h);
    // Every slot and every byte, whatever matches: the time a refusal takes
    // says nothing about how close the guess was.
    bool found = false;
    g_lock();
    for (int i = 0; i < FMRB_RGUARD_MAX_TOKENS; i++) {
        uint8_t diff = 0;
        for (int j = 0; j < FMRB_RGUARD_HASH_BYTES; j++) {
            diff |= (uint8_t)(s_g.blob.slots[i].hash[j] ^ h[j]);
        }
        found |= (diff == 0 && s_g.blob.slots[i].seq != 0);
    }
    g_unlock();
    return found ? FMRB_RGUARD_ALLOW : FMRB_RGUARD_DENY_BAD_TOKEN;
}

static bool parse_code(const char *code, int32_t *out)
{
    if (!code) return false;
    size_t n = strlen(code);
    if (n == 0 || n > 6) return false;
    int32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (code[i] < '0' || code[i] > '9') return false;
        v = v * 10 + (code[i] - '0');
    }
    *out = v;
    return true;
}

fmrb_rguard_pair_t fmrb_rguard_pair(const char *code,
                                    char out_token[FMRB_RGUARD_TOKEN_HEX_LEN + 1],
                                    int *tries_left)
{
    if (tries_left) *tries_left = 0;
    if (!s_g.ops || !s_g.open) return FMRB_RGUARD_PAIR_CLOSED;
    g_lock();
    if (!s_g.pairing) {
        // No number is shown, so nothing is guessed: not counted.
        g_unlock();
        return FMRB_RGUARD_PAIR_NOT_PAIRING;
    }
    if (s_g.locked) {
        g_unlock();
        return FMRB_RGUARD_PAIR_LOCKED;
    }
    int32_t v = -1;
    if (!parse_code(code, &v) || v != s_g.code) {
        s_g.wrong++;
        if (s_g.wrong >= FMRB_RGUARD_CODE_TRIES) {
            s_g.locked = true;
            s_g.code = -1;
            g_unlock();
            return FMRB_RGUARD_PAIR_LOCKED;
        }
        if (tries_left) *tries_left = FMRB_RGUARD_CODE_TRIES - s_g.wrong;
        g_unlock();
        return FMRB_RGUARD_PAIR_WRONG;
    }

    // An empty slot, or else the one paired longest ago.
    int pick = 0;
    uint32_t top = 0;
    fmrb_rguard_slot_t *slots = s_g.blob.slots;
    for (int i = 0; i < FMRB_RGUARD_MAX_TOKENS; i++) {
        if (slots[i].seq > top) top = slots[i].seq;
    }
    for (int i = 0; i < FMRB_RGUARD_MAX_TOKENS; i++) {
        if (slots[i].seq == 0) { pick = i; break; }
        if (slots[i].seq < slots[pick].seq) pick = i;
    }
    // The token goes to the PC; the board keeps its hash and forgets it.
    uint8_t token[FMRB_RGUARD_TOKEN_BYTES];
    s_g.ops->random(token, sizeof(token));
    s_g.ops->sha256(token, sizeof(token), slots[pick].hash);
    slots[pick].seq = top + 1;
    for (int i = 0; i < FMRB_RGUARD_TOKEN_BYTES; i++) {
        snprintf(out_token + i * 2, 3, "%02x", token[i]);
    }
    out_token[FMRB_RGUARD_TOKEN_HEX_LEN] = '\0';
    memset(token, 0, sizeof(token));
    // A failed save still pairs until the restart; the platform logs it.
    (void)save_blob();
    // The number is used once: the next PC reads a new one off the screen.
    s_g.wrong = 0;
    new_code();
    g_unlock();
    return FMRB_RGUARD_PAIR_OK;
}

const char *fmrb_rguard_reason(fmrb_rguard_verdict_t v)
{
    switch (v) {
    case FMRB_RGUARD_DENY_CLOSED:
        return "remote control is off on this board: allow it on the board "
               "(Config > Remote control: ON); a PC paired before needs nothing more";
    case FMRB_RGUARD_DENY_NO_TOKEN:
        return "this PC is not paired: on the board open Config > Remote control > "
               "Pair a PC and send the number it shows (pair <number>)";
    case FMRB_RGUARD_DENY_BAD_TOKEN:
        return "the board does not know this PC's token (revoked?): pair again "
               "with the number shown in Config > Remote control > Pair a PC";
    default:
        return "ok";
    }
}

const char *fmrb_rguard_verdict_name(fmrb_rguard_verdict_t v)
{
    switch (v) {
    case FMRB_RGUARD_DENY_CLOSED:    return "closed";
    case FMRB_RGUARD_DENY_NO_TOKEN:  return "no_token";
    case FMRB_RGUARD_DENY_BAD_TOKEN: return "bad_token";
    default:                         return "ok";
    }
}

bool fmrb_rguard_write_allowed(const char *vpath)
{
    static const char *const roots[] = { "/home", "/app", "/mnt/sd" };
    if (!vpath) return false;
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        size_t n = strlen(roots[i]);
        if (strncmp(vpath, roots[i], n) == 0 && (vpath[n] == '\0' || vpath[n] == '/')) {
            return true;
        }
    }
    return false;
}
