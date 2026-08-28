# Feasibility probe for Ruby Unified proxies on the app VM: method_missing
# dispatch, respond_to_missing?, stored blocks with local capture, send,
# and define_method (mruby-metaprog). Results go to the log as "MMCHK".
class MmProxy
  def initialize
    @blk = nil
  end

  def method_missing(name, *args, &blk)
    @blk = blk if blk
    [:mm, name, args]
  end

  def respond_to_missing?(name, include_private = false)
    name == :stop_cmd
  end

  def fire(v)
    @blk ? @blk.call(v) : :noblk
  end
end

class MmStub
end

class MmCheckApp < FmrbApp
  def on_create
    @pass = 0
    @fail = 0

    run_check("plain call") do
      p = MmProxy.new
      expect("plain", p.battery, [:mm, :battery, []])
    end

    run_check("positional args") do
      p = MmProxy.new
      expect("args", p.move_to(3, 4), [:mm, :move_to, [3, 4]])
    end

    run_check("kwargs arrive as trailing hash") do
      p = MmProxy.new
      r = p.move_to(x: 1, y: 2)
      h = r[2][0]
      expect("kwhash", [r[0], r[1], h.class == Hash ? h[:x] : nil], [:mm, :move_to, 1])
    end

    run_check("stored block with local capture") do
      p = MmProxy.new
      captured = 10
      p.on_collision { |v| v + captured }
      expect("block", p.fire(5), 15)
    end

    run_check("respond_to? honors respond_to_missing?") do
      p = MmProxy.new
      expect("rtm true", p.respond_to?(:stop_cmd), true)
      expect("rtm false", p.respond_to?(:zzz), false)
    end

    run_check("send goes through method_missing") do
      p = MmProxy.new
      expect("send", p.send(:battery), [:mm, :battery, []])
    end

    run_check("define_method generates a stub") do
      ok = false
      begin
        MmStub.define_method(:gen_answer) { 42 }
        ok = true
      rescue NoMethodError
        MmStub.send(:define_method, :gen_answer) { 42 }
        Log.info("MMCHK note: define_method is private, send() needed")
        ok = true
      end
      expect("define_method", ok && MmStub.new.gen_answer, 42)
    end

    run_check("define_method with args") do
      MmStub.send(:define_method, :add2) { |a, b| a + b }
      expect("define_method args", MmStub.new.add2(2, 3), 5)
    end

    run_check("methods introspection") do
      p = MmProxy.new
      ms = p.methods
      expect("methods", ms.class == Array && (ms.index(:fire) != nil), true)
    end

    run_check("instance_variable_get (metaprog)") do
      p = MmProxy.new
      p.battery
      expect("ivar_get", p.instance_variable_get(:@blk), nil)
    end

    Log.info("MMCHK DONE pass=#{@pass} fail=#{@fail}")
    draw_summary
  end

  def run_check(label)
    yield
  rescue => e
    @fail += 1
    Log.error("MMCHK NG #{label}: #{e.class}: #{e.message}")
  end

  def expect(label, got, want)
    if got == want
      @pass += 1
      Log.info("MMCHK ok #{label}")
    else
      @fail += 1
      Log.error("MMCHK NG #{label}: got=#{got.inspect} want=#{want.inspect}")
    end
  end

  def draw_summary
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4, "MMCHK", theme_fg)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 16,
                   "pass=#{@pass} fail=#{@fail}", theme_fg)
    @gfx.present
  end

  def on_update
    1000
  end
end

begin
  app = MmCheckApp.new
  app.start
rescue => e
  Log.error("MmCheckApp: #{e}")
end
