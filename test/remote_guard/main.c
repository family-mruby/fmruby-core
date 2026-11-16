// Host tests for the release build's remote control guard
// (main/kernel/fmrb_remote_guard.c): the switch, the pairing number, the
// tokens kept for several PCs, revoking them, and the places a paired PC may
// write (doc/remote_guard G1, G2). A mistake here is silent on the board --
// a door left open or a PC that can never pair -- so the rules run here.

#include "fmrb_remote_guard.h"

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

static const fmrb_rguard_ops_t s_ops = {
    .load = fake_load, .save = fake_save, .random = fake_random,
    .lock = fake_lock, .unlock = fake_unlock,
};

static void fresh(void)
{
    fmrb_rguard_disarm();
    memset(s_store, 0, sizeof(s_store));
    s_stored = false;
    s_saves = 0;
    s_save_fails = false;
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

static void test_pair_and_check(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    fmrb_rguard_set_open(true);
    int32_t c1 = fmrb_rguard_code();
    CHECK(c1 >= 0 && c1 <= 999999, "number in range: %d", (int)c1);
    CHECK(fmrb_rguard_check(NULL) == FMRB_RGUARD_DENY_NO_TOKEN, "open, no token");
    CHECK(fmrb_rguard_check("") == FMRB_RGUARD_DENY_NO_TOKEN, "open, empty token");
    CHECK(fmrb_rguard_check("abc") == FMRB_RGUARD_DENY_BAD_TOKEN, "short token");

    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(tok), "pair with the shown number");
    CHECK(strlen(tok) == FMRB_RGUARD_TOKEN_HEX_LEN, "token length %zu", strlen(tok));
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "paired token allowed");
    CHECK(fmrb_rguard_paired() == 1, "one paired");
    CHECK(s_saves == 1 && s_stored, "token saved");
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

    // Off again: the token is kept but the door is shut.
    fmrb_rguard_set_open(false);
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_DENY_CLOSED, "closed refuses a paired token");
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "opened again, no new pairing needed");
    CHECK(s_locks == 0, "lock balanced");
}

static void test_tokens_survive_restart(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    fmrb_rguard_set_open(true);
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(tok), "pair");
    // A restart: memory gone, the store kept.
    fmrb_rguard_disarm();
    fmrb_rguard_arm(&s_ops);
    CHECK(fmrb_rguard_state() == FMRB_RGUARD_CLOSED, "closed after restart");
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_DENY_CLOSED, "closed after restart, token too");
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_check(tok) == FMRB_RGUARD_ALLOW, "token loaded from the store");
    CHECK(fmrb_rguard_paired() == 1, "one paired after restart");
}

static void test_four_pcs_then_oldest_goes(void)
{
    fresh();
    fmrb_rguard_arm(&s_ops);
    fmrb_rguard_set_open(true);
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
    fmrb_rguard_set_open(true);
    char a[FMRB_RGUARD_TOKEN_HEX_LEN + 1], b[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    CHECK(pair_now(a) && pair_now(b), "pair two");
    CHECK(fmrb_rguard_revoke_all(), "revoke");
    CHECK(fmrb_rguard_paired() == 0, "none paired");
    CHECK(fmrb_rguard_check(a) == FMRB_RGUARD_DENY_BAD_TOKEN, "a revoked");
    CHECK(fmrb_rguard_check(b) == FMRB_RGUARD_DENY_BAD_TOKEN, "b revoked");
    // Revoked in the store too.
    fmrb_rguard_disarm();
    fmrb_rguard_arm(&s_ops);
    fmrb_rguard_set_open(true);
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
    fmrb_rguard_set_open(true);
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
    CHECK(fmrb_rguard_code() == -1, "locked: no number shown");
    char rc[16];
    snprintf(rc, sizeof(rc), "%06d", (int)right);
    CHECK(fmrb_rguard_pair(rc, tok, NULL) == FMRB_RGUARD_PAIR_LOCKED, "locked refuses the right number");
    CHECK(fmrb_rguard_paired() == 0, "nothing paired");
    // Off and on: a new number, tries forgotten.
    fmrb_rguard_set_open(false);
    fmrb_rguard_set_open(true);
    CHECK(fmrb_rguard_code() >= 0, "number again");
    CHECK(pair_now(tok), "pairs after off and on");
    // A right number after some wrong ones resets the count.
    CHECK(fmrb_rguard_pair("x", tok, &left) == FMRB_RGUARD_PAIR_WRONG, "wrong after pairing");
    CHECK(left == FMRB_RGUARD_CODE_TRIES - 1, "count reset by the success: %d", left);
    CHECK(s_locks == 0, "lock balanced");
}

static void test_short_number_form(void)
{
    // "000123" and "123" are the same number: a tool may drop the zeros.
    fresh();
    fmrb_rguard_arm(&s_ops);
    for (int i = 0; i < 200; i++) {
        fmrb_rguard_set_open(true);
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
        { "/homework", false }, { "/apps", false }, { "/application/x", false },
        { "/etc/system_conf.toml", false }, { "/usr/share/x", false }, { "/boot/x", false },
        { "/mnt/sd/x", false }, { "/", false }, { "", false }, { "home/x", false },
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
    CHECK(strstr(fmrb_rguard_reason(FMRB_RGUARD_DENY_NO_TOKEN), "pair") != NULL, "no token reason");
    CHECK(strcmp(fmrb_rguard_verdict_name(FMRB_RGUARD_DENY_BAD_TOKEN), "bad_token") == 0, "name");
}

int main(void)
{
    test_unarmed_is_free();
    test_closed_by_default();
    test_pair_and_check();
    test_tokens_survive_restart();
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
