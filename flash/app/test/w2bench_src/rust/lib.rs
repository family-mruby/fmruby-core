//! w2bench (Rust): the W2 speed probe, the same program as c/w2bench.c.
#![no_std]

use fmrb::{Text, WINDOW};

const N_INT: i32 = 100000;
const N_FILL: i32 = 200;
const N_TEXT: i32 = 100;
const N_CALL: i32 = 1000;

#[derive(Default)]
struct Bench {
    step: i32,
}

fn report(test: &str, n: i32, us: i64, sum: i64) {
    let mut t = Text::<96>::from("W2B rs ");
    t.push(test).push(" n=").int(n as i64).push(" us=").int(us).push(" sum=").int(sum);
    fmrb::log::info(t.as_str());
    t.push("\n");
    fmrb::file::append("/tmp/w2b.txt", t.as_str().as_bytes());
}

impl fmrb::App for Bench {
    fn on_create(&mut self) {
        fmrb::log::info("W2B rs start");
    }

    fn on_update(&mut self) -> i32 {
        if self.step >= 12 {
            fmrb::app::stop();
            return 1000;
        }
        let kind = self.step % 4;
        self.step += 1;
        let g = WINDOW;
        let t0 = fmrb::uptime_us();
        match kind {
            0 => {
                let mut s: i32 = 1;
                let mut i: i32 = 0;
                while i < N_INT {
                    s = (s.wrapping_mul(31).wrapping_add(i) ^ (s >> 3)) & 0xFFFFF;
                    i += 1;
                }
                report("int", N_INT, fmrb::uptime_us() - t0, s as i64);
            }
            1 => {
                for i in 0..N_FILL {
                    g.fill_rect((i * 8) % 160, ((i / 20) * 8) % 64, 8, 8, (i * 7) & 0xFE);
                }
                let us = fmrb::uptime_us() - t0;
                g.present();
                report("fill", N_FILL, us, 0);
            }
            2 => {
                for i in 0..N_TEXT {
                    g.draw_text((i % 4) * 40, ((i / 4) % 6) * 10, "0123456789", 0xFF);
                }
                let us = fmrb::uptime_us() - t0;
                g.present();
                report("text", N_TEXT, us, 0);
            }
            _ => {
                let mut sum: i64 = 0;
                for _ in 0..N_CALL {
                    sum += g.area_width() as i64;
                }
                report("call", N_CALL, fmrb::uptime_us() - t0, sum);
            }
        }
        200
    }
}

fmrb::app!(Bench);
