/*
 * native_clip.c - see native_clip.h.
 */

#include "native_clip.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "fmrb_clipboard.h"

static struct {
    pthread_mutex_t lock;
    void (*wake)(void);
    bool installed;
    char out[FMRB_CLIPBOARD_MAX];
    size_t out_len;
    unsigned out_seq, out_taken;
    char in[FMRB_CLIPBOARD_MAX];
    size_t in_len;
    uint32_t in_seq;
    native_clip_stats_t stats;
} s_clip = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* The kernel's side (a task, under the kernel clipboard's lock). */
static void host_publish(const char *text, size_t len)
{
    if (len > FMRB_CLIPBOARD_MAX) return;
    pthread_mutex_lock(&s_clip.lock);
    if (len > 0) memcpy(s_clip.out, text, len);
    s_clip.out_len = len;
    s_clip.out_seq++;
    s_clip.stats.out++;
    s_clip.stats.last_out = len;
    void (*wake)(void) = s_clip.wake;
    pthread_mutex_unlock(&s_clip.lock);
    if (wake) wake();
}

static bool host_take(uint32_t *seq, char *buf, size_t cap, size_t *len)
{
    pthread_mutex_lock(&s_clip.lock);
    bool got = s_clip.in_seq != *seq;
    if (got) {
        size_t n = s_clip.in_len;
        if (n <= cap && n <= FMRB_CLIPBOARD_MAX) memcpy(buf, s_clip.in, n);
        *len = n;
        *seq = s_clip.in_seq;
        s_clip.stats.taken++;
    }
    pthread_mutex_unlock(&s_clip.lock);
    return got;
}

static const fmrb_clipboard_host_t s_host = {
    .publish = host_publish,
    .take = host_take,
};

void native_clip_install(void (*wake)(void))
{
    pthread_mutex_lock(&s_clip.lock);
    s_clip.wake = wake;
    s_clip.installed = true;
    pthread_mutex_unlock(&s_clip.lock);
    fmrb_clipboard_host_install(&s_host);
}

bool native_clip_installed(void)
{
    pthread_mutex_lock(&s_clip.lock);
    bool i = s_clip.installed;
    pthread_mutex_unlock(&s_clip.lock);
    return i;
}

bool native_clip_take_out(char **text, size_t *len)
{
    pthread_mutex_lock(&s_clip.lock);
    if (s_clip.out_seq == s_clip.out_taken) {
        pthread_mutex_unlock(&s_clip.lock);
        return false;
    }
    char *copy = (char *)malloc(s_clip.out_len + 1);
    if (copy == NULL) {
        pthread_mutex_unlock(&s_clip.lock);
        return false;
    }
    memcpy(copy, s_clip.out, s_clip.out_len);
    copy[s_clip.out_len] = '\0';
    *len = s_clip.out_len;
    s_clip.out_taken = s_clip.out_seq;
    pthread_mutex_unlock(&s_clip.lock);
    *text = copy;
    return true;
}

void native_clip_offer(const char *text, size_t len)
{
    pthread_mutex_lock(&s_clip.lock);
    if (len <= FMRB_CLIPBOARD_MAX && len > 0 && text != NULL) memcpy(s_clip.in, text, len);
    s_clip.in_len = (text == NULL && len <= FMRB_CLIPBOARD_MAX) ? 0 : len;
    s_clip.in_seq++;
    s_clip.stats.in++;
    if (len > FMRB_CLIPBOARD_MAX) s_clip.stats.in_big++;
    s_clip.stats.last_in = len;
    pthread_mutex_unlock(&s_clip.lock);
}

void native_clip_stats(native_clip_stats_t *out)
{
    pthread_mutex_lock(&s_clip.lock);
    *out = s_clip.stats;
    pthread_mutex_unlock(&s_clip.lock);
}
