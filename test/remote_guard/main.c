// Host tests for the release build's remote control guard
// (main/kernel/fmrb_remote_guard.c): the switch, the pairing number, the
// tokens' hashes kept for several PCs, revoking them, cutting connections,
// and the places a paired PC may write (doc/remote_guard G1, G2). A mistake here is silent on the board --
// a door left open or a PC that can never pair -- so the rules run here.

#include "fmrb_remote_guard.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_count = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_count++;                                                    \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: ", __func__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
            g_fail++;                                                 \
        }                                                             \
    } while (0)

// ---- fake platform: storage, randomness, lock ----

static unsigned char s_store[FMRB_RGUARD_BLOB_SIZE];
static bool s_stored = false;
static int s_saves = 0;
static bool s_save_fails = false;
static unsigned s_rand = 12345;
static int s_locks = 0;
static int s_cuts = 0;

static bool fake_load(void *blob, size_t len)
{
    if (!s_stored || len != sizeof(s_store)) return false;
    memcpy(blob, s_store, len);
    return true;
}

static bool fake_save(const void *blob, size_t len)
{
    s_saves++;
    if (s_save_fails || len != sizeof(s_store)) return false;
    memcpy(s_store, blob, len);
    s_stored = true;
    return true;
}

static void fake_random(void *buf, size_t len)
{
    unsigned char *p = buf;
    for (size_t i = 0; i < len; i++) {
        s_rand = s_rand * 1103515245u + 12345u;
        p[i] = (unsigned char)(s_rand >> 16);
    }
}

static void fake_lock(void) { s_locks++; }
static void fake_unlock(void) { s_locks--; }
// Called outside the lock: a cut that runs under it could deadlock on the board.
static void fake_cut(void) { if (s_locks == 0) s_cuts++; else s_cuts += 1000; }

static const fmrb_rguard_ops_t s_ops = {
    .load = fake_load, .save = fake_save, .random = fake_random,
    .sha256 = test_sha256, .lock = fake_lock, .unlock = fake_unlock, .cut = fake_cut,
};

static const fmrb_rguard_blob_t *stored(void)
{
    return (const fmrb_rguard_blob_t *)s_store;
}

// Switch on and open "Pair a PC", as the owner does in Config.
static void open_pairing(void)
{
    fmrb_rguard_set_open(true);
    fmrb_rguard_set_pairing(true);
}

static void fresh(void)
{
    fmrb_rguard_disarm();
    memset(s_store, 0, sizeof(s_store));
    s_stored = false;
    s_saves = 0;
    s_save_fails = false;
    s_cuts = 0;
}

static void code_str(char *out, size_t cap)
{
    snprintf(out, cap, "%06d", (int)fmrb_rguard_code());
}

// Pair with the number on the screen; returns the token in out.
static bool pair_now(char out[FMRB_RGUARD_TOKEN_HEX_LEN + 1])
{
    char code[16];
    code_str(code, sizeof(code));
    return fmrb_rguard_pair(code, out, NULL) == FMRB_RGUARD_PAIR_OK;
}

// ---- tests ----

static void test_unarmed_is_free(void)
{
    fresh();
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_NONE, "unarmed state");
    CHECK(fmrb_rguard_check(NULL) == FMRB_RGUARD_ALLOW, "unarmed: no token allowed");
    CHECK(fmrb_rguard_check("zz") == FMRB_RGUARD_ALLOW, "unarmed: any token allowed");
    CHECK(fmrb_rguard_code() == -1, "unarmed: no number");
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_NONE, "set_open does not arm");
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(fmrb_rguard_pair("000000", tok, NULL) == FMRB_RGUARD_PAIR_CLOSED, "unarmed: no pairing");
}

static void test_closed_by_default(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_CLOSED, "armed starts closed");
    CHECK(fmrb_rguard_check(NULL) == FMRB_RGUARD_DENY_CLOSED, "closed refuses no token");
    CHECK(fmrb_rguard_code() == -1, "closed shows no number");
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(fmrb_rguard_pair("123456", tok, NULL) == FMRB_RGUARD_PAIR_CLOSED, "closed refuses pairing");
    CHECK(s_locks == 0, "lock balanced");
}

static void test_sha256_vector(void)
{
    // FIPS 180-2 "abc": the host stand-in for mbedtls is right.
    static const uint8_t want[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t got[32];
    test_sha256("abc", 3, got);
    CHECK(memcmp(got, want, 32) == 0, "sha256(abc)");
}

static void test_pairing_only_while_open(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_OPEN, "on");
    CHECK(!fmrb_rguard_pairing(), "on is not pairing");
    CHECK(fmrb_rguard_code() == -1, "on but not pairing: no number");
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    for (int i = 0; i < FMRB_RGUARD_CODE_TRIES + 2; i++) {
        CHECK(fmrb_rguard_pair("123456", tok, NULL) == FMRB_RGUARD_PAIR_NOT_PAIRING,
              "not pairing refuses %d", i);
    }
    fmrb_rguard_set_pairing(true);
    CHECK(fmrb_rguard_pairing(), "pairing");
    CHECK(fmrb_rguard_code() >= 0, "pairing shows a number");
    // Refusals outside the view did not count towards the lock.
    char code[16];
    code_str(code, sizeof(code));
    CHECK(fmrb_rguard_pair(code, tok, NULL) == FMRB_RGUARD_PAIR_OK, "pairs in the view");
    CHECK(fmrb_rguard_pairing() && fmrb_rguard_code() >= 0, "stays open for the next PC");
    fmrb_rguard_set_pairing(false);
    CHECK(fmrb_rguard_code() == -1, "closed view: no number");
    CHECK(fmrb_rguard_pair(code, tok, NULL) == FMRB_RGUARD_PAIR_NOT_PAIRING, "closed view refuses");
    // Pairing cannot be opened while the switch is off.
    fmrb_rguard_set_open(false);
    fmrb_rguard_set_pairing(true);
    CHECK(!fmrb_rguard_pairing() && fmrb_rguard_code() == -1, "no pairing while off");
    CHECK(fmrb_rguard_pair(code, tok, NULL) == FMRB_RGUARD_PAIR_CLOSED, "off refuses as closed");
    // Turning it off ends the view: on again is not pairing.
    fmrb_rguard_set_open(true);
    fmrb_rguard_set_pairing(true);
    fmrb_rguard_set_open(false);
    fmrb_rguard_set_open(true);
    CHECK(!fmrb_rguard_pairing(), "off ends pairing");
    CHECK(s_locks == 0, "lock balanced");
}

static void test_pair_and_check(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    open_pairing();
    int32_t c1 = fmrb_rguard_code();
    CHECK(c1 >= 0 && c1 <= 999999, "number in range: %d", (int)c1);
    CHECK(fmrb_rguard_check(NULL) == FMRB_RGUARD_DENY_NO_TOKEN, "open, no token");
    CHECK(fmrb_rguard_check("") == FMRB_RGUARD_DENY_NO_TOKEN, "open, empty token");
    CHECK(fmrb_rguard_check("abc") == FMRB_RGUARD_DENY_BAD_TOKEN, "short token");

    int saves0 = s_saves;
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(tok), "pair with the shown number");
    CHECK(strlen(tok) == FMRB_RGUARD_TOKEN_HEX_LEN, "token length %zu", strlen(tok));
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "paired token allowed");
    CHECK(fmrb_rguard_paired() == 1, "one paired");
    CHECK(s_saves == saves0 + 1 && s_stored, "pairing saved");
    CHECK(fmrb_rguard_code() != c1 || c1 < 0, "number changes after use");

    // Upper-case hex is the same token.
    char up[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    for (size_t i = 0; i <= FMRB_RGUARD_TOKEN_HEX_LEN; i++) {
        char ch = tok[i];
        up[i] = (ch >= 'a' && ch <= 'f') ? (char)(ch - 32) : ch;
    }
    CHECK(fmrb_rguard_check(up) == FMRB_RGUARD_ALLOW, "upper-case token");

    // One digit off is someone else's.
    char other[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    memcpy(other, tok, sizeof(other));
    other[5] = (other[5] == '0') ? '1' : '0';
    CHECK(fmrb_rguard_check(other) == FMRB_RGUARD_DENY_BAD_TOKEN, "altered token refused");

    // The same number twice: the first use retired it.
    char code_used[16];
    snprintf(code_used, sizeof(code_used), "%06d", (int)c1);
    char tok2[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    if (fmrb_rguard_code() != c1) {
        CHECK(fmrb_rguard_pair(code_used, tok2, NULL) == FMRB_RGUARD_PAIR_WRONG, "used number refused");
    }

    // A paired PC needs no pairing view.
    fmrb_rguard_set_pairing(false);
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "paired PC works without the view");

    // Off again: the token is kept but the door is shut.
    fmrb_rguard_set_open(false);
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_DENY_CLOSED, "closed refuses a paired token");
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "opened again, no new pairing needed");
    CHECK(s_locks == 0, "lock balanced");
}

static void test_store_holds_hashes(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    open_pairing();
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(tok), "pair");
    uint8_t raw[FMRB_RGUARD_TOKEN_BYTES];
    for (int i = 0; i < FMRB_RGUARD_TOKEN_BYTES; i++) {
        unsigned v = 0;
        sscanf(tok + i * 2, "%2x", &v);
        raw[i] = (uint8_t)v;
    }
    // The token's bytes appear nowhere in what is stored ...
    bool found = false;
    for (size_t off = 0; off + sizeof(raw) <= sizeof(s_store); off++) {
        if (memcmp(s_store + off, raw, sizeof(raw)) == 0) found = true;
    }
    CHECK(!found, "the token itself is not stored");
    // ... its SHA-256 is.
    uint8_t h[32];
    test_sha256(raw, sizeof(raw), h);
    bool has_hash = false;
    for (int i = 0; i < FMRB_RGUARD_MAX_TOKENS; i++) {
        if (stored()->slots[i].seq != 0 && memcmp(stored()->slots[i].hash, h, 32) == 0) has_hash = true;
    }
    CHECK(has_hash, "the token's SHA-256 is stored");
    CHECK(stored()->magic == FMRB_RGUARD_BLOB_MAGIC, "stored with the version mark");
    // The stored hash is not a token: sending it back is refused.
    char hex[65];
    for (int i = 0; i < FMRB_RGUARD_TOKEN_BYTES; i++) snprintf(hex + i * 2, 3, "%02x", h[i]);
    CHECK(fmrb_rguard_check(hex) == FMRB_RGUARD_DENY_BAD_TOKEN, "the hash's first half is no token");
}

static void test_old_plaintext_table_dropped(void)
{
    // The first version stored 4 x (16-byte token + seq) with no header. A
    // table that is not this version's starts empty and closed.
    fresh();
    memset(s_store, 0xA5, sizeof(s_store));
    s_stored = true;
    fmrb_rguard_arm(&s_ops);
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_CLOSED, "an unknown table: closed");
    CHECK(fmrb_rguard_paired() == 0, "an unknown table: nobody paired");
    // A short read (the old 80-byte blob) fails load on the board, same result.
    fresh();
    fmrb_rguard_arm(&s_ops);
    CHECK(fmrb_rguard_paired() == 0 && fmrb_rguard_state() == FMRB_RGUARD_CLOSED, "nothing stored");
}

static void test_switch_survives_restart(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    open_pairing();
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(tok), "pair");
    // A restart: memory gone, the store kept. The switch is still on, the
    // pairing view is not.
    fmrb_rguard_disarm();
    fmrb_rguard_arm(&s_ops);
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_OPEN, "still on after restart");
    CHECK(!fmrb_rguard_pairing() && fmrb_rguard_code() == -1, "not pairing after restart");
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "token works after restart");
    CHECK(fmrb_rguard_paired() == 1, "one paired after restart");
    // Off, restart: still off, the token kept.
    fmrb_rguard_set_open(false);
    fmrb_rguard_disarm();
    fmrb_rguard_arm(&s_ops);
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_CLOSED, "still off after restart");
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_DENY_CLOSED, "off after restart refuses");
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "on again, the token still good");
    // Setting the same state again does not write.
    int saves = s_saves;
    fmrb_rguard_set_open(true);
    CHECK(s_saves == saves, "no write when nothing changes");
}

static void test_off_and_revoke_cut(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    open_pairing();
    uint32_t e0 = fmrb_rguard_epoch();
    CHECK(s_cuts == 0, "turning on cuts nothing");
    fmrb_rguard_set_pairing(false);
    CHECK(s_cuts == 0 && fmrb_rguard_epoch() == e0, "closing the view cuts nothing");
    fmrb_rguard_set_open(false);
    CHECK(s_cuts == 1, "off cuts once, outside the lock (%d)", s_cuts);
    CHECK(fmrb_rguard_epoch() != e0, "off moves the epoch");
    uint32_t e1 = fmrb_rguard_epoch();
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_epoch() == e1, "on does not move the epoch");
    CHECK(fmrb_rguard_revoke_all(), "revoke");
    CHECK(s_cuts == 2, "revoke cuts too (%d)", s_cuts);
    CHECK(fmrb_rguard_epoch() != e1, "revoke moves the epoch");
}

static void test_four_pcs_then_oldest_goes(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    open_pairing();
    char tok[FMRB_RGUARD_MAX_TOKENS + 1][FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    for (int i = 0; i < FMRB_RGUARD_MAX_TOKENS; i++) {
        CHECK(pair_now(tok[i]), "pair %d", i);
    }
    CHECK(fmrb_rguard_paired() == FMRB_RGUARD_MAX_TOKENS, "four paired");
    for (int i = 0; i < FMRB_RGUARD_MAX_TOKENS; i++) {
        CHECK(fmrb_rguard_check(tok[i]) == FMRB_RGUARD_ALLOW, "token %d allowed", i);
    }
    CHECK(pair_now(tok[FMRB_RGUARD_MAX_TOKENS]), "fifth pairs");
    CHECK(fmrb_rguard_paired() == FMRB_RGUARD_MAX_TOKENS, "still four");
    CHECK(fmrb_rguard_check(tok[0]) == FMRB_RGUARD_DENY_BAD_TOKEN, "the oldest is gone");
    for (int i = 1; i <= FMRB_RGUARD_MAX_TOKENS; i++) {
        CHECK(fmrb_rguard_check(tok[i]) == FMRB_RGUARD_ALLOW, "token %d kept", i);
    }
    // The next one pushes out the second oldest, not the newest.
    char t6[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(t6), "sixth pairs");
    CHECK(fmrb_rguard_check(tok[1]) == FMRB_RGUARD_DENY_BAD_TOKEN, "second oldest gone");
    CHECK(fmrb_rguard_check(tok[FMRB_RGUARD_MAX_TOKENS]) == FMRB_RGUARD_ALLOW, "newest kept");
}

static void test_revoke_all(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    open_pairing();
    char a[FMRB_RGUARD_TOKEN_HEX_LEN + 1], b[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(a) && pair_now(b), "pair two");
    CHECK(fmrb_rguard_revoke_all(), "revoke");
    CHECK(fmrb_rguard_paired() == 0, "none paired");
    CHECK(fmrb_rguard_check(a) == FMRB_RGUARD_DENY_BAD_TOKEN, "a revoked");
    CHECK(fmrb_rguard_check(b) == FMRB_RGUARD_DENY_BAD_TOKEN, "b revoked");
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_OPEN, "revoking leaves the switch on");
    // Revoked in the store too.
    fmrb_rguard_disarm();
    fmrb_rguard_arm(&s_ops);
    CHECK(fmrb_rguard_check(a) == FMRB_RGUARD_DENY_BAD_TOKEN, "a revoked after restart");
    CHECK(fmrb_rguard_paired() == 0, "none after restart");
    // A failing store says so.
    s_save_fails = true;
    CHECK(!fmrb_rguard_revoke_all(), "revoke reports a failed save");
}

static void test_wrong_numbers_lock(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    open_pairing();
    int32_t right = fmrb_rguard_code();
    char wrong[16];
    snprintf(wrong, sizeof(wrong), "%06d", (int)((right + 1) % 1000000));
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    int left = -1;
    CHECK(fmrb_rguard_pair(wrong, tok, &left) == FMRB_RGUARD_PAIR_WRONG, "wrong 1");
    CHECK(left == FMRB_RGUARD_CODE_TRIES - 1, "tries left %d", left);
    CHECK(fmrb_rguard_pair("abc", tok, &left) == FMRB_RGUARD_PAIR_WRONG, "not digits counts");
    CHECK(fmrb_rguard_pair("1234567", tok, &left) == FMRB_RGUARD_PAIR_WRONG, "seven digits counts");
    CHECK(fmrb_rguard_pair(NULL, tok, &left) == FMRB_RGUARD_PAIR_WRONG, "NULL counts");
    CHECK(left == 1, "one left: %d", left);
    CHECK(fmrb_rguard_pair(wrong, tok, &left) == FMRB_RGUARD_PAIR_LOCKED, "fifth wrong locks");
    CHECK(fmrb_rguard_code() == -2, "locked: -2, no number shown");
    char rc[16];
    snprintf(rc, sizeof(rc), "%06d", (int)right);
    CHECK(fmrb_rguard_pair(rc, tok, NULL) == FMRB_RGUARD_PAIR_LOCKED, "locked refuses the right number");
    CHECK(fmrb_rguard_paired() == 0, "nothing paired");
    // Leaving the view and coming back: a new number, tries forgotten.
    fmrb_rguard_set_pairing(false);
    CHECK(fmrb_rguard_code() == -1, "view closed: no number, no lock shown");
    fmrb_rguard_set_pairing(true);
    CHECK(fmrb_rguard_code() >= 0, "number again");
    CHECK(pair_now(tok), "pairs after the view is opened again");
    // A right number after some wrong ones resets the count.
    CHECK(fmrb_rguard_pair("x", tok, &left) == FMRB_RGUARD_PAIR_WRONG, "wrong after pairing");
    CHECK(left == FMRB_RGUARD_CODE_TRIES - 1, "count reset by the success: %d", left);
    // Off and on alone does not reopen pairing after a lock.
    for (int i = 0; i < FMRB_RGUARD_CODE_TRIES; i++) fmrb_rguard_pair("x", tok, NULL);
    CHECK(fmrb_rguard_code() == -2, "locked again");
    fmrb_rguard_set_open(false);
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_pair(rc, tok, NULL) == FMRB_RGUARD_PAIR_NOT_PAIRING, "off and on: not pairing");
    CHECK(s_locks == 0, "lock balanced");
}

static void test_short_number_form(void)
{
    // "000123" and "123" are the same number: a tool may drop the zeros.
    fresh();
    fmrb_rguard_arm(&s_ops);
    fmrb_rguard_set_open(true);
    for (int i = 0; i < 200; i++) {
        fmrb_rguard_set_pairing(true);
        if (fmrb_rguard_code() < 100000) break;
    }
    int32_t c = fmrb_rguard_code();
    if (c < 100000) {
        char s[16];
        snprintf(s, sizeof(s), "%d", (int)c);
        char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
        CHECK(fmrb_rguard_pair(s, tok, NULL) == FMRB_RGUARD_PAIR_OK, "number without leading zeros");
    }
}

static void test_write_rule(void)
{
    struct { const char *p; bool ok; } cases[] = {
        { "/home", true }, { "/home/", true }, { "/home/a.rb", true },
        { "/home/asterism/x.tar", true }, { "/app", true }, { "/app/test/x.app.rb", true },
        { "/mnt/sd", true }, { "/mnt/sd/x", true }, { "/mnt/sd/dir/y.bin", true },
        { "/homework", false }, { "/apps", false }, { "/application/x", false },
        { "/mnt/sdcard/x", false }, { "/mnt", false }, { "/mnt/x", false },
        { "/etc/system_conf.toml", false }, { "/usr/share/x", false }, { "/boot/x", false },
        { "/", false }, { "", false }, { "home/x", false },
        { "/var/cache/x", false }, { "/tmp/x", false },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(fmrb_rguard_write_allowed(cases[i].p) == cases[i].ok, "write %s", cases[i].p);
    }
    CHECK(!fmrb_rguard_write_allowed(NULL), "NULL path");
}

static void test_reasons(void)
{
    CHECK(strstr(fmrb_rguard_reason(FMRB_RGUARD_DENY_CLOSED), "allow it on the board") != NULL, "closed reason");
    CHECK(strstr(fmrb_rguard_reason(FMRB_RGUARD_DENY_NO_TOKEN), "Pair a PC") != NULL, "no token reason");
    CHECK(strcmp(fmrb_rguard_verdict_name(FMRB_RGUARD_DENY_BAD_TOKEN), "bad_token") == 0, "name");
}

int main(void)
{
    test_sha256_vector();
    test_unarmed_is_free();
    test_closed_by_default();
    test_pairing_only_while_open();
    test_pair_and_check();
    test_store_holds_hashes();
    test_old_plaintext_table_dropped();
    test_switch_survives_restart();
    test_off_and_revoke_cut();
    test_four_pcs_then_oldest_goes();
    test_revoke_all();
    test_wrong_numbers_lock();
    test_short_number_form();
    test_write_rule();
    test_reasons();
    if (g_fail) {
        printf("remote_guard: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("remote_guard: %d checks passed\n", g_count);
    return 0;
}
