// Host golden test runner for the Family BASIC core.
//
// Builds the core (components/basic/core) with a plain host g++ and runs a
// .bas file against a naive host implementation: output goes to stdout, input
// comes from an optional input file (or stdin), tick is a no-op and memory
// comes from malloc. This file is host only, so malloc/stdio are allowed here
// (the firmware side uses the fmrb_malloc pools instead).
//
// Usage: basic_runner program.bas [input.txt]
// Output: the program output, then "OK" on success. Errors are printed by the
// core itself ("?SN ERROR IN 10"), and the exit status is 1.

#include "basic_core.hpp"

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

void host_sleep_ms(void* user, uint32_t ms) {
    // Tests must not spend real time waiting.
    (void)user;
    (void)ms;
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
        std::fprintf(stderr, "usage: %s program.bas [input.txt]\n", argv[0]);
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
    host.sleep_ms = host_sleep_ms;
    host.on_tick = host_on_tick;
    host.on_error = host_on_error;
    // No extension handler on the host: screen / sprite / sound statements
    // raise IL until the phase that implements them.
    host.ext_statement = nullptr;
    host.user = &state;

    int status = 0;
    {
        fmrb_basic::interpreter basic(host);
        if (!basic.init()) {
            status = 1;
        } else if (!basic.load(source)) {
            status = 1;
        } else if (!basic.run()) {
            status = 1;
        } else {
            std::printf("OK\n");
        }
    }

    std::fflush(stdout);
    if (state.input) {
        std::fclose(state.input);
    }
    std::free(source);
    return status;
}
