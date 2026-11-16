# w2bench (Ruby, mruby app): the W2 speed probe, the same program as the wasm
# ones in w2bench_src/ (doc/wasm_apps/report/w2.md). One test per on_update,
# three rounds; each logs "W2B rb <test> n=<N> us=<us> sum=<v>", then stops.
class W2BenchApp < FmrbApp
  N_INT = 100000
  N_FILL = 200
  N_TEXT = 100
  N_CALL = 1000

  def on_create
    @step = 0
    Log.info("W2B rb start")
  end

  def report(test, n, us, sum)
    line = "W2B rb #{test} n=#{n} us=#{us} sum=#{sum}"
    Log.info(line)
    FmrbFile.append("/tmp/w2b.txt", line + "\n")
  end

  def on_update
    if @step >= 12
      stop
      return 1000
    end
    kind = @step % 4
    @step += 1
    g = @gfx
    t0 = FmrbApp.uptime_us
    if kind == 0
      s = 1
      i = 0
      while i < N_INT
        s = ((s * 31 + i) ^ (s >> 3)) & 0xFFFFF
        i += 1
      end
      report("int", N_INT, FmrbApp.uptime_us - t0, s)
    elsif kind == 1
      i = 0
      while i < N_FILL
        g.fill_rect((i * 8) % 160, ((i / 20) * 8) % 64, 8, 8, (i * 7) & 0xFE)
        i += 1
      end
      us = FmrbApp.uptime_us - t0
      g.present
      report("fill", N_FILL, us, 0)
    elsif kind == 2
      i = 0
      while i < N_TEXT
        g.draw_text((i % 4) * 40, ((i / 4) % 6) * 10, "0123456789", 0xFF)
        i += 1
      end
      us = FmrbApp.uptime_us - t0
      g.present
      report("text", N_TEXT, us, 0)
    else
      sum = 0
      i = 0
      while i < N_CALL
        sum += g.area_width
        i += 1
      end
      report("call", N_CALL, FmrbApp.uptime_us - t0, sum)
    end
    200
  end
end
