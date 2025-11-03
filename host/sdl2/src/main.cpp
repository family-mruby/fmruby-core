#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>

extern "C" {
#include "socket_server.h"
#include "graphics_handler.h"
#include "audio_handler.h"
#include "../../common/protocol.h"
#include "../../common/graphics_commands.h"
}

static volatile int running = 1;
LGFX* g_lgfx = nullptr; // Global LGFX instance (not static, shared with graphics_handler.cpp)

extern "C" void signal_handler(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    running = 0;

    // Post SDL_QUIT event to stop LovyanGFX event loop
    SDL_Event quit_event;
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);
}

// User function that runs in a separate thread
int user_func(bool* thread_running) {
    printf("Family mruby Host (SDL2 + LovyanGFX) starting...\n");

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create LovyanGFX instance
    g_lgfx = new LGFX(480, 320);
    if (!g_lgfx) {
        fprintf(stderr, "Failed to create LovyanGFX instance\n");
        return 1;
    }

    g_lgfx->init();
    g_lgfx->setColorDepth(8);  // Set RGB332 (8-bit) color mode
    g_lgfx->fillScreen(FMRB_COLOR_BLACK);
    printf("Graphics handler initialized with LovyanGFX (480x320, RGB332)\n");

    // Initialize graphics handler
    if (graphics_handler_init(nullptr) < 0) {
        fprintf(stderr, "Graphics handler initialization failed\n");
        delete g_lgfx;
        return 1;
    }

    // Initialize audio handler
    if (audio_handler_init() < 0) {
        fprintf(stderr, "Audio handler initialization failed\n");
        graphics_handler_cleanup();
        delete g_lgfx;
        return 1;
    }

    // Start socket server
    if (socket_server_start() < 0) {
        fprintf(stderr, "Socket server start failed\n");
        audio_handler_cleanup();
        graphics_handler_cleanup();
        delete g_lgfx;
        return 1;
    }

    printf("Host server running. Connect fmruby-core or press ESC to quit.\n");

    // Main loop
    while (running && *thread_running) {
        // Process socket messages
        socket_server_process();

        // Small delay to prevent busy waiting
        lgfx::delay(16); // ~60 FPS
    }

    printf("Shutting down...\n");

    // Cleanup
    socket_server_stop();
    audio_handler_cleanup();
    graphics_handler_cleanup();
    delete g_lgfx;
    g_lgfx = nullptr;

    printf("Family mruby Host (SDL2 + LovyanGFX) stopped.\n");
    return 0;
}

int main(int argc, char *argv[]) {
    // Run the user function with LovyanGFX event loop
    return lgfx::Panel_sdl::main(user_func);
}
