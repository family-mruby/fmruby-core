# W2 memory probe (doc/wasm_apps/report/w2.md): once a second, appends to
# /tmp/w2p.txt the internal RAM free, the heap free, the wasm runtime's shared
# pool and each wasm app's stats, so the numbers can be read over WiFi when
# the serial log is not at hand. No window drawing beyond a line of text.
class W2ProbeApp < FmrbApp
  OUT = "/tmp/w2p.txt"

  def on_create
    @n = 0
  end

  def on_update
    @n += 1
    h = FmrbApp.heap_info
    w = FmrbApp.wasm_stats
    line = "P #{FmrbApp.uptime_ms} iram=#{h[:iram_free]} heap=#{h[:free]} " \
           "wasm=#{w[:running]}/#{w[:apps]} pool=#{w[:pool_used]}/#{w[:pool_high]}/#{w[:pool_total]} traps=#{w[:traps]}"
    FmrbApp.ps.each do |a|
      s = FmrbApp.wasm_app_stats(a[:pid])
      next unless s
      line += " | #{a[:pid]}:#{a[:name]} pg=#{s[:pages]}/#{s[:max_pages]} bin=#{s[:binary_size]} " \
              "hook=#{s[:hook_us]} max=#{s[:hook_max_us]} calls=#{s[:calls]}"
    end
    FmrbFile.append(OUT, line + "\n")
    @gfx.fill_rect(0, 0, 200, 12, 0xFF)
    @gfx.draw_text(2, 2, "probe #{@n}", 0x00)
    @gfx.present
    1000
  end
end
