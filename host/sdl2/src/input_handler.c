#include "input_handler.h"
#include "input_socket.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// Include HID event definitions (path relative to build)
#include "../../main/include/fmrb_hid_event.h"


// Current log level (default: info)
static input_log_level_t g_input_log_level = INPUT_LOG_INFO;

// Log macros
#define INPUT_LOG_E(fmt, ...) do { if (g_input_log_level >= INPUT_LOG_ERROR) { fprintf(stderr, "[INPUT_ERR] " fmt "\n", ##__VA_ARGS__); } } while(0)
#define INPUT_LOG_I(fmt, ...) do { if (g_input_log_level >= INPUT_LOG_INFO) { printf("[INPUT_INFO] " fmt "\n", ##__VA_ARGS__); } } while(0)
#define INPUT_LOG_D(fmt, ...) do { if (g_input_log_level >= INPUT_LOG_DEBUG) { printf("[INPUT_DBG] " fmt "\n", ##__VA_ARGS__); } } while(0)

void input_handler_set_log_level(int level) {
    if (level >= INPUT_LOG_NONE && level <= INPUT_LOG_DEBUG) {
        g_input_log_level = (input_log_level_t)level;
        printf("[INPUT] Log level set to %d\n", level);
    }
}

static bool g_initialized = false;
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;

// Event watch callback - called before SDL_PollEvent consumes events
static int event_watch_callback(void* userdata, SDL_Event* event) {
    (void)userdata;  // Unused

    switch (event->type) {
        case SDL_KEYDOWN:
            // INPUT_LOG_D("Key pressed: scancode=%d (%s), keycode=%d",
            //            event->key.keysym.scancode,
            //            SDL_GetScancodeName(event->key.keysym.scancode),
            //            event->key.keysym.sym);
            {
                hid_keyboard_event_t kbd_event;
                kbd_event.scancode = (uint8_t)event->key.keysym.scancode;
                kbd_event.keycode = (uint8_t)(event->key.keysym.sym & 0xFF);
                kbd_event.modifier = (uint8_t)(event->key.keysym.mod & 0xFF);
                input_socket_send_event(HID_EVENT_KEY_DOWN, &kbd_event, sizeof(kbd_event));
            }
            break;

        case SDL_KEYUP:
            // INPUT_LOG_D("Key released: scancode=%d (%s), keycode=%d",
            //            event->key.keysym.scancode,
            //            SDL_GetScancodeName(event->key.keysym.scancode),
            //            event->key.keysym.sym);
            {
                hid_keyboard_event_t kbd_event;
                kbd_event.scancode = (uint8_t)event->key.keysym.scancode;
                kbd_event.keycode = (uint8_t)(event->key.keysym.sym & 0xFF);
                kbd_event.modifier = (uint8_t)(event->key.keysym.mod & 0xFF);
                input_socket_send_event(HID_EVENT_KEY_UP, &kbd_event, sizeof(kbd_event));
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            // INPUT_LOG_D("Mouse button %d pressed at (%d, %d)",
            //            event->button.button, event->button.x, event->button.y);
            {
                hid_mouse_button_event_t mouse_event;
                mouse_event.button = event->button.button;
                mouse_event.state = 1;  // pressed
                mouse_event.x = (uint16_t)event->button.x;
                mouse_event.y = (uint16_t)event->button.y;
                input_socket_send_event(HID_EVENT_MOUSE_BUTTON, &mouse_event, sizeof(mouse_event));
            }
            break;

        case SDL_MOUSEBUTTONUP:
            // INPUT_LOG_D("Mouse button %d released at (%d, %d)",
            //            event->button.button, event->button.x, event->button.y);
            {
                hid_mouse_button_event_t mouse_event;
                mouse_event.button = event->button.button;
                mouse_event.state = 0;  // released
                mouse_event.x = (uint16_t)event->button.x;
                mouse_event.y = (uint16_t)event->button.y;
                input_socket_send_event(HID_EVENT_MOUSE_BUTTON, &mouse_event, sizeof(mouse_event));
            }
            break;

        case SDL_MOUSEMOTION:
            // Update current mouse position
            g_last_mouse_x = event->motion.x;
            g_last_mouse_y = event->motion.y;

            // Mouse motion events are very frequent (100+ events/sec when moving)
            // Only log occasionally for debugging
            static int motion_count = 0;
            if (++motion_count % 60 == 0) {  // Log every 60th event
                // INPUT_LOG_D("Mouse moved to (%d, %d)", event->motion.x, event->motion.y);
            }
            // Send to Core with throttling (every 10th event to reduce bandwidth)
            if (motion_count % 10 == 0) {
                hid_mouse_motion_event_t motion_event;
                motion_event.x = (uint16_t)event->motion.x;
                motion_event.y = (uint16_t)event->motion.y;
                input_socket_send_event(HID_EVENT_MOUSE_MOTION, &motion_event, sizeof(motion_event));
            }
            break;

        case SDL_QUIT:
            INPUT_LOG_I("SDL_QUIT event received");
            break;

        default:
            // Ignore other events
            break;
    }

    return 0;  // Return 0 to allow event to continue to event queue
}

int input_handler_init(void) {
    if (g_initialized) {
        INPUT_LOG_E("Input handler already initialized");
        return 0;
    }

    // Register event watch callback
    // This callback is called BEFORE SDL_PollEvent consumes the event
    SDL_AddEventWatch(event_watch_callback, NULL);

    INPUT_LOG_I("Input handler initialized with SDL_AddEventWatch");
    g_initialized = true;
    return 0;
}

int input_handler_process_events(void) {
    if (!g_initialized) {
        INPUT_LOG_E("Input handler not initialized");
        return -1;
    }

    // Events are now handled by event_watch_callback
    // This function is kept for API compatibility but does nothing
    // LovyanGFX will still call SDL_PollEvent and consume events,
    // but our callback already intercepted them

    return 0;
}

void input_handler_cleanup(void) {
    if (!g_initialized) {
        return;
    }

    // Unregister event watch callback
    SDL_DelEventWatch(event_watch_callback, NULL);

    INPUT_LOG_I("Input handler cleaned up");
    g_initialized = false;
}

int input_handler_get_mouse_position(int* x, int* y) {
    if (!g_initialized) {
        INPUT_LOG_E("Input handler not initialized");
        return -1;
    }

    if (x == NULL || y == NULL) {
        INPUT_LOG_E("NULL pointer passed to input_handler_get_mouse_position");
        return -1;
    }

    *x = g_last_mouse_x;
    *y = g_last_mouse_y;
    return 0;
}
