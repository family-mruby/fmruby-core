#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "socket_server.h"
#include "graphics_handler.h"
#include "audio_handler.h"
#include "../../common/protocol.h"

static volatile int running = 1;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

void signal_handler(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    running = 0;
}

int init_sdl(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return -1;
    }

    window = SDL_CreateWindow(
        "Family mruby Host (SDL2)",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    printf("SDL2 initialized successfully\n");
    return 0;
}

void cleanup_sdl(void) {
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    SDL_Quit();
    printf("SDL2 cleaned up\n");
}

void handle_sdl_events(void) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                running = 0;
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                }
                break;

            default:
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    printf("Family mruby Host (SDL2) starting...\n");

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize SDL2
    if (init_sdl() < 0) {
        return 1;
    }

    // Initialize graphics handler
    if (graphics_handler_init(renderer) < 0) {
        fprintf(stderr, "Graphics handler initialization failed\n");
        cleanup_sdl();
        return 1;
    }

    // Initialize audio handler
    if (audio_handler_init() < 0) {
        fprintf(stderr, "Audio handler initialization failed\n");
        graphics_handler_cleanup();
        cleanup_sdl();
        return 1;
    }

    // Start socket server
    if (socket_server_start() < 0) {
        fprintf(stderr, "Socket server start failed\n");
        audio_handler_cleanup();
        graphics_handler_cleanup();
        cleanup_sdl();
        return 1;
    }

    printf("Host server running. Connect fmruby-core or press ESC to quit.\n");

    // Main loop
    while (running) {
        // Handle SDL events
        handle_sdl_events();

        // Process socket messages
        socket_server_process();

        // Small delay to prevent busy waiting
        SDL_Delay(16); // ~60 FPS
    }

    printf("Shutting down...\n");

    // Cleanup
    socket_server_stop();
    audio_handler_cleanup();
    graphics_handler_cleanup();
    cleanup_sdl();

    printf("Family mruby Host (SDL2) stopped.\n");
    return 0;
}