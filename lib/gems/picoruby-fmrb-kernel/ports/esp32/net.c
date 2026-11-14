// FmrbNet: read-only network status API for Ruby.
//
// WiFi bring-up and reconnection are owned by the C-side wifi_task
// (main/drivers/wifi); Ruby can only observe the state. On the Linux dev
// build the host network is always available, so the module reports a
// connected loopback. On targets without WiFi (ATOM) every query reports
// "not connected".

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/class.h>

#include <sdkconfig.h>

#if defined(FMRB_HAS_WIFI) && !defined(CONFIG_IDF_TARGET_LINUX)
#include "wifi_task.h"
#define FMRB_NET_HAVE_WIFI 1
#endif

#if defined(__EMSCRIPTEN__)
#include <stdlib.h>
#include <emscripten.h>
#include <mruby/error.h>

/*
 * The browser half of FmrbNet.request.
 *
 * Everything here runs on the PAGE's thread, not the caller's. Each FreeRTOS
 * task in the wasm port is a pthread, which is a Worker, and a Worker that is
 * parked in emscripten_thread_sleep runs no JavaScript at all -- a fetch()
 * started there would never get to resolve its promise. The page's own thread
 * keeps its event loop, so the request is started, polled and collected there
 * with MAIN_THREAD_EM_ASM. The file syscalls already work this way
 * (proxyToMainThread in core_web.js), so the machine is used to it.
 *
 * The table lives on globalThis rather than Module: every Worker has its own
 * Module, and this one has to be the page's.
 */

/*
 * MAIN_THREAD_EM_ASM is a C macro, so these are plain C functions. Putting one
 * inside EM_JS emits its braces into JavaScript, where a block that starts
 * with `if` is read as an object literal and the build stops at acorn.
 *
 * The JavaScript is ES5-shaped for the same reason: async/await, ||= and ?.
 * did not survive that trip either. And every object literal is wrapped in
 * parentheses, because braces do not hide a comma from the C preprocessor --
 * without them the macro sees several arguments where one was meant.
 */

static int fmrb_fetch_start_js(const char *url)
{
    return MAIN_THREAD_EM_ASM_INT({
        if (!globalThis.__fmrbFetch) { globalThis.__fmrbFetch = ({ next: 1, tab: new Map() }); }
        var t = globalThis.__fmrbFetch;
        var id = t.next++;
        var rec = ({ state: 0, status: 0, body: null, err: "" });
        t.tab.set(id, rec);
        fetch(UTF8ToString($0), { cache: "no-store" }).then(function (r) {
            rec.status = r.status;
            return r.arrayBuffer();
        }).then(function (b) {
            rec.body = new Uint8Array(b);
            rec.state = 1;
        })["catch"](function (e) {
            rec.err = String(e && e.message ? e.message : e);
            rec.state = 2;
        });
        return id;
    }, url);
}

/* 0 pending, 1 done, 2 failed, 3 no such request */
static int fmrb_fetch_poll_js(int id)
{
    return MAIN_THREAD_EM_ASM_INT({
        var t = globalThis.__fmrbFetch;
        var rec = t ? t.tab.get($0) : null;
        return rec ? rec.state : 3;
    }, id);
}

static int fmrb_fetch_status_js(int id)
{
    return MAIN_THREAD_EM_ASM_INT({
        var t = globalThis.__fmrbFetch;
        var rec = t ? t.tab.get($0) : null;
        return rec ? rec.status : 0;
    }, id);
}

static int fmrb_fetch_body_len_js(int id)
{
    return MAIN_THREAD_EM_ASM_INT({
        var t = globalThis.__fmrbFetch;
        var rec = t ? t.tab.get($0) : null;
        return (rec && rec.body) ? rec.body.length : 0;
    }, id);
}

/* The page writes into the shared heap, which is why the buffer is allocated
   on the C side and passed in. */
static void fmrb_fetch_body_copy_js(int id, char *dst, int len)
{
    MAIN_THREAD_EM_ASM({
        var t = globalThis.__fmrbFetch;
        var rec = t ? t.tab.get($0) : null;
        if (rec && rec.body) { HEAPU8.set(rec.body.subarray(0, $2), $1); }
    }, id, dst, len);
}

static int fmrb_fetch_err_len_js(int id)
{
    return MAIN_THREAD_EM_ASM_INT({
        var t = globalThis.__fmrbFetch;
        var rec = t ? t.tab.get($0) : null;
        return lengthBytesUTF8((rec && rec.err) ? rec.err : "");
    }, id);
}

static void fmrb_fetch_err_copy_js(int id, char *dst, int len)
{
    MAIN_THREAD_EM_ASM({
        var t = globalThis.__fmrbFetch;
        var rec = t ? t.tab.get($0) : null;
        stringToUTF8((rec && rec.err) ? rec.err : "", $1, $2 + 1);
    }, id, dst, len);
}

static void fmrb_fetch_free_js(int id)
{
    MAIN_THREAD_EM_ASM({
        var t = globalThis.__fmrbFetch;
        if (t) { t.tab["delete"]($0); }
    }, id);
}

// FmrbNet._fetch_start(url) -> Integer id, or nil when the page refused it
static mrb_value mrb_fmrb_net_fetch_start(mrb_state *mrb, mrb_value self)
{
    const char *url;
    mrb_get_args(mrb, "z", &url);
    int id = fmrb_fetch_start_js(url);
    return id > 0 ? mrb_fixnum_value(id) : mrb_nil_value();
}

// FmrbNet._fetch_poll(id) -> 0 pending, 1 done, 2 failed, 3 unknown
static mrb_value mrb_fmrb_net_fetch_poll(mrb_state *mrb, mrb_value self)
{
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    return mrb_fixnum_value(fmrb_fetch_poll_js((int)id));
}

static mrb_value mrb_fmrb_net_fetch_status(mrb_state *mrb, mrb_value self)
{
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    return mrb_fixnum_value(fmrb_fetch_status_js((int)id));
}

// Shared by the body and the error message: ask the page how long it is,
// borrow that much, have the page fill it in, hand it to Ruby.
static mrb_value fetch_take_string(mrb_state *mrb, int id, int is_body)
{
    int len = is_body ? fmrb_fetch_body_len_js(id) : fmrb_fetch_err_len_js(id);
    if (len <= 0) {
        return mrb_str_new_cstr(mrb, "");
    }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "out of memory reading the response");
    }
    if (is_body) {
        fmrb_fetch_body_copy_js(id, buf, len);
    } else {
        fmrb_fetch_err_copy_js(id, buf, len);
    }
    buf[len] = '\0';
    mrb_value out = mrb_str_new(mrb, buf, len);
    free(buf);
    return out;
}

static mrb_value mrb_fmrb_net_fetch_body(mrb_state *mrb, mrb_value self)
{
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    return fetch_take_string(mrb, (int)id, 1);
}

static mrb_value mrb_fmrb_net_fetch_error(mrb_state *mrb, mrb_value self)
{
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    return fetch_take_string(mrb, (int)id, 0);
}

static mrb_value mrb_fmrb_net_fetch_free(mrb_state *mrb, mrb_value self)
{
    mrb_int id;
    mrb_get_args(mrb, "i", &id);
    fmrb_fetch_free_js((int)id);
    return mrb_nil_value();
}
#endif /* __EMSCRIPTEN__ */

// FmrbNet.connected? -> bool
static mrb_value mrb_fmrb_net_connected_p(mrb_state *mrb, mrb_value self)
{
#if defined(CONFIG_IDF_TARGET_LINUX)
    return mrb_true_value();
#elif defined(FMRB_NET_HAVE_WIFI)
    return mrb_bool_value(wifi_is_connected());
#else
    return mrb_false_value();
#endif
}

// FmrbNet.ip_address -> String ("0.0.0.0" when not connected)
static mrb_value mrb_fmrb_net_ip_address(mrb_state *mrb, mrb_value self)
{
#if defined(CONFIG_IDF_TARGET_LINUX)
    return mrb_str_new_cstr(mrb, "127.0.0.1");
#elif defined(FMRB_NET_HAVE_WIFI)
    char buf[16];
    wifi_get_ip_str(buf, sizeof(buf));
    return mrb_str_new_cstr(mrb, buf);
#else
    return mrb_str_new_cstr(mrb, "0.0.0.0");
#endif
}

// FmrbNet.hostname -> String
static mrb_value mrb_fmrb_net_hostname(mrb_state *mrb, mrb_value self)
{
#if defined(CONFIG_IDF_TARGET_LINUX)
    return mrb_str_new_cstr(mrb, "localhost");
#elif defined(FMRB_NET_HAVE_WIFI)
    char buf[32];
    wifi_get_hostname(buf, sizeof(buf));
    return mrb_str_new_cstr(mrb, buf);
#else
    return mrb_str_new_cstr(mrb, "");
#endif
}

// FmrbNet.ssid -> String (empty when unconfigured)
static mrb_value mrb_fmrb_net_ssid(mrb_state *mrb, mrb_value self)
{
#if defined(FMRB_NET_HAVE_WIFI)
    char buf[33];
    wifi_get_ssid(buf, sizeof(buf));
    return mrb_str_new_cstr(mrb, buf);
#else
    return mrb_str_new_cstr(mrb, "");
#endif
}

// FmrbNet.wait_for_ip(timeout_ms = 10000) -> bool
// Blocks the calling VM task up to timeout_ms; use short timeouts from
// interactive apps.
static mrb_value mrb_fmrb_net_wait_for_ip(mrb_state *mrb, mrb_value self)
{
    mrb_int timeout_ms = 10000;
    mrb_get_args(mrb, "|i", &timeout_ms);
    if (timeout_ms < 0) timeout_ms = 0;

#if defined(CONFIG_IDF_TARGET_LINUX)
    return mrb_true_value();
#elif defined(FMRB_NET_HAVE_WIFI)
    return mrb_bool_value(wifi_wait_for_ip((uint32_t)timeout_ms));
#else
    return mrb_false_value();
#endif
}

void mrb_fmrb_net_init(mrb_state *mrb)
{
    struct RClass *net_module = mrb_define_module(mrb, "FmrbNet");
    mrb_define_module_function(mrb, net_module, "connected?", mrb_fmrb_net_connected_p, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, net_module, "ip_address", mrb_fmrb_net_ip_address, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, net_module, "hostname", mrb_fmrb_net_hostname, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, net_module, "ssid", mrb_fmrb_net_ssid, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, net_module, "wait_for_ip", mrb_fmrb_net_wait_for_ip, MRB_ARGS_OPT(1));

#if defined(__EMSCRIPTEN__)
    /* The browser's half of FmrbNet.request. Ruby calls these only when
       FmrbConst::BOARD is "wasm"; everywhere else Request uses Net::HTTP. */
    mrb_define_module_function(mrb, net_module, "_fetch_start", mrb_fmrb_net_fetch_start, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, net_module, "_fetch_poll", mrb_fmrb_net_fetch_poll, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, net_module, "_fetch_status", mrb_fmrb_net_fetch_status, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, net_module, "_fetch_body", mrb_fmrb_net_fetch_body, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, net_module, "_fetch_error", mrb_fmrb_net_fetch_error, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, net_module, "_fetch_free", mrb_fmrb_net_fetch_free, MRB_ARGS_REQ(1));
#endif
}
