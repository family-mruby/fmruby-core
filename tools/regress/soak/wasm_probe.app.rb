#---fmrb
# default_window_mode = "background"
# launcher_visible = false
#---
# The wasm runtime's shared pool for rake regress:soak (tools/regress/soak.rb,
# doc/regression/usage.md chapter 14). The sim prints no periodic dump, so the
# soak starts this from the sim's /tmp, reads the one line it logs and lets it
# end. A board's periodic dump has the same numbers (the fmrb_wasm: lines).
class SoakWasmProbe < FmrbApp
  def on_create
    w = FmrbApp.wasm_stats
    Log.info("SOAK_WASM running=#{w[:running]} apps=#{w[:apps]} modules=#{w[:modules]} " \
             "used=#{w[:pool_used]} high=#{w[:pool_high]} total=#{w[:pool_total]} traps=#{w[:traps]}")
  end

  def on_update
    stop
    100
  end
end
