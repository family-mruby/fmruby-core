// Proof-of-concept wasm app (Zig). Same behaviour as the Rust and TinyGo apps.
extern "env" fn fmrb_log(ptr: [*]const u8, len: usize) void;
extern "env" fn fmrb_plot(x: i32, y: i32, color: i32) void;
extern "env" fn fmrb_millis() i64;
extern "env" fn fmrb_nop(v: i32) i32;

var frame: i32 = 0;

export fn on_create() void {
    const msg = "hello from zig";
    fmrb_log(msg.ptr, msg.len);
    _ = fmrb_millis();
}

export fn on_update() i32 {
    const f = frame;
    var i: i32 = 0;
    while (i < 64) : (i += 1) {
        const x = (i * 7 + f * 3) & 63;
        const y = (i * 13 + f * 5) & 63;
        const c = (i * 31 + f * 17) & 255;
        fmrb_plot(x, y, c);
    }
    frame = f + 1;
    return 16;
}

// --- test and benchmark entry points (not part of the app API) ---

export fn noop() void {}

export fn bench_compute(n: i32) i32 {
    var x: u32 = 1;
    var acc: u32 = 0;
    var i: i32 = 0;
    while (i < n) : (i += 1) {
        x = x *% 1103515245 +% 12345;
        acc = acc +% ((x >> 16) ^ @as(u32, @bitCast(i)));
    }
    return @bitCast(acc);
}

export fn bench_host_calls(n: i32) i32 {
    var acc: i32 = 0;
    var i: i32 = 0;
    while (i < n) : (i += 1) {
        acc +%= fmrb_nop(i);
    }
    return acc;
}

// call_indirect through a table of function pointers (feature-set check).
fn opA(k: i32) i32 {
    return k + 1;
}
fn opB(k: i32) i32 {
    return k * 2;
}
fn opC(k: i32) i32 {
    return k ^ 0x55;
}
const ops = [_]*const fn (i32) i32{ opA, opB, opC };

export fn dispatch(k: i32) i32 {
    const tbl: *const volatile [3]*const fn (i32) i32 = &ops;
    const f = tbl[@intCast(@mod(k, 3))];
    return f(k);
}

export fn spin() i32 {
    var x: i32 = 0;
    const p: *volatile i32 = &x;
    while (true) {
        p.* +%= 1;
    }
}

export fn grow(pages: i32) i32 {
    return @wasmMemoryGrow(0, @intCast(pages));
}
