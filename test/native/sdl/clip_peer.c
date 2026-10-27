/*
 * clip_peer.c - another SDL program on the same X server, for
 * test/native/sdl/sdl_clip_check.rb (doc/linux_native/ L12a): it stands for
 * a desktop application copying and pasting.
 *
 *   clip_peer get           print the clipboard's text
 *   clip_peer set TEXT SEC  own the clipboard with TEXT for SEC seconds
 *                           (serving it while it runs)
 *
 * A test program, not part of the product.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *w = SDL_CreateWindow("clip_peer", 0, 0, 10, 10, SDL_WINDOW_HIDDEN);
    (void)w;
    int rc = 0;
    if (strcmp(argv[1], "get") == 0) {
        char *t = SDL_GetClipboardText();
        printf("%s\n", t ? t : "");
        SDL_free(t);
    } else if (strcmp(argv[1], "set") == 0 && argc >= 4) {
        SDL_SetClipboardText(argv[2]);
        Uint32 end = SDL_GetTicks() + (Uint32)atoi(argv[3]) * 1000;
        SDL_Event e;
        while (SDL_GetTicks() < end) {
            while (SDL_PollEvent(&e)) {
            }
            SDL_Delay(10);
        }
    } else {
        rc = 2;
    }
    SDL_Quit();
    return rc;
}
