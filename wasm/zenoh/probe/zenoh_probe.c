/*
 * zenoh_probe.c - the smallest Zenoh client in a web page (doc/ruby_asterism,
 * B1): zenoh-pico over the WebSocket link of wasm/zenoh/, no Family mruby.
 *
 * Connects to the router given as the first argument (the page passes
 * ?zenoh=ws/<host>:<port>, default ws/localhost:7449), then
 *  - puts "browser <n>" on b1/probe/from_browser once a second,
 *  - subscribes b1/probe/to_browser and answers each sample on b1/probe/echo
 *    with "echo <payload>",
 * for the number of seconds given as the second argument (default 30), and
 * prints what it does (the page shows stdout).
 *
 * Built by `rake wasm:zenoh_probe`; its PC side is peer.rb next to it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "zenoh-pico.h"

static int s_received;
static char s_pending[256];
static int s_has_pending;

static void on_sample(z_loaned_sample_t *sample, void *ctx) {
    (void)ctx;
    z_owned_string_t s;
    if (z_bytes_to_string(z_sample_payload(sample), &s) != Z_OK) {
        return;
    }
    size_t n = z_string_len(z_loan(s));
    if (n > sizeof(s_pending) - 8) {
        n = sizeof(s_pending) - 8;
    }
    memcpy(s_pending, "echo ", 5);
    memcpy(s_pending + 5, z_string_data(z_loan(s)), n);
    s_pending[5 + n] = '\0';
    s_has_pending = 1;
    s_received++;
    printf("probe: received '%s'\n", s_pending + 5);
    z_drop(z_move(s));
}

static int put_str(const z_loaned_session_t *zs, const char *key, const char *text) {
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str(&ke, key);
    z_owned_bytes_t b;
    z_bytes_copy_from_str(&b, text);
    return z_put(zs, z_loan(ke), z_move(b), NULL);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    const char *locator = argc > 1 ? argv[1] : "ws/localhost:7449";
    int seconds = argc > 2 ? atoi(argv[2]) : 30;

    z_owned_config_t config;
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client");
    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator);

    printf("probe: opening %s\n", locator);
    double t0 = now_ms();
    z_owned_session_t session;
    z_result_t ret = z_open(&session, z_move(config), NULL);
    if (ret != Z_OK) {
        printf("probe: open failed (%d)\n", ret);
        return 1;
    }
    printf("probe: open in %.1f ms\n", now_ms() - t0);

    z_owned_closure_sample_t cb;
    z_closure(&cb, on_sample, NULL, NULL);
    z_owned_subscriber_t sub;
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str(&ke, "b1/probe/to_browser");
    if (z_declare_subscriber(z_loan(session), &sub, z_loan(ke), z_move(cb), NULL) != Z_OK) {
        printf("probe: subscribe failed\n");
        return 1;
    }

    double next_put = now_ms();
    int n = 0;
    /* How long one poll (8 spins) takes: the cost of reading the link. */
    double poll_total = 0, poll_max = 0;
    long polls = 0;
    double end = now_ms() + seconds * 1000.0;
    while (now_ms() < end && !z_session_is_closed(z_loan(session))) {
        double p0 = now_ms();
        for (int i = 0; i < 8; i++) {
            if (!zp_spin_once(z_loan(session))) {
                break;
            }
        }
        double pd = now_ms() - p0;
        poll_total += pd;
        if (pd > poll_max) {
            poll_max = pd;
        }
        polls++;
        if (s_has_pending) {
            s_has_pending = 0;
            put_str(z_loan(session), "b1/probe/echo", s_pending);
        }
        if (now_ms() >= next_put) {
            char text[32];
            snprintf(text, sizeof(text), "browser %d", ++n);
            ret = put_str(z_loan(session), "b1/probe/from_browser", text);
            printf("probe: put '%s' (%d)\n", text, ret);
            next_put += 1000.0;
        }
        usleep(10 * 1000);
    }
    printf("probe: done, received %d; poll (8 spins) mean %.3f ms, max %.3f ms over %ld\n", s_received,
           polls ? poll_total / polls : 0.0, poll_max, polls);
    z_drop(z_move(sub));
    z_drop(z_move(session));
    return 0;
}
