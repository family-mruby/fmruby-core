// Driver for the kana composer test (c_kana.rb): main/drivers/usb/fmrb_kana.c
// built with the host cc. Each input line is "<mode> <keys>" (mode 1
// hiragana, 2 katakana; keys are typed one by one, '<' is Backspace, '!' is
// Esc, then a space ends the line); the answer is what an app would see:
// the composed bytes and the keys passed on ('_' for the space, '!' for Esc,
// a Backspace passed on erases the character before it). "ALL <n>" prints
// "<romaji>\t<text>" for every string of a-z and '-' up to n letters.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fmrb_kana.h"
#include "fmrb_keymap.h"

fmrb_keymap_layout_t fmrb_keymap_get_layout(void)
{
    return FMRB_KEYMAP_LAYOUT_JP;
}

static void run(int mode, const char *s, size_t n, char *buf)
{
    fmrb_kana_set_mode((fmrb_kana_mode_t)mode);
    size_t len = 0;
    for (size_t i = 0; i <= n; i++) {
        char c = i < n ? s[i] : ' ';
        uint8_t sc = 0x04;
        char a = c;
        if (c == ' ') sc = 0x2C;
        else if (c == '<') { sc = 0x2A; a = 0x08; }
        else if (c == '!') { sc = 0x29; a = 0x1B; }
        fmrb_kana_result_t r;
        fmrb_kana_action_t act = fmrb_kana_feed(true, sc, 0, a, &r);
        memcpy(buf + len, r.out, r.out_len);
        len += r.out_len;
        if (act == FMRB_KANA_PASS) {
            if (c == '<') {
                while (len > 0) {
                    len--;
                    if (((unsigned char)buf[len] & 0xC0) != 0x80) break;
                }
            } else {
                buf[len++] = (c == ' ') ? '_' : c;
            }
        }
    }
    buf[len] = '\0';
}

int main(void)
{
    char line[256], out[1024];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "ALL ", 4) == 0) {
            const char *alpha = "abcdefghijklmnopqrstuvwxyz-";
            int a = (int)strlen(alpha), max = atoi(line + 4);
            int idx[8];
            char s[8];
            for (int n = 1; n <= max && n <= 8; n++) {
                memset(idx, 0, sizeof(idx));
                for (;;) {
                    for (int i = 0; i < n; i++) s[i] = alpha[idx[i]];
                    run(1, s, (size_t)n, out);
                    printf("%.*s\t%s\n", n, s, out);
                    int k = n - 1;
                    while (k >= 0 && ++idx[k] == a) idx[k--] = 0;
                    if (k < 0) break;
                }
            }
            continue;
        }
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        run(atoi(line), sp + 1, strlen(sp + 1), out);
        printf("%s\n", out);
    }
    return 0;
}
