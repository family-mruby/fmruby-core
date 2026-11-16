//! w2bench (Zig): the W2 speed probe, the same program as c/w2bench.c.
const std = @import("std");
const fmrb = @import("fmrb");

pub const panic = fmrb.panic;

const n_int: i32 = 100000;
const n_fill: i32 = 200;
const n_text: i32 = 100;
const n_call: i32 = 1000;

fn report(comptime t: []const u8, n: i32, us: i64, sum: i64) void {
    var buf: [96]u8 = undefined;
    const line = std.fmt.bufPrint(&buf, "W2B zig " ++ t ++ " n={d} us={d} sum={d}\n", .{ n, us, sum }) catch "W2B zig ?\n";
    fmrb.logInfo(line[0 .. line.len - 1]);
    _ = fmrb.file.append("/tmp/w2b.txt", line);
}

const Bench = struct {
    step: i32 = 0,

    pub fn onCreate(self: *Bench) void {
        _ = self;
        fmrb.logInfo("W2B zig start");
    }

    pub fn onUpdate(self: *Bench) i32 {
        if (self.step >= 12) {
            fmrb.app.stop();
            return 1000;
        }
        const kind = @mod(self.step, 4);
        self.step += 1;
        const g = fmrb.window;
        const t0 = fmrb.uptimeUs();
        switch (kind) {
            0 => {
                var s: i32 = 1;
                var i: i32 = 0;
                while (i < n_int) : (i += 1) {
                    s = ((s *% 31 +% i) ^ (s >> 3)) & 0xFFFFF;
                }
                report("int", n_int, fmrb.uptimeUs() - t0, s);
            },
            1 => {
                var i: i32 = 0;
                while (i < n_fill) : (i += 1) {
                    g.fillRect(@mod(i * 8, 160), @mod(@divTrunc(i, 20) * 8, 64), 8, 8, (i * 7) & 0xFE);
                }
                const us = fmrb.uptimeUs() - t0;
                g.present();
                report("fill", n_fill, us, 0);
            },
            2 => {
                var i: i32 = 0;
                while (i < n_text) : (i += 1) {
                    g.drawText(@mod(i, 4) * 40, @mod(@divTrunc(i, 4), 6) * 10, "0123456789", 0xFF);
                }
                const us = fmrb.uptimeUs() - t0;
                g.present();
                report("text", n_text, us, 0);
            },
            else => {
                var sum: i64 = 0;
                var i: i32 = 0;
                while (i < n_call) : (i += 1) {
                    sum += g.areaWidth();
                }
                report("call", n_call, fmrb.uptimeUs() - t0, sum);
            },
        }
        return 200;
    }
};

comptime {
    fmrb.exportApp(Bench);
}
