# frozen_string_literal: true
#
# scene.rb - the scene scripts' language, and one run of a scene.
#
# A scene file (tools/regress/scenes/*.rb) names an app and its scenes:
#
#   Regress.app "othello_table", "flash/app/game/othello/othello.app.rb" do
#     on { |t| t.screen[0] >= 320 }      # the profiles it applies to (default: all)
#     setup { |s| ... }                  # before every scene (seed files, the host)
#     scene "list" do |s|
#       s.net.host.tables["games-host-a1b2c3/games/othello1"] = { "st" => "wait" }
#       s.start                          # new + on_create
#       s.update(3)                      # on_update, three times
#       s.key :down
#       s.update
#     end
#     scene "seated", lang: "ja", on: ->(t) { t.family == "modern" } do |s| ... end
#   end
#
# Every scene runs once per profile it applies to (targets.rb), each run in
# a forked process of its own, and its record is everything drawn from
# s.start on.
module Regress
  CORE = File.expand_path("../..", __dir__)

  AppDef = Struct.new(:name, :file, :filter, :setups, :scenes, :gems, :window, :klass, :prelude, :inline,
                      keyword_init: true)
  SceneDef = Struct.new(:app, :name, :lang, :filter, :body, keyword_init: true) do
    def id = "#{app.name}/#{name}"

    # On a profile (its boards): one of them carries the app's file, and the
    # app's and the scene's own conditions say yes.
    def applies?(profile)
      t = profile.target
      (app.file.nil? || profile.boards.any? { |b| b.carries?(app.file, CORE) }) &&
        (app.filter.nil? || app.filter.call(t)) && (filter.nil? || filter.call(t))
    end
  end

  @apps = []
  class << self
    attr_reader :apps

    def app(name, file, &blk)
      d = AppDef.new(name: name, file: file, filter: nil, setups: [], scenes: [], gems: [], window: nil, klass: nil,
                     prelude: [], inline: nil)
      AppBuilder.new(d).instance_eval(&blk)
      @apps << d
      d
    end

    def scenes = @apps.flat_map(&:scenes)
  end

  class AppBuilder
    def initialize(d) = @d = d

    # Which profiles (Target) the app applies to.
    def on(&blk) = @d.filter = blk
    def setup(&blk) = @d.setups << blk
    # More gem directories under lib/add/ whose mrblib/*.rb the app needs.
    def gems(*names) = @d.gems.concat(names)
    # The window when the app has no .app.toml (a built-in app): w:, h:, name:, fullscreen:, hires:.
    def window(**kw) = @d.window = kw
    # The class to make when the file's start code does not say.
    def klass(name) = @d.klass = name
    # Ruby run before the app's file is read (a stand-in only this app needs).
    def prelude(&blk) = @d.prelude << blk
    # An app written in the scene file itself (file nil): the block defines
    # the class named with klass. For the parts of the gem no shipped mruby
    # app shows (a page of every FmrbUI widget).
    def inline(&blk) = @d.inline = blk

    def scene(name, lang: "en", on: nil, &body)
      @d.scenes << SceneDef.new(app: @d, name: name, lang: lang, filter: on, body: body)
    end
  end

  # Key names for Session#key: [scancode, character].
  KEYS = {
    enter: [0x28, 13], esc: [0x29, 27], backspace: [0x2A, 8], tab: [0x2B, 9], space: [0x2C, 32],
    right: [0x4F, 0], left: [0x50, 0], down: [0x51, 0], up: [0x52, 0], home: [0x4A, 0], end: [0x4D, 0],
    pgup: [0x4B, 0], pgdn: [0x4E, 0], delete: [0x4C, 0],
    f1: [0x3A, 0], f2: [0x3B, 0], f3: [0x3C, 0], f4: [0x3D, 0], f5: [0x3E, 0], f11: [0x44, 0]
  }.freeze
  PUNCT = { "-" => 0x2D, "=" => 0x2E, "[" => 0x2F, "]" => 0x30, "\\" => 0x31, ";" => 0x33, "'" => 0x34,
            "`" => 0x35, "," => 0x36, "." => 0x37, "/" => 0x38, " " => 0x2C }.freeze

  # One run: the app on one profile, driven by the scene's body.
  class Session
    attr_reader :app, :target, :scene

    def initialize(scene, target)
      @scene = scene
      @target = target
      @app = nil
    end

    def net = Regress::Net
    def host = Regress::Net.host
    def env = Regress::Env
    def seed(path, text) = FakeFS.seed(path, text)
    def mark(text) = Rec.mark(text)
    def now = Env.ms

    def start
      cls = Object.const_get(Regress.app_class)
      Rec.mark("start")
      @app = cls.new
      @app.instance_variable_set(:@_running, true)
      @app.send(:on_create)
      pump
      self
    end

    # n turns of the app's loop: on_update, the messages, the wait it asked
    # for (at least 1 ms, at most max_ms), the timers.
    def update(n = 1, max_ms: 1000)
      n.times do |i|
        Rec.mark(n > 1 ? "update #{i + 1}/#{n}" : "update")
        ms = @app.send(:on_update)
        pump
        step = ms.is_a?(Numeric) ? ms.to_i : 0
        step = 1 if step < 1
        step = max_ms if step > max_ms
        Env.ms += step
        @app.send(:_run_timers)
        break unless @app.running?
      end
      self
    end

    # Runs the loop until ms of the fake clock have gone by.
    def run(ms, max_ms: 1000)
      Rec.mark("run #{ms} ms")
      t_end = Env.ms + ms
      turns = 0
      while Env.ms < t_end && @app.running? && turns < 10_000
        v = @app.send(:on_update)
        pump
        step = v.is_a?(Numeric) ? v.to_i : 0
        step = 1 if step < 1
        step = max_ms if step > max_ms
        step = t_end - Env.ms if Env.ms + step > t_end
        Env.ms += step
        @app.send(:_run_timers)
        turns += 1
      end
      self
    end

    def wait(ms) = (Env.ms += ms; self)

    # A control message from the kernel (on_control): file_selected and the like.
    def control(msg)
      Rec.mark("control #{msg['cmd']}")
      @app.send(:on_control, msg) if @app.respond_to?(:on_control, true)
      pump
      self
    end

    def event(ev)
      @app.send(:_dispatch_event, ev)
      pump
      self
    end

    # A key: a name from KEYS, or a one-character String. ctrl/shift/alt add
    # the modifier (both down and up are sent).
    def key(k, ctrl: false, shift: false, alt: false)
      sc, ch = key_codes(k)
      mod = (ctrl ? 0x04 : 0) | (shift ? 0x01 : 0) | (alt ? 0x10 : 0)
      Rec.mark("key #{k.is_a?(String) ? k.inspect : k}#{ctrl ? ' ctrl' : ''}#{shift ? ' shift' : ''}#{alt ? ' alt' : ''}")
      ch = 0 if ctrl
      event({ type: :key_down, keycode: ch, scancode: sc, modifier: mod, character: ch })
      event({ type: :key_up, keycode: ch, scancode: sc, modifier: mod, character: ch })
    end

    def type(text)
      text.each_char { |c| key(c) }
      self
    end

    def key_codes(k)
      return KEYS.fetch(k) if k.is_a?(Symbol)
      c = k.to_s
      o = c.ord
      sc = if c =~ /[a-z]/ then 0x04 + (o - 97)
           elsif c =~ /[A-Z]/ then 0x04 + (o - 65)
           elsif c =~ /[1-9]/ then 0x1E + (o - 49)
           elsif c == "0" then 0x27
           else PUNCT[c] || 0
           end
      [sc, o]
    end

    # A click at window coordinates (as the app's events carry them).
    def click(x, y, button: 1)
      Rec.mark("click #{x},#{y}#{button == 1 ? '' : " button #{button}"}")
      event({ type: :mouse_down, button: button, x: x, y: y })
      event({ type: :mouse_up, button: button, x: x, y: y })
    end

    # A click on an FmrbUI widget of the app (by its id): its middle, or
    # near its left / right end (a stepper's - and +).
    def tap(id, ui: :@ui, at: :middle, button: 1)
      w = @app.instance_variable_get(ui).find(id)
      raise "no widget #{id.inspect} in #{ui}" unless w
      x = case at
          when :left then w.x + 3
          when :right then w.x + w.w - 4
          else w.x + w.w / 2
          end
      click(x, w.y + w.h / 2, button: button)
    end

    def press(x, y, button: 1) = (Rec.mark("press #{x},#{y}"); event({ type: :mouse_down, button: button, x: x, y: y }))
    def release(x, y, button: 1) = (Rec.mark("release #{x},#{y}"); event({ type: :mouse_up, button: button, x: x, y: y }))
    def move(x, y) = (Rec.mark("move #{x},#{y}"); event({ type: :mouse_move, x: x, y: y }))
    def wheel(d, x: 10, y: 20) = (Rec.mark("wheel #{d}"); event({ type: :mouse_wheel, delta: d, x: x, y: y }))

    def ivar(name, value = nil)
      return @app.instance_variable_get(name) if value.nil?
      @app.instance_variable_set(name, value)
    end

    # The kernel's side of the messages the app sent: a fullscreen request
    # becomes the resize app.c delivers; the rest go into the record.
    def pump
      until Env.outbox.empty?
        _dest, type, data = Env.outbox.shift
        # The audio commands (FmrbAudio, type 3) are not what the screen shows.
        next if type == 3
        cmd = data.is_a?(Hash) ? data["cmd"] : nil
        case cmd
        when "enter_fullscreen", "exit_fullscreen"
          Rec.mark("kernel #{cmd}")
          on = cmd == "enter_fullscreen"
          w = Env.extra[:window]
          size = on ? @target.fullscreen_size(w[:hires]) : [w[:win_w], w[:win_h]]
          resize(size[0], size[1], on)
        when "exit"
          nil
        else
          Rec.add("msg #{type} #{Fmt.v(short(data))}")
        end
      end
    end

    def short(v)
      case v
      when String then v.bytesize > 64 ? "<#{v.bytesize} bytes>" : v
      when Array then v.map { |e| short(e) }
      when Hash then v.to_h { |k, e| [k, short(e)] }
      else v
      end
    end

    # What app.c does on a "resize" control message.
    def resize(w, h, fullscreen)
      a = @app
      a.instance_variable_set(:@_fullscreen, fullscreen)
      a.instance_variable_set(:@window_width, w)
      a.instance_variable_set(:@window_height, h)
      a.instance_variable_set(:@user_area_x0, fullscreen ? 0 : 1)
      a.instance_variable_set(:@user_area_y0, fullscreen ? 0 : 11)
      a.instance_variable_set(:@user_area_width, fullscreen ? w : w - 2)
      a.instance_variable_set(:@user_area_height, fullscreen ? h : h - 12)
      a.instance_variable_set(:@user_area_x1, fullscreen ? w : w - 1)
      a.instance_variable_set(:@user_area_y1, fullscreen ? h : h - 1)
      a._apply_user_area_sprite_clip
      a.send(:on_resize, w, h)
    end
  end
end
