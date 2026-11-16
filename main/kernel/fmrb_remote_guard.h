#pragma once

/**
 * @file fmrb_remote_guard.h
 * @brief Who may use the WiFi remote control of a release build: the switch
 *        in Config, the pairing number, the PCs' tokens, and where a paired PC
 *        may write files.
 *
 * A development build (FMRB_DEV_REMOTE_CTL) never arms the guard: its remote
 * control stays free, and every check here answers "allowed". A release build
 * arms it when the HTTP server comes up (devctl), and then:
 *
 * - The remote control is closed until the owner turns on "Remote control" in
 *   Config. The switch is kept (through the ops: NVS on the board), so it
 *   stays as it was left across restarts until it is turned off.
 * - A PC already paired is let in whenever the switch is on.
 * - A new PC pairs only while the owner has "Pair a PC" open in Config: then
 *   the board shows a six-digit number, which the PC sends once (POST /pair)
 *   for a token; the tools send the token with every request. The number is
 *   used once (a new one replaces it), and after FMRB_RGUARD_CODE_TRIES wrong
 *   numbers pairing stops until "Pair a PC" is opened again.
 * - The board keeps only the SHA-256 of each token, for up to
 *   FMRB_RGUARD_MAX_TOKENS PCs; a fifth PC replaces the one paired longest
 *   ago. Config can revoke them all.
 * - Turning the switch off, or revoking, cuts what is connected (ops->cut,
 *   and fmrb_rguard_epoch for a stream that polls).
 * - Files are written only under /home, /app and /mnt/sd.
 *
 * Pure C over memory and the ops the platform supplies (storage, randomness,
 * SHA-256, a lock, cutting connections), so the rules are tested on the host
 * (test/remote_guard).
 * Plan: doc/remote_guard/plan.md (G1, G2).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FMRB_RGUARD_MAX_TOKENS     4
#define FMRB_RGUARD_TOKEN_BYTES    16
#define FMRB_RGUARD_TOKEN_HEX_LEN  (FMRB_RGUARD_TOKEN_BYTES * 2)
#define FMRB_RGUARD_CODE_TRIES     5
#define FMRB_RGUARD_HASH_BYTES     32

/** One stored PC: the SHA-256 of its token's 16 bytes. seq 0 is an empty
 *  slot; a larger seq was paired later. */
typedef struct {
    uint8_t hash[FMRB_RGUARD_HASH_BYTES];
    uint32_t seq;
} fmrb_rguard_slot_t;

/** "FRG2": the table with hashes and the switch. Anything else read back (the
 *  first version's plaintext tokens had no header) is dropped. */
#define FMRB_RGUARD_BLOB_MAGIC 0x32475246u

#define FMRB_RGUARD_FLAG_OPEN  0x1u

/** What load and save move. */
typedef struct {
    uint32_t magic;
    uint32_t flags;   /**< FMRB_RGUARD_FLAG_OPEN: the switch is on */
    fmrb_rguard_slot_t slots[FMRB_RGUARD_MAX_TOKENS];
} fmrb_rguard_blob_t;

#define FMRB_RGUARD_BLOB_SIZE sizeof(fmrb_rguard_blob_t)

/** What the platform supplies. lock, unlock and cut may be NULL. */
typedef struct {
    bool (*load)(void *blob, size_t len);        /**< false: nothing stored yet */
    bool (*save)(const void *blob, size_t len);
    void (*random)(void *buf, size_t len);
    void (*sha256)(const void *in, size_t len, uint8_t out[FMRB_RGUARD_HASH_BYTES]);
    void (*lock)(void);
    void (*unlock)(void);
    /** Close the connections that are open now (the switch went off, or the
     *  PCs were revoked). Called outside the lock. */
    void (*cut)(void);
} fmrb_rguard_ops_t;

typedef enum {
    FMRB_RGUARD_NONE = -1,   /**< not armed: a development build, or no server */
    FMRB_RGUARD_CLOSED = 0,  /**< armed, the switch in Config is off */
    FMRB_RGUARD_OPEN = 1,    /**< armed and turned on (kept until turned off) */
} fmrb_rguard_state_t;

typedef enum {
    FMRB_RGUARD_ALLOW = 0,
    FMRB_RGUARD_DENY_CLOSED,     /**< the switch is off */
    FMRB_RGUARD_DENY_NO_TOKEN,   /**< no token sent */
    FMRB_RGUARD_DENY_BAD_TOKEN,  /**< a token the board does not know */
} fmrb_rguard_verdict_t;

typedef enum {
    FMRB_RGUARD_PAIR_OK = 0,
    FMRB_RGUARD_PAIR_CLOSED,       /**< the switch is off */
    FMRB_RGUARD_PAIR_NOT_PAIRING,  /**< on, but "Pair a PC" is not open in Config */
    FMRB_RGUARD_PAIR_WRONG,        /**< wrong number, tries left */
    FMRB_RGUARD_PAIR_LOCKED,       /**< too many wrong numbers: open "Pair a PC" again */
} fmrb_rguard_pair_t;

/**
 * @brief Arm the guard (a release build's server calls this once).
 *
 * Loads the stored hashes and the switch through ops->load (a table that is
 * not this version's is dropped). ops->random and ops->sha256 are required.
 * The ops must outlive the guard. A second call does nothing.
 */
void fmrb_rguard_arm(const fmrb_rguard_ops_t *ops);

/** @brief FMRB_RGUARD_NONE, _CLOSED or _OPEN. */
fmrb_rguard_state_t fmrb_rguard_state(void);

/**
 * @brief Turn the remote control on or off (Config), and keep it so
 *        (ops->save). Turning it off ends pairing and cuts the connections.
 *        Does nothing unless armed.
 */
void fmrb_rguard_set_open(bool open);

/**
 * @brief Open or close "Pair a PC" (Config, while it is on the screen).
 *        Opening shows a new number and forgets the wrong tries (it is what
 *        lifts the lock). Not kept: a restart closes it. Ignored while the
 *        switch is off or the guard is not armed.
 */
void fmrb_rguard_set_pairing(bool on);

/** @brief Whether "Pair a PC" is open. */
bool fmrb_rguard_pairing(void);

/**
 * @brief The number to show (0..999999); -1 when there is none (not pairing,
 *        closed, unarmed); -2 while pairing is locked by wrong numbers.
 */
int32_t fmrb_rguard_code(void);

/**
 * @brief A count that moves whenever the switch goes off or the PCs are
 *        revoked. A long-lived stream compares it with the value it started
 *        with and stops when it differs.
 */
uint32_t fmrb_rguard_epoch(void);

/** @brief How many PCs hold a token. */
int fmrb_rguard_paired(void);

/** @brief Forget every token. False when the storage could not be written. */
bool fmrb_rguard_revoke_all(void);

/**
 * @brief Whether a request carrying token (hex, may be NULL) may go on.
 *        Always FMRB_RGUARD_ALLOW when the guard is not armed.
 */
fmrb_rguard_verdict_t fmrb_rguard_check(const char *token);

/**
 * @brief Trade the number shown on the board for a token (only while "Pair a
 *        PC" is open).
 * @param code the number as the PC sent it (six digits)
 * @param out_token receives the token as hex on FMRB_RGUARD_PAIR_OK
 * @param tries_left receives how many wrong numbers remain (may be NULL)
 */
fmrb_rguard_pair_t fmrb_rguard_pair(const char *code,
                                    char out_token[FMRB_RGUARD_TOKEN_HEX_LEN + 1],
                                    int *tries_left);

/** @brief A sentence for the person at the PC, for a refused request. */
const char *fmrb_rguard_reason(fmrb_rguard_verdict_t v);

/** @brief "closed", "no_token", "bad_token" (or "ok"), for the tools. */
const char *fmrb_rguard_verdict_name(fmrb_rguard_verdict_t v);

/**
 * @brief Whether a restricted (release) build writes, deletes or makes a
 *        directory at vpath: only under /home, /app and /mnt/sd (G1).
 */
bool fmrb_rguard_write_allowed(const char *vpath);

/** @brief Back to the unarmed state. For the host tests only. */
void fmrb_rguard_disarm(void);

#ifdef __cplusplus
}
#endif
