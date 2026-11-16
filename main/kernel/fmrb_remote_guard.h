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
 * - The remote control is closed until the owner turns on "development remote
 *   control" in Config; it stays open until the board restarts.
 * - While it is open the board shows a six-digit number. A PC sends it once
 *   (POST /pair) and receives a token; the tools send the token with every
 *   request. The number is used once (a new one replaces it), and after
 *   FMRB_RGUARD_CODE_TRIES wrong numbers pairing stops until the switch is
 *   turned off and on again.
 * - Up to FMRB_RGUARD_MAX_TOKENS tokens are kept (NVS on the board, through
 *   the ops); a fifth PC replaces the one paired longest ago. Config can
 *   revoke them all.
 * - Files are written only under /home and /app.
 *
 * Pure C over memory and the ops the platform supplies (storage, randomness,
 * a lock), so the rules are tested on the host (test/remote_guard).
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

/** One stored token. seq 0 is an empty slot; a larger seq was paired later. */
typedef struct {
    uint8_t token[FMRB_RGUARD_TOKEN_BYTES];
    uint32_t seq;
} fmrb_rguard_slot_t;

/** What load and save move: the whole table, as it is in memory. */
#define FMRB_RGUARD_BLOB_SIZE (sizeof(fmrb_rguard_slot_t) * FMRB_RGUARD_MAX_TOKENS)

/** What the platform supplies. lock and unlock may be NULL (one task). */
typedef struct {
    bool (*load)(void *blob, size_t len);        /**< false: nothing stored yet */
    bool (*save)(const void *blob, size_t len);
    void (*random)(void *buf, size_t len);
    void (*lock)(void);
    void (*unlock)(void);
} fmrb_rguard_ops_t;

typedef enum {
    FMRB_RGUARD_NONE = -1,   /**< not armed: a development build, or no server */
    FMRB_RGUARD_CLOSED = 0,  /**< armed, the switch in Config is off */
    FMRB_RGUARD_OPEN = 1,    /**< armed and turned on until the next restart */
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
    FMRB_RGUARD_PAIR_WRONG,        /**< wrong number, tries left */
    FMRB_RGUARD_PAIR_LOCKED,       /**< too many wrong numbers: off and on again */
} fmrb_rguard_pair_t;

/**
 * @brief Arm the guard (a release build's server calls this once).
 *
 * Loads the stored tokens through ops->load. The ops must outlive the guard.
 * A second call does nothing.
 */
void fmrb_rguard_arm(const fmrb_rguard_ops_t *ops);

/** @brief FMRB_RGUARD_NONE, _CLOSED or _OPEN. */
fmrb_rguard_state_t fmrb_rguard_state(void);

/**
 * @brief Turn the remote control on or off (Config). Turning it on shows a
 *        new number and forgets the wrong tries. Does nothing unless armed.
 */
void fmrb_rguard_set_open(bool open);

/** @brief The number to show (0..999999), or -1 while closed or locked. */
int32_t fmrb_rguard_code(void);

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
 * @brief Trade the number shown on the board for a token.
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
 *        directory at vpath: only under /home and /app (G1).
 */
bool fmrb_rguard_write_allowed(const char *vpath);

/** @brief Back to the unarmed state. For the host tests only. */
void fmrb_rguard_disarm(void);

#ifdef __cplusplus
}
#endif
