/*
 * picoruby-asterism-zenoh: a thin Ruby layer over zenoh-pico (Asterism::Zenoh).
 *
 * Covers: sessions (client, or peer with an optional listener), put /
 * subscribe, get / queryable (query and reply), liveliness tokens and
 * liveliness watches.
 *
 * Threading model: zenoh-pico is built single-threaded, so nothing happens
 * behind the interpreter's back. The application calls Session#poll from its
 * own loop; poll runs zp_spin_once(), which reads the sockets and may call the
 * callbacks below. No callback touches the mruby VM. Each one copies what it
 * received into a bounded ring owned by the Ruby-side object (buffers from
 * zenoh-pico's allocator, z_malloc), or keeps a reference to it (a query, by
 * z_query_clone), and the Ruby methods (each_pending, each_reply) turn them
 * into Ruby values later. When a ring is full the oldest entry is dropped and
 * counted.
 *
 * Losing the connection: zenoh-pico's client read path does not tell a closed
 * connection from an idle one, so the gem asks its TCP link
 * (include/picoruby_zenoh_link.h) after every poll and put. When the router
 * has closed the connection, the socket failed, or a send ran out of time,
 * the session is closed here: poll returns false, closed? is true and put
 * raises Asterism::Zenoh::Error. A peer session that only connects (no listener) is
 * closed the same way once it has no peer left; a listening peer session
 * stays open while peers come and go. There is no reconnection; the
 * application opens a new session.
 *
 * Lifetime: closing is optional. The Ruby objects close their zenoh-pico
 * counterparts when they are freed, in any order (an interpreter shutdown
 * frees objects in no particular order): a session keeps lists of its live
 * subscribers, queryables and tokens and undeclares them before it closes,
 * and detaches them so a later free does not touch the freed session. A get
 * is owned jointly by its Ruby object and zenoh-pico (which calls the reply
 * closure's drop when the query is finished or times out); whichever lets go
 * last frees it.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/error.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include <zenoh-pico.h>
#include <zenoh-pico/link/link.h>
#include <zenoh-pico/net/session.h>

#include "picoruby_zenoh_link.h"

#define ZRB_DEFAULT_DEPTH 16
#define ZRB_MAX_DEPTH 1024
#define ZRB_DEFAULT_POLL_STEPS 8
#define ZRB_DEFAULT_GET_TIMEOUT_MS 2000
#define ZRB_MAX_GET_TIMEOUT_MS 600000

typedef struct zrb_sub zrb_sub;
typedef struct zrb_qable zrb_qable;
typedef struct zrb_token zrb_token;

typedef struct {
    z_owned_session_t session;
    bool open;
    bool peer;      /* peer mode (otherwise client) */
    bool listening; /* peer mode with a listener: stays open without peers */
    zrb_sub *subs;  /* live subscribers and liveliness watches */
    zrb_qable *qables;
    zrb_token *tokens;
} zrb_session;

typedef struct {
    uint8_t *buf; /* key bytes followed by payload bytes (z_malloc) */
    size_t key_len;
    size_t payload_len;
    bool alive; /* liveliness watches: the token appeared (true) or went away */
} zrb_entry;

/* A bounded queue of received values (key + payload). */
typedef struct {
    zrb_entry *slots;
    uint32_t depth;
    uint32_t head;
    uint32_t count;
    uint32_t received;
    uint32_t dropped;
} zrb_ring;

struct zrb_sub {
    z_owned_subscriber_t sub;
    bool declared;
    bool liveliness;    /* a liveliness watch rather than a data subscriber */
    zrb_session *owner; /* NULL once detached (closed, or the session went away) */
    zrb_sub *next;
    zrb_ring ring; /* slots from mrb_malloc */
};

struct zrb_qable {
    z_owned_queryable_t qable;
    bool declared;
    zrb_session *owner;
    zrb_qable *next;
    z_owned_query_t *slots; /* depth cloned queries (mrb_malloc) */
    uint32_t depth;
    uint32_t head;
    uint32_t count;
    uint32_t received;
    uint32_t dropped;
};

struct zrb_token {
    z_owned_liveliness_token_t token;
    bool declared;
    zrb_session *owner;
    zrb_token *next;
};

/* A get in flight. Allocated with z_malloc (not the mruby allocator): the
 * reply closure's drop may run after the Ruby object, or the whole VM, has
 * gone. */
typedef struct {
    zrb_ring ring; /* slots from z_malloc */
    uint32_t errors;
    bool done;     /* zenoh-pico dropped the closure: all replies in, or timed out */
    bool rb_alive; /* the Ruby object still refers to it */
} zrb_get;

typedef struct {
    z_owned_query_t query;
    bool live;
} zrb_query;

static void zrb_session_free(mrb_state *mrb, void *p);
static void zrb_sub_free(mrb_state *mrb, void *p);
static void zrb_qable_free(mrb_state *mrb, void *p);
static void zrb_token_free(mrb_state *mrb, void *p);
static void zrb_get_free(mrb_state *mrb, void *p);
static void zrb_query_free(mrb_state *mrb, void *p);

static const struct mrb_data_type zrb_session_type = {"Asterism::Zenoh::Session", zrb_session_free};
static const struct mrb_data_type zrb_sub_type = {"Asterism::Zenoh::Subscriber", zrb_sub_free};
static const struct mrb_data_type zrb_qable_type = {"Asterism::Zenoh::Queryable", zrb_qable_free};
static const struct mrb_data_type zrb_token_type = {"Asterism::Zenoh::LivelinessToken", zrb_token_free};
static const struct mrb_data_type zrb_get_type = {"Asterism::Zenoh::Get", zrb_get_free};
static const struct mrb_data_type zrb_query_type = {"Asterism::Zenoh::Query", zrb_query_free};

static struct RClass *zrb_class(mrb_state *mrb, const char *name) {
    struct RClass *mod = mrb_module_get_under(mrb, mrb_module_get(mrb, "Asterism"), "Zenoh");
    return mrb_class_get_under(mrb, mod, name);
}

static struct RClass *zrb_error_class(mrb_state *mrb) { return zrb_class(mrb, "Error"); }

static void zrb_view_key(mrb_state *mrb, z_view_keyexpr_t *ke, const char *key) {
    if (z_view_keyexpr_from_str(ke, key) != Z_OK) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid key expression: %s", key);
    }
}

static mrb_int zrb_check_depth(mrb_state *mrb, mrb_int depth) {
    if (depth < 1 || depth > ZRB_MAX_DEPTH) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "depth must be 1..%d", ZRB_MAX_DEPTH);
    }
    return depth;
}

static mrb_int zrb_check_timeout(mrb_state *mrb, mrb_int timeout_ms) {
    if (timeout_ms < 1 || timeout_ms > ZRB_MAX_GET_TIMEOUT_MS) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "timeout must be 1..%d ms", ZRB_MAX_GET_TIMEOUT_MS);
    }
    return timeout_ms;
}

/* Copy a loaned string-ish view to a Ruby String. */
static mrb_value zrb_str_from_view(mrb_state *mrb, const z_loaned_string_t *s) {
    return mrb_str_new(mrb, z_string_data(s), (mrb_int)z_string_len(s));
}

static mrb_value zrb_str_from_bytes(mrb_state *mrb, const z_loaned_bytes_t *b) {
    size_t len = z_bytes_len(b);
    mrb_value str = mrb_str_new(mrb, NULL, (mrb_int)len);
    z_bytes_reader_t reader = z_bytes_get_reader(b);
    size_t got = z_bytes_reader_read(&reader, (uint8_t *)RSTRING_PTR(str), len);
    if (got != len) {
        mrb_str_resize(mrb, str, (mrb_int)got);
    }
    return str;
}

/* ------------------------------------------------------------------ ring */

static void zrb_ring_clear(zrb_ring *r) {
    while (r->count > 0) {
        z_free(r->slots[r->head].buf);
        r->slots[r->head].buf = NULL;
        r->head = (r->head + 1) % r->depth;
        r->count--;
    }
    r->head = 0;
}

/* Store key + payload. Called from inside zp_spin_once(): no VM access. */
static void zrb_ring_push(zrb_ring *r, const z_loaned_keyexpr_t *keyexpr, const z_loaned_bytes_t *payload,
                          bool alive) {
    z_view_string_t ks;
    if (z_keyexpr_as_view_string(keyexpr, &ks) != Z_OK) {
        r->dropped++;
        return;
    }
    const char *kd = z_string_data(z_loan(ks));
    size_t kl = z_string_len(z_loan(ks));
    size_t plen = (payload == NULL) ? 0 : z_bytes_len(payload);

    uint8_t *buf = (uint8_t *)z_malloc(kl + plen + 1);
    if (buf == NULL) {
        r->dropped++;
        return;
    }
    memcpy(buf, kd, kl);
    if (plen > 0) {
        z_bytes_reader_t reader = z_bytes_get_reader(payload);
        size_t got = z_bytes_reader_read(&reader, buf + kl, plen);
        if (got != plen) {
            z_free(buf);
            r->dropped++;
            return;
        }
    }

    if (r->count == r->depth) {
        /* Full: drop the oldest. */
        z_free(r->slots[r->head].buf);
        r->slots[r->head].buf = NULL;
        r->head = (r->head + 1) % r->depth;
        r->count--;
        r->dropped++;
    }
    uint32_t idx = (r->head + r->count) % r->depth;
    r->slots[idx].buf = buf;
    r->slots[idx].key_len = kl;
    r->slots[idx].payload_len = plen;
    r->slots[idx].alive = alive;
    r->count++;
    r->received++;
}

/* Take out what is pending now, oldest first: yield [key, second] to blk, or
 * collect them into an Array when blk is nil. second is the payload String,
 * or the alive flag for liveliness entries. Values that arrive while the
 * block runs (it may poll) are left for the next call. */
static mrb_value zrb_ring_take(mrb_state *mrb, zrb_ring *r, mrb_value blk, bool liveliness) {
    bool collect = mrb_nil_p(blk);
    mrb_value out = collect ? mrb_ary_new(mrb) : mrb_nil_value();
    mrb_int taken = 0;
    uint32_t todo = r->count;
    while (todo > 0 && r->count > 0) {
        todo--;
        int ai = mrb_gc_arena_save(mrb);
        /* Make the Ruby values while the entry is still owned by the ring: if
         * an allocation raises, the value stays queued (and is freed with the
         * ring) instead of leaking. */
        zrb_entry e = r->slots[r->head];
        mrb_value pair[2];
        pair[0] = mrb_str_new(mrb, (const char *)e.buf, (mrb_int)e.key_len);
        pair[1] = liveliness ? mrb_bool_value(e.alive)
                             : mrb_str_new(mrb, (const char *)e.buf + e.key_len, (mrb_int)e.payload_len);
        r->slots[r->head].buf = NULL;
        r->head = (r->head + 1) % r->depth;
        r->count--;
        z_free(e.buf);
        taken++;
        if (collect) {
            mrb_ary_push(mrb, out, mrb_ary_new_from_values(mrb, 2, pair));
        } else {
            mrb_yield_argv(mrb, blk, 2, pair);
        }
        mrb_gc_arena_restore(mrb, ai);
    }
    return collect ? out : mrb_fixnum_value(taken);
}

/* ------------------------------------------------------------ subscriber */

/* Called from inside zp_spin_once(). Must not call into the VM. */
static void zrb_on_sample(z_loaned_sample_t *sample, void *ctx) {
    zrb_sub *s = (zrb_sub *)ctx;
    if (s == NULL || s->ring.slots == NULL) {
        return;
    }
    if (s->liveliness) {
        /* A token appearing is a PUT, one going away a DELETE. */
        zrb_ring_push(&s->ring, z_sample_keyexpr(sample), NULL, z_sample_kind(sample) == Z_SAMPLE_KIND_PUT);
    } else {
        zrb_ring_push(&s->ring, z_sample_keyexpr(sample), z_sample_payload(sample), true);
    }
}

/* Undeclare and unlink from the owning session. Keeps the ring (pending
 * values can still be read after close). */
static void zrb_sub_detach(zrb_sub *s) {
    if (s->declared) {
        z_drop(z_move(s->sub));
        s->declared = false;
    }
    if (s->owner != NULL) {
        zrb_sub **pp = &s->owner->subs;
        while (*pp != NULL) {
            if (*pp == s) {
                *pp = s->next;
                break;
            }
            pp = &(*pp)->next;
        }
        s->owner = NULL;
        s->next = NULL;
    }
}

static void zrb_sub_free(mrb_state *mrb, void *p) {
    zrb_sub *s = (zrb_sub *)p;
    if (s == NULL) {
        return;
    }
    zrb_sub_detach(s);
    if (s->ring.slots != NULL) {
        zrb_ring_clear(&s->ring);
        mrb_free(mrb, s->ring.slots);
    }
    mrb_free(mrb, s);
}

static zrb_sub *zrb_sub_get(mrb_state *mrb, mrb_value self) {
    zrb_sub *s = (zrb_sub *)mrb_data_get_ptr(mrb, self, &zrb_sub_type);
    if (s == NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Asterism::Zenoh::Subscriber");
    }
    return s;
}

/* sub.each_pending { |key, payload| ... }   -> Integer (values taken)
 * watch.each_pending { |key, alive| ... }   -> Integer
 * Without a block: Array of [key, payload] / [key, alive]. */
static mrb_value zrb_sub_each_pending(mrb_state *mrb, mrb_value self) {
    mrb_value blk = mrb_nil_value();
    mrb_get_args(mrb, "&", &blk);
    zrb_sub *s = zrb_sub_get(mrb, self);
    return zrb_ring_take(mrb, &s->ring, blk, s->liveliness);
}

static mrb_value zrb_sub_pending(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_sub_get(mrb, self)->ring.count);
}

static mrb_value zrb_sub_received(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_sub_get(mrb, self)->ring.received);
}

static mrb_value zrb_sub_dropped(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_sub_get(mrb, self)->ring.dropped);
}

static mrb_value zrb_sub_close(mrb_state *mrb, mrb_value self) {
    zrb_sub_detach(zrb_sub_get(mrb, self));
    return mrb_nil_value();
}

static mrb_value zrb_sub_closed_p(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(!zrb_sub_get(mrb, self)->declared);
}

/* ------------------------------------------------------------- queryable */

/* Called from inside zp_spin_once(). Must not call into the VM. Keeps a
 * reference to the query so it can be answered later; dropping the last
 * reference sends the final reply, which ends the query for the requester. */
static void zrb_on_query(z_loaned_query_t *query, void *ctx) {
    zrb_qable *q = (zrb_qable *)ctx;
    if (q == NULL || q->slots == NULL) {
        return;
    }
    if (q->count == q->depth) {
        /* Full: finish the oldest unanswered. */
        z_drop(z_move(q->slots[q->head]));
        q->head = (q->head + 1) % q->depth;
        q->count--;
        q->dropped++;
    }
    uint32_t idx = (q->head + q->count) % q->depth;
    if (z_query_clone(&q->slots[idx], query) != Z_OK) {
        q->dropped++;
        return;
    }
    q->count++;
    q->received++;
}

/* Finish every query still waiting (each sends its final reply). */
static void zrb_qable_finish_pending(zrb_qable *q) {
    while (q->count > 0) {
        z_drop(z_move(q->slots[q->head]));
        q->head = (q->head + 1) % q->depth;
        q->count--;
    }
    q->head = 0;
}

static void zrb_qable_detach(zrb_qable *q) {
    if (q->slots != NULL) {
        zrb_qable_finish_pending(q);
    }
    if (q->declared) {
        z_drop(z_move(q->qable));
        q->declared = false;
    }
    if (q->owner != NULL) {
        zrb_qable **pp = &q->owner->qables;
        while (*pp != NULL) {
            if (*pp == q) {
                *pp = q->next;
                break;
            }
            pp = &(*pp)->next;
        }
        q->owner = NULL;
        q->next = NULL;
    }
}

static void zrb_qable_free(mrb_state *mrb, void *p) {
    zrb_qable *q = (zrb_qable *)p;
    if (q == NULL) {
        return;
    }
    zrb_qable_detach(q);
    if (q->slots != NULL) {
        mrb_free(mrb, q->slots);
    }
    mrb_free(mrb, q);
}

static zrb_qable *zrb_qable_get(mrb_state *mrb, mrb_value self) {
    zrb_qable *q = (zrb_qable *)mrb_data_get_ptr(mrb, self, &zrb_qable_type);
    if (q == NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Asterism::Zenoh::Queryable");
    }
    return q;
}

static mrb_value zrb_query_wrap(mrb_state *mrb, z_owned_query_t *moved_from) {
    struct RData *data = mrb_data_object_alloc(mrb, zrb_class(mrb, "Query"), NULL, &zrb_query_type);
    zrb_query *zq = (zrb_query *)mrb_malloc(mrb, sizeof(zrb_query));
    zq->query = *moved_from;
    zq->live = true;
    z_internal_query_null(moved_from);
    data->data = zq;
    return mrb_obj_value(data);
}

static void zrb_query_finish(zrb_query *zq) {
    if (zq->live) {
        z_drop(z_move(zq->query));
        zq->live = false;
    }
}

typedef struct {
    mrb_value blk;
    mrb_value query;
} zrb_yield_args;

static mrb_value zrb_query_yield_body(mrb_state *mrb, void *ud) {
    zrb_yield_args *a = (zrb_yield_args *)ud;
    return mrb_yield(mrb, a->blk, a->query);
}

/* Yield one query and finish it afterwards, also when the block raises. */
static void zrb_query_yield(mrb_state *mrb, mrb_value blk, mrb_value obj) {
    zrb_yield_args a = {blk, obj};
    mrb_bool error = FALSE;
    mrb_value result = mrb_protect_error(mrb, zrb_query_yield_body, &a, &error);
    zrb_query *zq = (zrb_query *)mrb_data_get_ptr(mrb, obj, &zrb_query_type);
    if (zq != NULL) {
        zrb_query_finish(zq);
    }
    if (error) {
        mrb_exc_raise(mrb, result);
    }
}

/* qa.each_pending { |q| ... } -> Integer (queries taken). Each query is
 * finished when the block returns (answered or not): its final reply is
 * sent, which ends the query for the requester.
 * qa.each_pending -> Array of Query; each stays open until Query#finish or
 * until the object is garbage-collected. */
static mrb_value zrb_qable_each_pending(mrb_state *mrb, mrb_value self) {
    mrb_value blk = mrb_nil_value();
    mrb_get_args(mrb, "&", &blk);
    zrb_qable *q = zrb_qable_get(mrb, self);
    bool collect = mrb_nil_p(blk);
    mrb_value out = collect ? mrb_ary_new(mrb) : mrb_nil_value();
    mrb_int taken = 0;
    uint32_t todo = q->count;
    while (todo > 0 && q->count > 0) {
        todo--;
        int ai = mrb_gc_arena_save(mrb);
        mrb_value obj = zrb_query_wrap(mrb, &q->slots[q->head]);
        q->head = (q->head + 1) % q->depth;
        q->count--;
        taken++;
        if (collect) {
            mrb_ary_push(mrb, out, obj);
        } else {
            zrb_query_yield(mrb, blk, obj);
        }
        mrb_gc_arena_restore(mrb, ai);
    }
    return collect ? out : mrb_fixnum_value(taken);
}

static mrb_value zrb_qable_pending(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_qable_get(mrb, self)->count);
}

static mrb_value zrb_qable_received(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_qable_get(mrb, self)->received);
}

static mrb_value zrb_qable_dropped(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_qable_get(mrb, self)->dropped);
}

static mrb_value zrb_qable_close(mrb_state *mrb, mrb_value self) {
    zrb_qable_detach(zrb_qable_get(mrb, self));
    return mrb_nil_value();
}

static mrb_value zrb_qable_closed_p(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(!zrb_qable_get(mrb, self)->declared);
}

/* ----------------------------------------------------------------- query */

static void zrb_query_free(mrb_state *mrb, void *p) {
    zrb_query *zq = (zrb_query *)p;
    if (zq == NULL) {
        return;
    }
    zrb_query_finish(zq);
    mrb_free(mrb, zq);
}

static zrb_query *zrb_query_get(mrb_state *mrb, mrb_value self) {
    zrb_query *zq = (zrb_query *)mrb_data_get_ptr(mrb, self, &zrb_query_type);
    if (zq == NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Asterism::Zenoh::Query");
    }
    return zq;
}

static zrb_query *zrb_query_get_live(mrb_state *mrb, mrb_value self) {
    zrb_query *zq = zrb_query_get(mrb, self);
    if (!zq->live) {
        mrb_raise(mrb, zrb_error_class(mrb), "the query is finished");
    }
    return zq;
}

static mrb_value zrb_query_key(mrb_state *mrb, mrb_value self) {
    zrb_query *zq = zrb_query_get_live(mrb, self);
    z_view_string_t ks;
    if (z_keyexpr_as_view_string(z_query_keyexpr(z_loan(zq->query)), &ks) != Z_OK) {
        return mrb_str_new_lit(mrb, "");
    }
    return zrb_str_from_view(mrb, z_loan(ks));
}

static mrb_value zrb_query_params(mrb_state *mrb, mrb_value self) {
    zrb_query *zq = zrb_query_get_live(mrb, self);
    z_view_string_t ps;
    z_query_parameters(z_loan(zq->query), &ps);
    return zrb_str_from_view(mrb, z_loan(ps));
}

static mrb_value zrb_query_payload(mrb_state *mrb, mrb_value self) {
    zrb_query *zq = zrb_query_get_live(mrb, self);
    const z_loaned_bytes_t *b = z_query_payload(z_loan(zq->query));
    if (b == NULL) {
        return mrb_str_new_lit(mrb, "");
    }
    return zrb_str_from_bytes(mrb, b);
}

/* q.reply(payload) / q.reply(key, payload) -> nil. The key defaults to the
 * query's key; it must match the query's key expression. May be called more
 * than once before the query is finished. */
static mrb_value zrb_query_reply(mrb_state *mrb, mrb_value self) {
    mrb_value a1, a2 = mrb_nil_value();
    mrb_int argc = mrb_get_args(mrb, "o|o", &a1, &a2);
    zrb_query *zq = zrb_query_get_live(mrb, self);
    mrb_value key_v, payload;
    if (argc == 1) {
        key_v = mrb_nil_value();
        payload = a1;
    } else {
        key_v = a1;
        payload = a2;
    }
    if (!mrb_string_p(payload)) {
        mrb_raise(mrb, E_TYPE_ERROR, "payload must be a String");
    }
    z_view_keyexpr_t ke;
    const z_loaned_keyexpr_t *kp;
    if (mrb_nil_p(key_v)) {
        kp = z_query_keyexpr(z_loan(zq->query));
    } else {
        zrb_view_key(mrb, &ke, mrb_string_value_cstr(mrb, &key_v));
        kp = z_loan(ke);
    }
    z_owned_bytes_t bytes;
    if (z_bytes_copy_from_buf(&bytes, (const uint8_t *)RSTRING_PTR(payload), (size_t)RSTRING_LEN(payload)) != Z_OK) {
        mrb_raise(mrb, zrb_error_class(mrb), "cannot allocate the payload");
    }
    z_result_t ret = z_query_reply(z_loan(zq->query), kp, z_move(bytes), NULL);
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "reply failed (%d)", (int)ret);
    }
    return mrb_nil_value();
}

/* q.finish -> nil. Sends the final reply. Idempotent. */
static mrb_value zrb_query_finish_m(mrb_state *mrb, mrb_value self) {
    zrb_query_finish(zrb_query_get(mrb, self));
    return mrb_nil_value();
}

static mrb_value zrb_query_finished_p(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(!zrb_query_get(mrb, self)->live);
}

/* ------------------------------------------------------------------- get */

static void zrb_get_release(zrb_get *g) {
    zrb_ring_clear(&g->ring);
    z_free(g->ring.slots);
    z_free(g);
}

/* Called from inside zp_spin_once() (or z_get itself). No VM access. */
static void zrb_on_reply(z_loaned_reply_t *reply, void *ctx) {
    zrb_get *g = (zrb_get *)ctx;
    if (g == NULL || !g->rb_alive) {
        return;
    }
    if (z_reply_is_ok(reply)) {
        const z_loaned_sample_t *sample = z_reply_ok(reply);
        zrb_ring_push(&g->ring, z_sample_keyexpr(sample), z_sample_payload(sample), true);
    } else {
        g->errors++;
    }
}

/* zenoh-pico is done with the query: every replier sent its final reply, the
 * time ran out, or the session closed. */
static void zrb_on_reply_drop(void *ctx) {
    zrb_get *g = (zrb_get *)ctx;
    if (g == NULL) {
        return;
    }
    g->done = true;
    if (!g->rb_alive) {
        zrb_get_release(g);
    }
}

static void zrb_get_free(mrb_state *mrb, void *p) {
    (void)mrb;
    zrb_get *g = (zrb_get *)p;
    if (g == NULL) {
        return;
    }
    g->rb_alive = false;
    if (g->done) {
        zrb_get_release(g);
    } else {
        /* zenoh-pico still holds it; the drop callback frees it. */
        zrb_ring_clear(&g->ring);
    }
}

static zrb_get *zrb_get_get(mrb_state *mrb, mrb_value self) {
    zrb_get *g = (zrb_get *)mrb_data_get_ptr(mrb, self, &zrb_get_type);
    if (g == NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Asterism::Zenoh::Get");
    }
    return g;
}

/* Allocate a Get object and its context (z_malloc). */
static mrb_value zrb_get_new(mrb_state *mrb, zrb_get **out) {
    struct RData *data = mrb_data_object_alloc(mrb, zrb_class(mrb, "Get"), NULL, &zrb_get_type);
    mrb_value obj = mrb_obj_value(data);
    zrb_get *g = (zrb_get *)z_malloc(sizeof(zrb_get));
    if (g == NULL) {
        mrb_raise(mrb, zrb_error_class(mrb), "cannot allocate the query");
    }
    memset(g, 0, sizeof(*g));
    g->ring.slots = (zrb_entry *)z_malloc(sizeof(zrb_entry) * ZRB_DEFAULT_DEPTH);
    if (g->ring.slots == NULL) {
        z_free(g);
        mrb_raise(mrb, zrb_error_class(mrb), "cannot allocate the query");
    }
    memset(g->ring.slots, 0, sizeof(zrb_entry) * ZRB_DEFAULT_DEPTH);
    g->ring.depth = ZRB_DEFAULT_DEPTH;
    g->rb_alive = true;
    data->data = g;
    *out = g;
    return obj;
}

/* g.each_reply { |key, payload| ... } -> Integer; without a block, Array. */
static mrb_value zrb_get_each_reply(mrb_state *mrb, mrb_value self) {
    mrb_value blk = mrb_nil_value();
    mrb_get_args(mrb, "&", &blk);
    return zrb_ring_take(mrb, &zrb_get_get(mrb, self)->ring, blk, false);
}

static mrb_value zrb_get_done_p(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(zrb_get_get(mrb, self)->done);
}

static mrb_value zrb_get_pending(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_get_get(mrb, self)->ring.count);
}

static mrb_value zrb_get_received(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_get_get(mrb, self)->ring.received);
}

static mrb_value zrb_get_dropped(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_get_get(mrb, self)->ring.dropped);
}

static mrb_value zrb_get_errors(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_get_get(mrb, self)->errors);
}

/* ----------------------------------------------------------------- token */

static void zrb_token_detach(zrb_token *t) {
    if (t->declared) {
        z_drop(z_move(t->token));
        t->declared = false;
    }
    if (t->owner != NULL) {
        zrb_token **pp = &t->owner->tokens;
        while (*pp != NULL) {
            if (*pp == t) {
                *pp = t->next;
                break;
            }
            pp = &(*pp)->next;
        }
        t->owner = NULL;
        t->next = NULL;
    }
}

static void zrb_token_free(mrb_state *mrb, void *p) {
    zrb_token *t = (zrb_token *)p;
    if (t == NULL) {
        return;
    }
    zrb_token_detach(t);
    mrb_free(mrb, t);
}

static zrb_token *zrb_token_get(mrb_state *mrb, mrb_value self) {
    zrb_token *t = (zrb_token *)mrb_data_get_ptr(mrb, self, &zrb_token_type);
    if (t == NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Asterism::Zenoh::LivelinessToken");
    }
    return t;
}

static mrb_value zrb_token_close(mrb_state *mrb, mrb_value self) {
    zrb_token_detach(zrb_token_get(mrb, self));
    return mrb_nil_value();
}

static mrb_value zrb_token_closed_p(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(!zrb_token_get(mrb, self)->declared);
}

/* --------------------------------------------------------------- session */

static void zrb_session_shutdown(zrb_session *z) {
    /* Queryables first: their unanswered queries send final replies while
     * the session can still carry them. */
    while (z->qables != NULL) {
        zrb_qable_detach(z->qables); /* unlinks itself */
    }
    while (z->tokens != NULL) {
        zrb_token_detach(z->tokens);
    }
    while (z->subs != NULL) {
        zrb_sub_detach(z->subs);
    }
    if (z->open) {
        /* Pending gets are dropped here (their drop callback marks them done). */
        z_close(z_loan_mut(z->session), NULL);
        z_drop(z_move(z->session));
        z->open = false;
    }
}

static void zrb_session_free(mrb_state *mrb, void *p) {
    zrb_session *z = (zrb_session *)p;
    if (z == NULL) {
        return;
    }
    zrb_session_shutdown(z);
    mrb_free(mrb, z);
}

static zrb_session *zrb_session_get(mrb_state *mrb, mrb_value self) {
    zrb_session *z = (zrb_session *)mrb_data_get_ptr(mrb, self, &zrb_session_type);
    if (z == NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Asterism::Zenoh::Session");
    }
    return z;
}

/* Close the session when its connection can no longer carry it (see the
 * comment at the top). Reaches into zenoh-pico's session for the transport;
 * the layout is that of the pinned release (lib/add/ZENOH_PICO_PIN).
 * Returns true when the session is (now) closed. */
static bool zrb_session_check_link(zrb_session *z) {
    if (!z->open) {
        return true;
    }
    if (z_session_is_closed(z_loan(z->session))) {
        zrb_session_shutdown(z);
        return true;
    }
    _z_session_t *s = _Z_RC_IN_VAL(z_loan(z->session));
    /* A router that stays silent past the lease: zenoh-pico clears the
     * transport (type NONE, link freed) but does not mark the session
     * closed. Treat it like a lost connection. */
    if (s->_tp._type == _Z_TRANSPORT_NONE) {
        zrb_session_shutdown(z);
        return true;
    }
    if (s->_tp._type != _Z_TRANSPORT_UNICAST_TYPE) {
        return false;
    }
    if (z->peer) {
        /* zenoh-pico drops a peer whose connection closed, failed or went
         * silent past the lease. A listener waits for the next one; a
         * session that only connected has nothing left. */
        if (z->listening) {
            return false;
        }
        if (_z_transport_peer_unicast_slist_is_empty(s->_tp._transport._unicast._peers)) {
            zrb_session_shutdown(z);
            return true;
        }
        return false;
    }
    const _z_link_t *link = s->_tp._transport._unicast._common._link;
    if (link == NULL || link->_cap._transport != Z_LINK_CAP_TRANSPORT_UNICAST) {
        return false;
    }
    if (zp_tcp_socket_closed(_z_link_get_socket(link))) {
        zrb_session_shutdown(z);
        return true;
    }
    return false;
}

static zrb_session *zrb_session_get_open(mrb_state *mrb, mrb_value self) {
    zrb_session *z = zrb_session_get(mrb, self);
    if (zrb_session_check_link(z)) {
        mrb_raise(mrb, zrb_error_class(mrb), "session is closed");
    }
    return z;
}

/* Keep the session object alive while obj is reachable. */
static void zrb_hold_session(mrb_state *mrb, mrb_value obj, mrb_value session, const char *key) {
    mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "@session"), session);
    if (key != NULL) {
        mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "@key"), mrb_str_new_cstr(mrb, key));
    }
}

/* Asterism::Zenoh::Session.open(locator = nil, mode: :client, listen: nil) -> Session
 * - client: connects to the router at locator.
 * - peer: connects to the peer at locator (if given) and/or listens on
 *   listen (e.g. "tcp/0.0.0.0:7447"). At least one of them is needed.
 * Raises Asterism::Zenoh::Error when the session cannot be opened. */
static mrb_value zrb_session_s_open(mrb_state *mrb, mrb_value klass) {
    mrb_value locator_v = mrb_nil_value();
    mrb_sym kw_names[2] = {mrb_intern_lit(mrb, "mode"), mrb_intern_lit(mrb, "listen")};
    mrb_value kw_values[2];
    mrb_kwargs kwargs = {2, 0, kw_names, kw_values, NULL};
    mrb_get_args(mrb, "|o:", &locator_v, &kwargs);

    bool peer = false;
    if (!mrb_undef_p(kw_values[0]) && !mrb_nil_p(kw_values[0])) {
        mrb_sym mode = mrb_obj_to_sym(mrb, kw_values[0]);
        if (mode == mrb_intern_lit(mrb, "peer")) {
            peer = true;
        } else if (mode != mrb_intern_lit(mrb, "client")) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "mode must be :client or :peer");
        }
    }
    const char *locator = mrb_nil_p(locator_v) ? NULL : mrb_string_value_cstr(mrb, &locator_v);
    mrb_value listen_v = mrb_undef_p(kw_values[1]) ? mrb_nil_value() : kw_values[1];
    const char *listen = mrb_nil_p(listen_v) ? NULL : mrb_string_value_cstr(mrb, &listen_v);
    if (!peer && listen != NULL) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "listen needs mode: :peer");
    }
    if (locator == NULL && listen == NULL) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "a locator (or, for a peer, listen:) is needed");
    }
#if Z_FEATURE_UNICAST_PEER == 0
    if (peer) {
        mrb_raise(mrb, zrb_error_class(mrb), "peer mode is not built in (Z_FEATURE_UNICAST_PEER)");
    }
#endif

    struct RClass *cls = mrb_class_ptr(klass);
    struct RData *data = mrb_data_object_alloc(mrb, cls, NULL, &zrb_session_type);
    zrb_session *z = (zrb_session *)mrb_malloc(mrb, sizeof(zrb_session));
    memset(z, 0, sizeof(*z));
    data->data = z;
    mrb_value obj = mrb_obj_value(data);

    z_owned_config_t config;
    if (z_config_default(&config) != Z_OK) {
        mrb_raise(mrb, zrb_error_class(mrb), "cannot create the configuration");
    }
    bool ok = zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY,
                               peer ? Z_CONFIG_MODE_PEER : Z_CONFIG_MODE_CLIENT) == Z_OK;
    if (ok && locator != NULL) {
        ok = zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator) == Z_OK;
    }
    if (ok && listen != NULL) {
        ok = zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, listen) == Z_OK;
    }
    if (!ok) {
        z_drop(z_move(config));
        mrb_raise(mrb, zrb_error_class(mrb), "invalid locator");
    }
    z_result_t ret = z_open(&z->session, z_move(config), NULL);
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "cannot open a session to %s (%d)",
                   locator != NULL ? locator : listen, (int)ret);
    }
    z->open = true;
    z->peer = peer;
    z->listening = (listen != NULL);
    return obj;
}

/* session.put(key, payload) -> nil */
static mrb_value zrb_session_put(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_value payload;
    mrb_get_args(mrb, "zS", &key, &payload);
    zrb_session *z = zrb_session_get_open(mrb, self);

    z_view_keyexpr_t ke;
    zrb_view_key(mrb, &ke, key);
    z_owned_bytes_t bytes;
    if (z_bytes_copy_from_buf(&bytes, (const uint8_t *)RSTRING_PTR(payload), (size_t)RSTRING_LEN(payload)) != Z_OK) {
        mrb_raise(mrb, zrb_error_class(mrb), "cannot allocate the payload");
    }
    z_result_t ret = z_put(z_loan(z->session), z_loan(ke), z_move(bytes), NULL);
    if (zrb_session_check_link(z)) {
        mrb_raisef(mrb, zrb_error_class(mrb), "put failed: the connection is lost (%d)", (int)ret);
    }
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "put failed (%d)", (int)ret);
    }
    return mrb_nil_value();
}

static mrb_value zrb_sub_new(mrb_state *mrb, mrb_value self, zrb_session *z, const char *key, mrb_int depth,
                             bool liveliness) {
    z_view_keyexpr_t ke;
    zrb_view_key(mrb, &ke, key);

    struct RClass *cls = zrb_class(mrb, liveliness ? "LivelinessWatch" : "Subscriber");
    struct RData *data = mrb_data_object_alloc(mrb, cls, NULL, &zrb_sub_type);
    zrb_sub *s = (zrb_sub *)mrb_malloc(mrb, sizeof(zrb_sub));
    memset(s, 0, sizeof(*s));
    data->data = s;
    mrb_value obj = mrb_obj_value(data);
    s->ring.slots = (zrb_entry *)mrb_malloc(mrb, sizeof(zrb_entry) * (size_t)depth);
    memset(s->ring.slots, 0, sizeof(zrb_entry) * (size_t)depth);
    s->ring.depth = (uint32_t)depth;
    s->liveliness = liveliness;

    z_owned_closure_sample_t cb;
    z_closure(&cb, zrb_on_sample, NULL, s);
    z_result_t ret;
    if (liveliness) {
        /* history: the tokens alive now are reported first, as appearing. */
        z_liveliness_subscriber_options_t opts;
        z_liveliness_subscriber_options_default(&opts);
        opts.history = true;
        ret = z_liveliness_declare_subscriber(z_loan(z->session), &s->sub, z_loan(ke), z_move(cb), &opts);
    } else {
        ret = z_declare_subscriber(z_loan(z->session), &s->sub, z_loan(ke), z_move(cb), NULL);
    }
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "cannot subscribe to %s (%d)", key, (int)ret);
    }
    s->declared = true;
    s->owner = z;
    s->next = z->subs;
    z->subs = s;
    zrb_hold_session(mrb, obj, self, key);
    return obj;
}

/* session.subscribe(key, depth = 16) -> Subscriber */
static mrb_value zrb_session_subscribe(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_int depth = ZRB_DEFAULT_DEPTH;
    mrb_get_args(mrb, "z|i", &key, &depth);
    zrb_check_depth(mrb, depth);
    zrb_session *z = zrb_session_get_open(mrb, self);
    return zrb_sub_new(mrb, self, z, key, depth, false);
}

/* session.liveliness_watch(key, depth = 16) -> LivelinessWatch. The tokens
 * alive when it starts come first, as appearing (alive = true). */
static mrb_value zrb_session_liveliness_watch(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_int depth = ZRB_DEFAULT_DEPTH;
    mrb_get_args(mrb, "z|i", &key, &depth);
    zrb_check_depth(mrb, depth);
    zrb_session *z = zrb_session_get_open(mrb, self);
    return zrb_sub_new(mrb, self, z, key, depth, true);
}

/* session.queryable(key, depth = 16) -> Queryable */
static mrb_value zrb_session_queryable(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_int depth = ZRB_DEFAULT_DEPTH;
    mrb_get_args(mrb, "z|i", &key, &depth);
    zrb_check_depth(mrb, depth);
    zrb_session *z = zrb_session_get_open(mrb, self);
    z_view_keyexpr_t ke;
    zrb_view_key(mrb, &ke, key);

    struct RData *data = mrb_data_object_alloc(mrb, zrb_class(mrb, "Queryable"), NULL, &zrb_qable_type);
    zrb_qable *q = (zrb_qable *)mrb_malloc(mrb, sizeof(zrb_qable));
    memset(q, 0, sizeof(*q));
    data->data = q;
    mrb_value obj = mrb_obj_value(data);
    q->slots = (z_owned_query_t *)mrb_malloc(mrb, sizeof(z_owned_query_t) * (size_t)depth);
    for (mrb_int i = 0; i < depth; i++) {
        z_internal_query_null(&q->slots[i]);
    }
    q->depth = (uint32_t)depth;

    z_owned_closure_query_t cb;
    z_closure(&cb, zrb_on_query, NULL, q);
    z_result_t ret = z_declare_queryable(z_loan(z->session), &q->qable, z_loan(ke), z_move(cb), NULL);
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "cannot declare a queryable on %s (%d)", key, (int)ret);
    }
    q->declared = true;
    q->owner = z;
    q->next = z->qables;
    z->qables = q;
    zrb_hold_session(mrb, obj, self, key);
    return obj;
}

/* session.get(key, timeout_ms = 2000, params = nil, payload = nil) -> Get.
 * Returns at once; the replies come in with later polls. Every matching
 * queryable is asked and every reply is kept (no consolidation). */
static mrb_value zrb_session_get_m(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_int timeout_ms = ZRB_DEFAULT_GET_TIMEOUT_MS;
    const char *params = NULL;
    mrb_value payload = mrb_nil_value();
    mrb_get_args(mrb, "z|iz!o", &key, &timeout_ms, &params, &payload);
    zrb_check_timeout(mrb, timeout_ms);
    if (!mrb_nil_p(payload) && !mrb_string_p(payload)) {
        mrb_raise(mrb, E_TYPE_ERROR, "payload must be a String");
    }
    zrb_session *z = zrb_session_get_open(mrb, self);
    z_view_keyexpr_t ke;
    zrb_view_key(mrb, &ke, key);

    zrb_get *g;
    mrb_value obj = zrb_get_new(mrb, &g);

    z_get_options_t opts;
    z_get_options_default(&opts);
    opts.timeout_ms = (uint64_t)timeout_ms;
    opts.target = Z_QUERY_TARGET_ALL;
    opts.consolidation = z_query_consolidation_none();
    z_owned_bytes_t bytes;
    if (!mrb_nil_p(payload)) {
        if (z_bytes_copy_from_buf(&bytes, (const uint8_t *)RSTRING_PTR(payload), (size_t)RSTRING_LEN(payload)) !=
            Z_OK) {
            mrb_raise(mrb, zrb_error_class(mrb), "cannot allocate the payload");
        }
        opts.payload = z_move(bytes);
    }
    z_owned_closure_reply_t cb;
    z_closure(&cb, zrb_on_reply, zrb_on_reply_drop, g);
    /* On failure zenoh-pico has already run the drop callback (done = true). */
    z_result_t ret = z_get(z_loan(z->session), z_loan(ke), params, z_move(cb), &opts);
    if (zrb_session_check_link(z)) {
        mrb_raisef(mrb, zrb_error_class(mrb), "get failed: the connection is lost (%d)", (int)ret);
    }
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "get failed (%d)", (int)ret);
    }
    zrb_hold_session(mrb, obj, self, key);
    return obj;
}

/* session.liveliness(key) -> LivelinessToken. Alive until closed (or
 * collected, or the session closes). */
static mrb_value zrb_session_liveliness(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_get_args(mrb, "z", &key);
    zrb_session *z = zrb_session_get_open(mrb, self);
    z_view_keyexpr_t ke;
    zrb_view_key(mrb, &ke, key);

    struct RData *data = mrb_data_object_alloc(mrb, zrb_class(mrb, "LivelinessToken"), NULL, &zrb_token_type);
    zrb_token *t = (zrb_token *)mrb_malloc(mrb, sizeof(zrb_token));
    memset(t, 0, sizeof(*t));
    data->data = t;
    mrb_value obj = mrb_obj_value(data);
    z_result_t ret = z_liveliness_declare_token(z_loan(z->session), &t->token, z_loan(ke), NULL);
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "cannot declare a liveliness token on %s (%d)", key, (int)ret);
    }
    t->declared = true;
    t->owner = z;
    t->next = z->tokens;
    z->tokens = t;
    zrb_hold_session(mrb, obj, self, key);
    return obj;
}

/* session.liveliness_get(key, timeout_ms = 2000) -> Get. Each reply is a
 * token alive now (key, empty payload). */
static mrb_value zrb_session_liveliness_get(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_int timeout_ms = ZRB_DEFAULT_GET_TIMEOUT_MS;
    mrb_get_args(mrb, "z|i", &key, &timeout_ms);
    zrb_check_timeout(mrb, timeout_ms);
    zrb_session *z = zrb_session_get_open(mrb, self);
    z_view_keyexpr_t ke;
    zrb_view_key(mrb, &ke, key);

    zrb_get *g;
    mrb_value obj = zrb_get_new(mrb, &g);
    z_liveliness_get_options_t opts;
    z_liveliness_get_options_default(&opts);
    opts.timeout_ms = (uint64_t)timeout_ms;
    z_owned_closure_reply_t cb;
    z_closure(&cb, zrb_on_reply, zrb_on_reply_drop, g);
    z_result_t ret = z_liveliness_get(z_loan(z->session), z_loan(ke), z_move(cb), &opts);
    if (zrb_session_check_link(z)) {
        mrb_raisef(mrb, zrb_error_class(mrb), "liveliness_get failed: the connection is lost (%d)", (int)ret);
    }
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "liveliness_get failed (%d)", (int)ret);
    }
    zrb_hold_session(mrb, obj, self, key);
    return obj;
}

/* session.poll(steps = 8) -> true while the session is open, false once closed
 * (by close, by zenoh-pico, or because the connection was lost).
 * Runs zenoh-pico's pending work (reading the sockets, accepting peers,
 * keep-alive, lease, query time limits) at most `steps` times, stopping
 * early when nothing is left. Does not wait for data; closing a lost session
 * may wait for the send limit at most. */
static mrb_value zrb_session_poll(mrb_state *mrb, mrb_value self) {
    mrb_int steps = ZRB_DEFAULT_POLL_STEPS;
    mrb_get_args(mrb, "|i", &steps);
    zrb_session *z = zrb_session_get(mrb, self);
    if (!z->open) {
        return mrb_false_value();
    }
    for (mrb_int i = 0; i < steps; i++) {
        if (z_session_is_closed(z_loan(z->session))) {
            break;
        }
        if (!zp_spin_once(z_loan(z->session))) {
            break;
        }
    }
    return mrb_bool_value(!zrb_session_check_link(z));
}

static mrb_value zrb_session_closed_p(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(zrb_session_check_link(zrb_session_get(mrb, self)));
}

static mrb_value zrb_session_close(mrb_state *mrb, mrb_value self) {
    zrb_session_shutdown(zrb_session_get(mrb, self));
    return mrb_nil_value();
}

/* session.peers -> Integer: connected peers (peer mode), or 1 / 0 for the
 * router of a client session. */
static mrb_value zrb_session_peers(mrb_state *mrb, mrb_value self) {
    zrb_session *z = zrb_session_get(mrb, self);
    if (zrb_session_check_link(z)) {
        return mrb_fixnum_value(0);
    }
    _z_session_t *s = _Z_RC_IN_VAL(z_loan(z->session));
    if (s->_tp._type != _Z_TRANSPORT_UNICAST_TYPE) {
        return mrb_fixnum_value(0);
    }
    return mrb_fixnum_value((mrb_int)_z_transport_peer_unicast_slist_len(s->_tp._transport._unicast._peers));
}

/* ------------------------------------------------------------------ init */

void mrb_picoruby_asterism_zenoh_gem_init(mrb_state *mrb) {
    /* Asterism::Zenoh. No top-level Zenoh is defined (doc/ruby_asterism/design.md). */
    struct RClass *asterism = mrb_define_module(mrb, "Asterism");
    struct RClass *mod = mrb_define_module_under(mrb, asterism, "Zenoh");
    mrb_define_class_under(mrb, mod, "Error", mrb->eStandardError_class);
    mrb_define_const(mrb, mod, "PICO_VERSION", mrb_str_new_cstr(mrb, ZENOH_PICO));
    mrb_define_const(mrb, mod, "CONNECT_TIMEOUT_MS", mrb_fixnum_value(PICORUBY_ZENOH_CONNECT_TIMEOUT_MS));
    mrb_define_const(mrb, mod, "SEND_TIMEOUT_MS", mrb_fixnum_value(PICORUBY_ZENOH_SEND_TIMEOUT_MS));
    mrb_define_const(mrb, mod, "PEER", mrb_bool_value(Z_FEATURE_UNICAST_PEER == 1));
    mrb_define_const(mrb, mod, "MAX_PEERS", mrb_fixnum_value(Z_LISTEN_MAX_CONNECTION_NB));

    struct RClass *ses = mrb_define_class_under(mrb, mod, "Session", mrb->object_class);
    MRB_SET_INSTANCE_TT(ses, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, ses, "new");
    mrb_define_class_method(mrb, ses, "open", zrb_session_s_open, MRB_ARGS_OPT(1) | MRB_ARGS_KEY(2, 0));
    mrb_define_method(mrb, ses, "put", zrb_session_put, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, ses, "subscribe", zrb_session_subscribe, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, ses, "get", zrb_session_get_m, MRB_ARGS_ARG(1, 3));
    mrb_define_method(mrb, ses, "queryable", zrb_session_queryable, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, ses, "liveliness", zrb_session_liveliness, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, ses, "liveliness_watch", zrb_session_liveliness_watch, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, ses, "liveliness_get", zrb_session_liveliness_get, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, ses, "poll", zrb_session_poll, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, ses, "peers", zrb_session_peers, MRB_ARGS_NONE());
    mrb_define_method(mrb, ses, "closed?", zrb_session_closed_p, MRB_ARGS_NONE());
    mrb_define_method(mrb, ses, "close", zrb_session_close, MRB_ARGS_NONE());

    /* Subscriber and LivelinessWatch share one C structure. */
    struct RClass *sub = mrb_define_class_under(mrb, mod, "Subscriber", mrb->object_class);
    struct RClass *watch = mrb_define_class_under(mrb, mod, "LivelinessWatch", mrb->object_class);
    struct RClass *both[2] = {sub, watch};
    for (int i = 0; i < 2; i++) {
        MRB_SET_INSTANCE_TT(both[i], MRB_TT_CDATA);
        mrb_undef_class_method(mrb, both[i], "new");
        mrb_define_method(mrb, both[i], "each_pending", zrb_sub_each_pending, MRB_ARGS_BLOCK());
        mrb_define_method(mrb, both[i], "pending", zrb_sub_pending, MRB_ARGS_NONE());
        mrb_define_method(mrb, both[i], "received", zrb_sub_received, MRB_ARGS_NONE());
        mrb_define_method(mrb, both[i], "dropped", zrb_sub_dropped, MRB_ARGS_NONE());
        mrb_define_method(mrb, both[i], "close", zrb_sub_close, MRB_ARGS_NONE());
        mrb_define_method(mrb, both[i], "closed?", zrb_sub_closed_p, MRB_ARGS_NONE());
    }

    struct RClass *qable = mrb_define_class_under(mrb, mod, "Queryable", mrb->object_class);
    MRB_SET_INSTANCE_TT(qable, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, qable, "new");
    mrb_define_method(mrb, qable, "each_pending", zrb_qable_each_pending, MRB_ARGS_BLOCK());
    mrb_define_method(mrb, qable, "pending", zrb_qable_pending, MRB_ARGS_NONE());
    mrb_define_method(mrb, qable, "received", zrb_qable_received, MRB_ARGS_NONE());
    mrb_define_method(mrb, qable, "dropped", zrb_qable_dropped, MRB_ARGS_NONE());
    mrb_define_method(mrb, qable, "close", zrb_qable_close, MRB_ARGS_NONE());
    mrb_define_method(mrb, qable, "closed?", zrb_qable_closed_p, MRB_ARGS_NONE());

    struct RClass *query = mrb_define_class_under(mrb, mod, "Query", mrb->object_class);
    MRB_SET_INSTANCE_TT(query, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, query, "new");
    mrb_define_method(mrb, query, "key", zrb_query_key, MRB_ARGS_NONE());
    mrb_define_method(mrb, query, "params", zrb_query_params, MRB_ARGS_NONE());
    mrb_define_method(mrb, query, "payload", zrb_query_payload, MRB_ARGS_NONE());
    mrb_define_method(mrb, query, "reply", zrb_query_reply, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, query, "finish", zrb_query_finish_m, MRB_ARGS_NONE());
    mrb_define_method(mrb, query, "finished?", zrb_query_finished_p, MRB_ARGS_NONE());

    struct RClass *get = mrb_define_class_under(mrb, mod, "Get", mrb->object_class);
    MRB_SET_INSTANCE_TT(get, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, get, "new");
    mrb_define_method(mrb, get, "each_reply", zrb_get_each_reply, MRB_ARGS_BLOCK());
    mrb_define_method(mrb, get, "done?", zrb_get_done_p, MRB_ARGS_NONE());
    mrb_define_method(mrb, get, "pending", zrb_get_pending, MRB_ARGS_NONE());
    mrb_define_method(mrb, get, "received", zrb_get_received, MRB_ARGS_NONE());
    mrb_define_method(mrb, get, "dropped", zrb_get_dropped, MRB_ARGS_NONE());
    mrb_define_method(mrb, get, "errors", zrb_get_errors, MRB_ARGS_NONE());

    struct RClass *tok = mrb_define_class_under(mrb, mod, "LivelinessToken", mrb->object_class);
    MRB_SET_INSTANCE_TT(tok, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, tok, "new");
    mrb_define_method(mrb, tok, "close", zrb_token_close, MRB_ARGS_NONE());
    mrb_define_method(mrb, tok, "closed?", zrb_token_closed_p, MRB_ARGS_NONE());
}

void mrb_picoruby_asterism_zenoh_gem_final(mrb_state *mrb) { (void)mrb; }
