# Runs the Ruby profile probes of the asterism repository (profile/ there) in
# the app VM (the PicoRuby compiler plus the mruby VM) and writes one log
# line per probe, "RPROF|<id>|<status>|<detail>", between RPROF-BEGIN and
# RPROF-END. The asterism side turns the log into the table:
#
#   ruby ../asterism/profile/profile.rb sync ../fmruby-core   # refresh the copies
#   (launch /app/test/ruby_profile.app.rb: sim_app, or debugd spawn)
#   ruby ../asterism/profile/profile.rb collect sim-standard
#
# ruby_profile/runner.rb and ruby_profile/probes.txt are copies of the
# asterism files (profile.rb sync writes them). The runner is loaded with eval
# at the top level, so RubyProfile is a top-level module here as on CRuby.
# The block parameter below is the block's own local, not the file's.
eval(File.open("/app/test/ruby_profile/runner.rb", "r") { |f| f.read })

class RubyProfileApp < FmrbApp
  PROBES = "/app/test/ruby_profile/probes.txt"
  IMPL = "app-vm"
  PER_TICK = 8

  def on_create
    text = File.open(PROBES, "r") { |f| f.read }
    @runner = ::RubyProfile::Runner.new(::RubyProfile.parse(text))
    @next = 0
    @done = false
    Log.info(::RubyProfile.header(IMPL, @runner.size))
    show("running #{@runner.size} probes")
  end

  # A few probes per update, so the log keeps up and the window stays live.
  def on_update
    return 1000 if @done
    n = 0
    while @next < @runner.size && n < PER_TICK
      Log.info(@runner.line(@next))
      @next += 1
      n += 1
    end
    if @next >= @runner.size
      @done = true
      Log.info(::RubyProfile.footer(IMPL, @runner.counts))
      c = @runner.counts
      show("ok #{c['ok']} differs #{c['differs']} ng #{c['ng']}")
    end
    20
  end

  private

  def show(text)
    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @gfx.draw_text(6, 6, "Ruby profile", FmrbGfx::WHITE)
    @gfx.draw_text(6, 22, text, FmrbGfx::WHITE)
    @gfx.present
  end
end

# Keep the locals at the top level of this file few and unlikely: in the
# app VM, eval of code that assigns a new local with the name of one of them
# fails with SyntaxError (see the asterism repository's docs/ruby_profile.md).
begin
  RubyProfileApp.new.start
rescue => rprof_top_error
  Log.error("RubyProfileApp: #{rprof_top_error}")
end
