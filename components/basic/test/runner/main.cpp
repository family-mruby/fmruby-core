// Host golden test runner for the Family BASIC core.
//
// Builds the core (components/basic/core) with a plain host g++ and runs a
// .bas file against a naive host implementation: output goes to stdout, input
// comes from an optional input file (or stdin), tick is a no-op and memory
// comes from malloc. This file is host only, so malloc/stdio are allowed here
// (the firmware side uses the fmrb_malloc pools instead).
//
// Usage: basic_runner program.bas [input.txt [keys.txt]]
//   input.txt  lines handed to INPUT / LINPUT
//   keys.txt   characters queued for INKEY$ (one key per character)
// Output: the program output and any _SCRDUMP lines, then "OK" on success.
// Errors are printed by the core itself ("?SN ERROR IN 10"), exit status 1.

#include "basic_core.hpp"
#include "basic_charset.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct host_state {
    FILE* input = nullptr;
    fmrb_basic::error_code error = fmrb_basic::error_code::none;
    int32_t error_line = -1;
};

void* host_alloc(void* user, size_t size) {
    (void)user;
    return std::malloc(size);
}

void host_dealloc(void* user, void* ptr) {
    (void)user;
    std::free(ptr);
}

void host_put_char(void* user, char c) {
    (void)user;
    std::fputc(c, stdout);
}

int host_read_line(void* user, char* buf, size_t buf_size) {
    host_state* state = static_cast<host_state*>(user);
    FILE* in = state->input ? state->input : stdin;
    if (!std::fgets(buf, static_cast<int>(buf_size), in)) {
        return -1;
    }
    size_t len = std::strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return static_cast<int>(len);
}

uint32_t host_ticks_ms(void* user) {
    // Deterministic clock: golden output must not depend on wall time. Every
    // read advances it, so PAUSE finishes without ever sleeping.
    (void)user;
    static uint32_t ticks = 0;
    ticks += 1000;
    return ticks;
}

bool host_on_tick(void* user) {
    (void)user;
    return true;
}

void host_on_error(void* user, fmrb_basic::error_code code, int32_t line_number) {
    host_state* state = static_cast<host_state*>(user);
    state->error = code;
    state->error_line = line_number;
}

bool host_audio_play(void* user, const uint8_t* data, uint16_t len) {
    // Print the converted sequence so the golden files pin the MML to FMSQ
    // conversion down byte for byte (the sound itself cannot be checked here).
    (void)user;
    std::printf("FMSQ|%u|", static_cast<unsigned>(len));
    for (uint16_t i = 0; i < len; ++i) {
        std::printf("%02X", data[i]);
    }
    std::printf("\n");
    return true;
}

void host_audio_beep(void* user) {
    (void)user;
    std::printf("BEEP\n");
}

void host_debug_line(void* user, const char* text) {
    // Screen dumps go to stdout so they become part of the golden output; on
    // the device the same lines go to the log (see the B2 report).
    (void)user;
    // The input wait marker exists for the simulation harness, which reads the
    // device log. Here it would only add noise to every INPUT golden, and this
    // runner never has to wait for input in the first place.
    if (std::strncmp(text, "INWAIT|", 7) == 0) {
        return;
    }
    std::printf("%s\n", text);
}

// LOAD / SAVE work inside one directory, chosen with BASIC_PROGRAM_DIR so a
// golden run keeps its files out of the source tree (run_golden.sh sets it).
// The device maps the same names under /home (see fmrb_basic.cpp).
bool program_path(const char* name, char* out, size_t out_size) {
    if (!name || name[0] == '\0') {
        return false;
    }
    const char* dir = std::getenv("BASIC_PROGRAM_DIR");
    if (!dir || dir[0] == '\0') {
        dir = ".";
    }
    const int n = std::snprintf(out, out_size, "%s/%s.bas", dir, name);
    return n > 0 && static_cast<size_t>(n) < out_size;
}

bool host_program_write(void* user, const char* name, const char* text) {
    (void)user;
    char path[512];
    if (!program_path(name, path, sizeof(path))) {
        return false;
    }
    FILE* fp = std::fopen(path, "wb");
    if (!fp) {
        return false;
    }
    const size_t len = std::strlen(text);
    const size_t put = std::fwrite(text, 1, len, fp);
    std::fclose(fp);
    return put == len;
}

// Read the whole file into a NUL terminated buffer owned by the caller.
char* read_file(const char* path) {
    FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        std::fprintf(stderr, "basic_runner: cannot open %s\n", path);
        return nullptr;
    }
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(fp);
        std::fprintf(stderr, "basic_runner: cannot size %s\n", path);
        return nullptr;
    }
    char* buffer = static_cast<char*>(std::malloc(static_cast<size_t>(size) + 1));
    if (!buffer) {
        std::fclose(fp);
        std::fprintf(stderr, "basic_runner: out of memory reading %s\n", path);
        return nullptr;
    }
    const size_t read = std::fread(buffer, 1, static_cast<size_t>(size), fp);
    buffer[read] = '\0';
    std::fclose(fp);
    return buffer;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s program.bas [input.txt [keys.txt]]\n", argv[0]);
        return 2;
    }

    char* source = read_file(argv[1]);
    if (!source) {
        return 2;
    }

    host_state state;
    if (argc >= 3) {
        state.input = std::fopen(argv[2], "rb");
        if (!state.input) {
            std::fprintf(stderr, "basic_runner: cannot open %s\n", argv[2]);
            std::free(source);
            return 2;
        }
    }

    fmrb_basic::basic_host_t host = {};
    host.alloc = host_alloc;
    host.dealloc = host_dealloc;
    host.put_char = host_put_char;
    host.read_line = host_read_line;
    host.ticks_ms = host_ticks_ms;
    // No sleep_ms on purpose: the core treats a host without clock control as
    // "cannot wait", so PAUSE and the blocking INKEY$(0) return immediately
    // instead of hanging a test run.
    host.sleep_ms = nullptr;
    host.on_tick = host_on_tick;
    host.on_error = host_on_error;
    // No extension handler on the host: screen / sprite / sound statements
    // raise IL until the phase that implements them.
    host.ext_statement = nullptr;
    host.debug_line = host_debug_line;
    host.program_write = host_program_write;
    host.audio_play = host_audio_play;
    host.audio_beep = host_audio_beep;
    // No screen renderer here: the shadow buffer plus _SCRDUMP is what the
    // tests inspect.
    host.screen_cell = nullptr;
    host.screen_present = nullptr;
    host.user = &state;

    int status = 0;
    {
        fmrb_basic::interpreter basic(host);
        if (!basic.init()) {
            status = 1;
        } else {
            // Queue the key script (if any) after init(), which resets the
            // queue: INKEY$ consumes one character per call.
            if (argc >= 4) {
                char* keys = read_file(argv[3]);
                if (keys) {
                    size_t i = 0;
                    while (keys[i] != '\0') {
                        if (keys[i] == '\n' || keys[i] == '\r') {
                            ++i;
                            continue;
                        }
                        size_t used = 0;
                        const uint32_t ucs =
                            fmrb_basic::utf8_decode(keys + i, std::strlen(keys + i), &used);
                        basic.push_key(fmrb_basic::unicode_to_fbcode(ucs));
                        i += used;
                    }
                    std::free(keys);
                }
            }
            if (!basic.load(source)) {
                status = 1;
            } else if (!basic.run()) {
                status = 1;
            } else {
                std::printf("OK\n");
            }
        }
    }

    std::fflush(stdout);
    if (state.input) {
        std::fclose(state.input);
    }
    std::free(source);
    return status;
}
