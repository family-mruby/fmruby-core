// Proof-of-concept wasm app (Rust). Same behaviour as the TinyGo and Zig apps:
// the host drives the loop and calls on_create once, then on_update N times.
#![no_std]

#[link(wasm_import_module = "env")]
extern "C" {
    fn fmrb_log(ptr: *const u8, len: usize);
    fn fmrb_plot(x: i32, y: i32, color: i32);
    fn fmrb_millis() -> i64;
    fn fmrb_nop(v: i32) -> i32;
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

static mut FRAME: i32 = 0;

#[no_mangle]
pub extern "C" fn on_create() {
    let msg = b"hello from rust";
    unsafe {
        fmrb_log(msg.as_ptr(), msg.len());
        let _ = fmrb_millis();
    }
}

#[no_mangle]
pub extern "C" fn on_update() -> i32 {
    unsafe {
        let f = FRAME;
        let mut i = 0;
        while i < 64 {
            let x = (i * 7 + f * 3) & 63;
            let y = (i * 13 + f * 5) & 63;
            let c = (i * 31 + f * 17) & 255;
            fmrb_plot(x, y, c);
            i += 1;
        }
        FRAME = f + 1;
    }
    16
}

// --- test and benchmark entry points (not part of the app API) ---

#[no_mangle]
pub extern "C" fn noop() {}

// Integer-only loop used for the speed comparison (same as bench_native.c).
#[no_mangle]
pub extern "C" fn bench_compute(n: i32) -> i32 {
    let mut x: u32 = 1;
    let mut acc: u32 = 0;
    let mut i = 0;
    while i < n {
        x = x.wrapping_mul(1103515245).wrapping_add(12345);
        acc = acc.wrapping_add((x >> 16) ^ (i as u32));
        i += 1;
    }
    acc as i32
}

#[no_mangle]
pub extern "C" fn bench_host_calls(n: i32) -> i32 {
    let mut acc = 0i32;
    let mut i = 0;
    while i < n {
        acc = acc.wrapping_add(unsafe { fmrb_nop(i) });
        i += 1;
    }
    acc
}

// call_indirect through a table of function pointers (feature-set check).
fn op_a(k: i32) -> i32 { k + 1 }
fn op_b(k: i32) -> i32 { k * 2 }
fn op_c(k: i32) -> i32 { k ^ 0x55 }
static OPS: [fn(i32) -> i32; 3] = [op_a, op_b, op_c];

#[no_mangle]
pub extern "C" fn dispatch(k: i32) -> i32 {
    let f = core::hint::black_box(OPS[(k as usize) % 3]);
    f(k)
}

// Never returns: the host must stop it with the instruction limit.
#[no_mangle]
pub extern "C" fn spin() -> i32 {
    let mut x: i32 = 0;
    loop {
        x = unsafe { core::ptr::read_volatile(&x) }.wrapping_add(1);
        unsafe { core::ptr::write_volatile(&mut x, x) };
    }
}

// Tries to grow the linear memory; returns the old size in pages or -1.
#[no_mangle]
pub extern "C" fn grow(pages: i32) -> i32 {
    core::arch::wasm32::memory_grow(0, pages as usize) as i32
}
