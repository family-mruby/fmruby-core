/*
 * Host test of the remote debugger's TCP transport
 * (main/drivers/debug/fmrb_debug_transport_tcp.c), the one the simulation and
 * the native program listen with.
 *
 *   - a request and its answer on one connection;
 *   - a client that closes and a next one that connects before the transport
 *     has polled again (a tool's ps and then its kill, each a process of its
 *     own): the next one is let in, after the transport has reported the old
 *     one gone, and is answered. It used to be turned away (closed at once),
 *     and the tool's write failed with a broken pipe;
 *   - one session at a time: a second client while one is connected is
 *     turned away, and the first goes on.
 *
 *   rake debug_tcp:test
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fmrb_debug_transport.h"

static const fmrb_debug_transport_ops_t *tp = &fmrb_debug_transport_tcp;
static int port;
static int failures;
static int checks;

static void expect(const char *what, long got, long want)
{
    checks++;
    if (got != want) {
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        failures++;
    }
}

static int dial(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        printf("FAIL connect: %s\n", strerror(errno));
        exit(1);
    }
    return fd;
}

// One frame: u32 BE length + body. Returns 0, or -errno.
static int put_frame(int fd, const char *body)
{
    uint32_t n = (uint32_t)strlen(body);
    uint8_t out[64] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
    memcpy(out + 4, body, n);
    if (send(fd, out, 4 + n, MSG_NOSIGNAL) < 0) return -errno;
    return 0;
}

// The answer's body length, or -errno / 0 when the peer closed.
static int get_frame(int fd, char *body, size_t cap)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };
    uint8_t buf[64];
    size_t got = 0;
    while (got < 4 || got < 4 + (size_t)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3])) {
        if (poll(&p, 1, 1000) <= 0) return -ETIMEDOUT;
        ssize_t r = recv(fd, buf + got, sizeof(buf) - got, 0);
        if (r < 0) return -errno;
        if (r == 0) return 0;
        got += (size_t)r;
    }
    size_t n = got - 4;
    if (n >= cap) n = cap - 1;
    memcpy(body, buf + 4, n);
    body[n] = '\0';
    return (int)n;
}

// Poll the transport until a frame comes, at most `tries` polls; whether the
// session was seen down on the way goes to *saw_down.
static int serve(uint8_t *buf, size_t cap, int tries, int *saw_down)
{
    for (int i = 0; i < tries; i++) {
        int r = tp->poll(buf, cap, 20);
        if (saw_down && !tp->connected()) *saw_down = 1;
        if (r > 0) return r;
    }
    return 0;
}

static void answer(const uint8_t *buf, int n)
{
    uint8_t out[64];
    memcpy(out, "re:", 3);
    memcpy(out + 3, buf, (size_t)n);
    expect("answer sent", tp->send(out, (size_t)n + 3), FMRB_OK);
}

int main(void)
{
    char env[16];
    port = 20000 + (int)(getpid() % 20000);
    snprintf(env, sizeof(env), "%d", port);
    setenv("FMRB_DEBUG_TCP_PORT", env, 1);
    if (tp->init() != FMRB_OK) {
        printf("FAIL init on port %d\n", port);
        return 1;
    }
    uint8_t buf[4096];
    char body[64];

    // A request and its answer.
    int a = dial();
    expect("a: request", put_frame(a, "ps"), 0);
    int n = serve(buf, sizeof(buf), 50, NULL);
    expect("a: request arrives", n, 2);
    expect("a: connected", tp->connected(), 1);
    answer(buf, n);
    expect("a: answer", get_frame(a, body, sizeof(body)), 5);
    expect("a: answer text", strcmp(body, "re:ps"), 0);

    // Closed, and the next one connects before the transport polls again.
    close(a);
    int b = dial();
    expect("b: request written", put_frame(b, "kill"), 0);
    usleep(20 * 1000);   // both the end and the new connection are waiting
    int down = 0;
    n = serve(buf, sizeof(buf), 50, &down);
    expect("b: the old session reported down first", down, 1);
    expect("b: request arrives", n, 4);
    answer(buf, n);
    expect("b: answered (not turned away)", get_frame(b, body, sizeof(body)), 7);
    expect("b: answer text", strcmp(body, "re:kill"), 0);

    // One session at a time: c is turned away, b goes on.
    int c = dial();
    for (int i = 0; i < 5; i++) tp->poll(buf, sizeof(buf), 20);
    expect("c: turned away", get_frame(c, body, sizeof(body)) <= 0, 1);
    expect("b: still connected", tp->connected(), 1);
    expect("b: second request", put_frame(b, "ps"), 0);
    n = serve(buf, sizeof(buf), 50, NULL);
    expect("b: second request arrives", n, 2);
    answer(buf, n);
    expect("b: second answer", get_frame(b, body, sizeof(body)), 5);
    close(c);
    close(b);

    if (failures) {
        printf("debug_tcp: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("debug_tcp: %d checks passed\n", checks);
    return 0;
}
