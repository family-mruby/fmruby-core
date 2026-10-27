/*
 * comp_link.c - see comp_link.h.
 */

#include "comp_link.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define COMP_CMDS 32

static struct {
    pthread_mutex_t lock;
    comp_route_t *route;
    int wake_fd;
    int scale, base_w, base_h;
    comp_places_t places;
    unsigned places_seq, places_taken;
    struct {
        comp_cmd_kind_t kind;
        int id;
    } cmd[COMP_CMDS];
    unsigned cmd_wr, cmd_rd;
} s_link = { .lock = PTHREAD_MUTEX_INITIALIZER, .wake_fd = -1 };

static void wake(int fd)
{
    if (fd < 0) return;
    uint64_t one = 1;
    ssize_t n = write(fd, &one, sizeof(one));
    (void)n;
}

void comp_link_attach(comp_route_t *r, int wake_fd)
{
    pthread_mutex_lock(&s_link.lock);
    s_link.route = r;
    s_link.wake_fd = wake_fd;
    pthread_mutex_unlock(&s_link.lock);
}

void comp_link_detach(void)
{
    pthread_mutex_lock(&s_link.lock);
    s_link.route = NULL;
    s_link.wake_fd = -1;
    pthread_mutex_unlock(&s_link.lock);
}

bool comp_link_attached(void)
{
    pthread_mutex_lock(&s_link.lock);
    bool a = s_link.route != NULL;
    pthread_mutex_unlock(&s_link.lock);
    return a;
}

void comp_link_set_geometry(int scale, int base_w, int base_h)
{
    pthread_mutex_lock(&s_link.lock);
    s_link.scale = scale;
    s_link.base_w = base_w;
    s_link.base_h = base_h;
    pthread_mutex_unlock(&s_link.lock);
}

bool comp_link_geometry(int *scale, int *base_w, int *base_h)
{
    pthread_mutex_lock(&s_link.lock);
    *scale = s_link.scale;
    *base_w = s_link.base_w;
    *base_h = s_link.base_h;
    pthread_mutex_unlock(&s_link.lock);
    return *scale > 0 && *base_w > 0 && *base_h > 0;
}

bool comp_link_take_notice(comp_notice_t *out)
{
    /* The route's queue has its own lock; the route itself lives as long as
     * the program (the compositor never frees it), so no lock is held here
     * across the take. */
    pthread_mutex_lock(&s_link.lock);
    comp_route_t *r = s_link.route;
    pthread_mutex_unlock(&s_link.lock);
    return r != NULL && comp_route_take_notice(r, out);
}

void comp_link_place(const comp_places_t *pl)
{
    pthread_mutex_lock(&s_link.lock);
    bool same = s_link.places.n == pl->n && s_link.places.focus == pl->focus &&
                memcmp(s_link.places.p, pl->p, sizeof(pl->p[0]) * (size_t)pl->n) == 0;
    int fd = -1;
    if (!same) {
        s_link.places = *pl;
        s_link.places_seq++;
        fd = s_link.wake_fd;
    }
    pthread_mutex_unlock(&s_link.lock);
    wake(fd);
}

bool comp_link_take_places(comp_places_t *out)
{
    pthread_mutex_lock(&s_link.lock);
    bool got = s_link.places_taken != s_link.places_seq;
    if (got) {
        *out = s_link.places;
        s_link.places_taken = s_link.places_seq;
    }
    pthread_mutex_unlock(&s_link.lock);
    return got;
}

void comp_link_command(comp_cmd_kind_t kind, int id)
{
    pthread_mutex_lock(&s_link.lock);
    if (s_link.cmd_wr - s_link.cmd_rd >= COMP_CMDS) s_link.cmd_rd++;
    s_link.cmd[s_link.cmd_wr % COMP_CMDS].kind = kind;
    s_link.cmd[s_link.cmd_wr % COMP_CMDS].id = id;
    s_link.cmd_wr++;
    int fd = s_link.wake_fd;
    pthread_mutex_unlock(&s_link.lock);
    wake(fd);
}

bool comp_link_take_command(comp_cmd_kind_t *kind, int *id)
{
    pthread_mutex_lock(&s_link.lock);
    bool got = s_link.cmd_rd != s_link.cmd_wr;
    if (got) {
        *kind = s_link.cmd[s_link.cmd_rd % COMP_CMDS].kind;
        *id = s_link.cmd[s_link.cmd_rd % COMP_CMDS].id;
        s_link.cmd_rd++;
    }
    pthread_mutex_unlock(&s_link.lock);
    return got;
}
