/*
 * picoruby-zenoh: a thin Ruby layer over zenoh-pico (client, put/subscribe).
 *
 * Threading model: zenoh-pico is built single-threaded, so nothing happens
 * behind the interpreter's back. The application calls Session#poll from its
 * own loop; poll runs zp_spin_once(), which reads the socket and may call the
 * subscriber callback. The callback does NOT touch the mruby VM: it copies the
 * key and payload into a bounded ring owned by the subscriber (buffers come
 * from zenoh-pico's allocator, z_malloc), and Subscriber#each_pending turns
 * them into Ruby strings later. When the ring is full the oldest entry is
 * dropped and counted.
 *
 * Lifetime: closing is optional. The Session and Subscriber objects close
 * their zenoh-pico counterparts when they are freed, in either order (an
 * interpreter shutdown frees objects in no particular order): a session keeps
 * a list of its live subscribers and undeclares them before it closes, and
 * detaches them so a later subscriber free does not touch the freed session.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include <zenoh-pico.h>

#define ZRB_DEFAULT_DEPTH 16
#define ZRB_MAX_DEPTH 1024
#define ZRB_DEFAULT_POLL_STEPS 8

typedef struct zrb_sub zrb_sub;

typedef struct {
    z_owned_session_t session;
    bool open;
    zrb_sub *subs; /* live subscribers declared on this session */
} zrb_session;

typedef struct {
    uint8_t *buf; /* key bytes followed by payload bytes (z_malloc) */
    size_t key_len;
    size_t payload_len;
} zrb_entry;

struct zrb_sub {
    z_owned_subscriber_t sub;
    bool declared;
    zrb_session *owner; /* NULL once detached (closed, or the session went away) */
    zrb_sub *next;
    zrb_entry *ring; /* depth entries (mrb_malloc) */
    uint32_t depth;
    uint32_t head;
    uint32_t count;
    uint32_t received;
    uint32_t dropped;
};

static void zrb_session_free(mrb_state *mrb, void *p);
static void zrb_sub_free(mrb_state *mrb, void *p);

static const struct mrb_data_type zrb_session_type = {"Zenoh::Session", zrb_session_free};
static const struct mrb_data_type zrb_sub_type = {"Zenoh::Subscriber", zrb_sub_free};

static struct RClass *zrb_error_class(mrb_state *mrb) {
    struct RClass *mod = mrb_module_get(mrb, "Zenoh");
    return mrb_class_get_under(mrb, mod, "Error");
}

/* ------------------------------------------------------------------ ring */

static void zrb_ring_clear(zrb_sub *s) {
    while (s->count > 0) {
        z_free(s->ring[s->head].buf);
        s->ring[s->head].buf = NULL;
        s->head = (s->head + 1) % s->depth;
        s->count--;
    }
    s->head = 0;
}

/* Called from inside zp_spin_once(). Must not call into the VM. */
static void zrb_on_sample(z_loaned_sample_t *sample, void *ctx) {
    zrb_sub *s = (zrb_sub *)ctx;
    if (s == NULL || s->ring == NULL) {
        return;
    }
    z_view_string_t ks;
    if (z_keyexpr_as_view_string(z_sample_keyexpr(sample), &ks) != Z_OK) {
        s->dropped++;
        return;
    }
    const char *kd = z_string_data(z_loan(ks));
    size_t kl = z_string_len(z_loan(ks));
    const z_loaned_bytes_t *pl = z_sample_payload(sample);
    size_t plen = z_bytes_len(pl);

    uint8_t *buf = (uint8_t *)z_malloc(kl + plen + 1);
    if (buf == NULL) {
        s->dropped++;
        return;
    }
    memcpy(buf, kd, kl);
    z_bytes_reader_t reader = z_bytes_get_reader(pl);
    size_t got = z_bytes_reader_read(&reader, buf + kl, plen);
    if (got != plen) {
        z_free(buf);
        s->dropped++;
        return;
    }

    if (s->count == s->depth) {
        /* Full: drop the oldest. */
        z_free(s->ring[s->head].buf);
        s->ring[s->head].buf = NULL;
        s->head = (s->head + 1) % s->depth;
        s->count--;
        s->dropped++;
    }
    uint32_t idx = (s->head + s->count) % s->depth;
    s->ring[idx].buf = buf;
    s->ring[idx].key_len = kl;
    s->ring[idx].payload_len = plen;
    s->count++;
    s->received++;
}

/* ------------------------------------------------------------ subscriber */

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
    if (s->ring != NULL) {
        zrb_ring_clear(s);
        mrb_free(mrb, s->ring);
    }
    mrb_free(mrb, s);
}

static zrb_sub *zrb_sub_get(mrb_state *mrb, mrb_value self) {
    zrb_sub *s = (zrb_sub *)mrb_data_get_ptr(mrb, self, &zrb_sub_type);
    if (s == NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Zenoh::Subscriber");
    }
    return s;
}

/* sub.each_pending { |key, payload| ... } -> Integer (values taken)
 * sub.each_pending                         -> Array of [key, payload] */
static mrb_value zrb_sub_each_pending(mrb_state *mrb, mrb_value self) {
    mrb_value blk = mrb_nil_value();
    mrb_get_args(mrb, "&", &blk);
    zrb_sub *s = zrb_sub_get(mrb, self);
    bool collect = mrb_nil_p(blk);
    mrb_value out = collect ? mrb_ary_new(mrb) : mrb_nil_value();
    mrb_int taken = 0;

    /* Only what is pending now: values that arrive while the block runs (it
     * may poll) are left for the next call. */
    uint32_t todo = s->count;
    while (todo > 0 && s->count > 0) {
        todo--;
        int ai = mrb_gc_arena_save(mrb);
        zrb_entry e = s->ring[s->head];
        s->ring[s->head].buf = NULL;
        s->head = (s->head + 1) % s->depth;
        s->count--;
        mrb_value pair[2];
        pair[0] = mrb_str_new(mrb, (const char *)e.buf, (mrb_int)e.key_len);
        pair[1] = mrb_str_new(mrb, (const char *)e.buf + e.key_len, (mrb_int)e.payload_len);
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

static mrb_value zrb_sub_pending(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_sub_get(mrb, self)->count);
}

static mrb_value zrb_sub_received(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_sub_get(mrb, self)->received);
}

static mrb_value zrb_sub_dropped(mrb_state *mrb, mrb_value self) {
    return mrb_fixnum_value((mrb_int)zrb_sub_get(mrb, self)->dropped);
}

static mrb_value zrb_sub_close(mrb_state *mrb, mrb_value self) {
    zrb_sub_detach(zrb_sub_get(mrb, self));
    return mrb_nil_value();
}

static mrb_value zrb_sub_closed_p(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(!zrb_sub_get(mrb, self)->declared);
}

/* --------------------------------------------------------------- session */

static void zrb_session_shutdown(zrb_session *z) {
    while (z->subs != NULL) {
        zrb_sub_detach(z->subs); /* unlinks itself from z->subs */
    }
    if (z->open) {
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
        mrb_raise(mrb, E_RUNTIME_ERROR, "uninitialized Zenoh::Session");
    }
    return z;
}

static zrb_session *zrb_session_get_open(mrb_state *mrb, mrb_value self) {
    zrb_session *z = zrb_session_get(mrb, self);
    if (!z->open || z_session_is_closed(z_loan(z->session))) {
        mrb_raise(mrb, zrb_error_class(mrb), "session is closed");
    }
    return z;
}

/* Zenoh::Session.open(locator) -> Session (client mode). Raises Zenoh::Error. */
static mrb_value zrb_session_s_open(mrb_state *mrb, mrb_value klass) {
    const char *locator;
    mrb_get_args(mrb, "z", &locator);

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
    if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, Z_CONFIG_MODE_CLIENT) != Z_OK ||
        zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator) != Z_OK) {
        z_drop(z_move(config));
        mrb_raise(mrb, zrb_error_class(mrb), "invalid locator");
    }
    z_result_t ret = z_open(&z->session, z_move(config), NULL);
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "cannot open a session to %s (%d)", locator, (int)ret);
    }
    z->open = true;
    return obj;
}

/* session.put(key, payload) -> nil */
static mrb_value zrb_session_put(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_value payload;
    mrb_get_args(mrb, "zS", &key, &payload);
    zrb_session *z = zrb_session_get_open(mrb, self);

    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, key) != Z_OK) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid key expression: %s", key);
    }
    z_owned_bytes_t bytes;
    if (z_bytes_copy_from_buf(&bytes, (const uint8_t *)RSTRING_PTR(payload), (size_t)RSTRING_LEN(payload)) != Z_OK) {
        mrb_raise(mrb, zrb_error_class(mrb), "cannot allocate the payload");
    }
    z_result_t ret = z_put(z_loan(z->session), z_loan(ke), z_move(bytes), NULL);
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "put failed (%d)", (int)ret);
    }
    return mrb_nil_value();
}

/* session.subscribe(key, depth = 16) -> Subscriber */
static mrb_value zrb_session_subscribe(mrb_state *mrb, mrb_value self) {
    const char *key;
    mrb_int depth = ZRB_DEFAULT_DEPTH;
    mrb_get_args(mrb, "z|i", &key, &depth);
    if (depth < 1 || depth > ZRB_MAX_DEPTH) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "depth must be 1..%d", ZRB_MAX_DEPTH);
    }
    zrb_session *z = zrb_session_get_open(mrb, self);

    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, key) != Z_OK) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid key expression: %s", key);
    }

    struct RClass *mod = mrb_module_get(mrb, "Zenoh");
    struct RClass *cls = mrb_class_get_under(mrb, mod, "Subscriber");
    struct RData *data = mrb_data_object_alloc(mrb, cls, NULL, &zrb_sub_type);
    zrb_sub *s = (zrb_sub *)mrb_malloc(mrb, sizeof(zrb_sub));
    memset(s, 0, sizeof(*s));
    data->data = s;
    mrb_value obj = mrb_obj_value(data);
    s->ring = (zrb_entry *)mrb_malloc(mrb, sizeof(zrb_entry) * (size_t)depth);
    memset(s->ring, 0, sizeof(zrb_entry) * (size_t)depth);
    s->depth = (uint32_t)depth;

    z_owned_closure_sample_t cb;
    z_closure(&cb, zrb_on_sample, NULL, s);
    z_result_t ret = z_declare_subscriber(z_loan(z->session), &s->sub, z_loan(ke), z_move(cb), NULL);
    if (ret != Z_OK) {
        mrb_raisef(mrb, zrb_error_class(mrb), "cannot subscribe to %s (%d)", key, (int)ret);
    }
    s->declared = true;
    s->owner = z;
    s->next = z->subs;
    z->subs = s;
    /* Keep the session object alive while the subscriber is reachable. */
    mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "@session"), self);
    mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "@key"), mrb_str_new_cstr(mrb, key));
    return obj;
}

/* session.poll(steps = 8) -> true while the session is open, false once closed.
 * Runs zenoh-pico's pending work (reading the socket, keep-alive, lease) at
 * most `steps` times, stopping early when nothing is left. Never blocks. */
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
    return mrb_bool_value(!z_session_is_closed(z_loan(z->session)));
}

static mrb_value zrb_session_closed_p(mrb_state *mrb, mrb_value self) {
    zrb_session *z = zrb_session_get(mrb, self);
    return mrb_bool_value(!z->open || z_session_is_closed(z_loan(z->session)));
}

static mrb_value zrb_session_close(mrb_state *mrb, mrb_value self) {
    zrb_session_shutdown(zrb_session_get(mrb, self));
    return mrb_nil_value();
}

/* ------------------------------------------------------------------ init */

void mrb_picoruby_zenoh_gem_init(mrb_state *mrb) {
    struct RClass *mod = mrb_define_module(mrb, "Zenoh");
    mrb_define_class_under(mrb, mod, "Error", mrb->eStandardError_class);
    mrb_define_const(mrb, mod, "PICO_VERSION", mrb_str_new_cstr(mrb, ZENOH_PICO));

    struct RClass *ses = mrb_define_class_under(mrb, mod, "Session", mrb->object_class);
    MRB_SET_INSTANCE_TT(ses, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, ses, "new");
    mrb_define_class_method(mrb, ses, "open", zrb_session_s_open, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, ses, "put", zrb_session_put, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, ses, "subscribe", zrb_session_subscribe, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, ses, "poll", zrb_session_poll, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, ses, "closed?", zrb_session_closed_p, MRB_ARGS_NONE());
    mrb_define_method(mrb, ses, "close", zrb_session_close, MRB_ARGS_NONE());

    struct RClass *sub = mrb_define_class_under(mrb, mod, "Subscriber", mrb->object_class);
    MRB_SET_INSTANCE_TT(sub, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, sub, "new");
    mrb_define_method(mrb, sub, "each_pending", zrb_sub_each_pending, MRB_ARGS_BLOCK());
    mrb_define_method(mrb, sub, "pending", zrb_sub_pending, MRB_ARGS_NONE());
    mrb_define_method(mrb, sub, "received", zrb_sub_received, MRB_ARGS_NONE());
    mrb_define_method(mrb, sub, "dropped", zrb_sub_dropped, MRB_ARGS_NONE());
    mrb_define_method(mrb, sub, "close", zrb_sub_close, MRB_ARGS_NONE());
    mrb_define_method(mrb, sub, "closed?", zrb_sub_closed_p, MRB_ARGS_NONE());
}

void mrb_picoruby_zenoh_gem_final(mrb_state *mrb) { (void)mrb; }
