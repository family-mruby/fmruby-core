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
}
