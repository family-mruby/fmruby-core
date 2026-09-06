# StackChan Remote - demo publisher for the "stackchan" pub/sub topic.
#
# Drives the StackChan face app running in another window/task by publishing
# small control messages. Demonstrates inter-app (inter-task) synchronization:
# open StackChan and this app side by side, then tap the buttons here.
#
# Protocol (see stackchan.app.rb):
#   publish("stackchan", {"type"=>"talk",    "on"=>true/false})
#   publish("stackchan", {"type"=>"mouth",   "level"=>0..100})   # lip-sync stream
#   publish("stackchan", {"type"=>"emotion", "name"=>"Happy"})
#   publish("stackchan", {"type"=>"emote",   "name"=>"heart"})   # nil clears

class StackChanRemoteApp < FmrbApp
  TOPIC = "stackchan"

  def on_create
    @talk_on = false
    @lip_on  = false
    @t       = 0
    build_buttons
    draw_screen
  end

  # Build the button list with auto-computed positions (2-3 columns).
  def build_buttons
    x0 = @user_area_x0 + 4
    y0 = @user_area_y0 + 2
    @buttons = []
    # Row 0: two wide toggles
    @buttons << { x: x0,       y: y0, w: 92, h: 20, type: :talk }
    @buttons << { x: x0 + 96,  y: y0, w: 92, h: 20, type: :lip }
    # Emotions (3 cols x 2 rows)
    ey = y0 + 34
    emos = ["Neutral", "Happy", "Angry", "Sad", "Doubt", "Sleepy"]
    i = 0
    while i < emos.size
      col = i % 3
      row = i / 3
      @buttons << { x: x0 + col * 64, y: ey + row * 24, w: 60, h: 20,
                    type: :emotion, value: emos[i], label: emos[i] }
      i += 1
    end
    # Emotes (3 cols x 2 rows); label shown vs published name
    my = ey + 2 * 24 + 14
    elabels = ["Heart", "Shy", "Sweat", "Dizzy", "Clear"]
    evalues = ["heart", "shy", "sweat", "dizzy", nil]
    i = 0
    while i < elabels.size
      col = i % 3
      row = i / 3
      @buttons << { x: x0 + col * 64, y: my + row * 24, w: 60, h: 20,
                    type: :emote, value: evalues[i], label: elabels[i] }
      i += 1
    end
    @emos_label_y = y0 + 24
    @emotes_label_y = my - 12
  end

  def button_label(b)
    case b[:type]
    when :talk then "Talk: #{@talk_on ? "ON" : "off"}"
    when :lip  then "Lip: #{@lip_on ? "ON" : "off"}"
    else b[:label]
    end
  end

  def button_color(b)
    return FmrbGfx::GREEN if (b[:type] == :talk && @talk_on) ||
                             (b[:type] == :lip && @lip_on)
    FmrbGfx::BLUE
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @emos_label_y, "Emotion:", theme_border)
    @gfx.draw_text(@user_area_x0 + 4, @emotes_label_y, "Emote:", theme_border)
    @buttons.each { |b| draw_button(b) }
    draw_window_frame
    @gfx.present
  end

  def draw_button(b)
    bg = button_color(b)
    @gfx.fill_rect(b[:x], b[:y], b[:w], b[:h], bg)
    @gfx.draw_rect(b[:x], b[:y], b[:w], b[:h], theme_border)
    label = button_label(b)
    lx = b[:x] + (b[:w] - label.length * 6) / 2
    ly = b[:y] + (b[:h] - 8) / 2
    @gfx.draw_text(lx, ly, label, FmrbGfx::WHITE, bg)
  end

  def on_event(ev)
    super(ev)
    return unless running?
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    @buttons.each do |b|
      next unless ev[:x] >= b[:x] && ev[:x] < b[:x] + b[:w] &&
                  ev[:y] >= b[:y] && ev[:y] < b[:y] + b[:h]
      handle_button(b)
      break
    end
  end

  def handle_button(b)
    case b[:type]
    when :talk
      @talk_on = !@talk_on
      publish(TOPIC, { "type" => "talk", "on" => @talk_on })
    when :lip
      @lip_on = !@lip_on
      # On stop, send a final closed mouth so the face settles.
      publish(TOPIC, { "type" => "mouth", "level" => 0 }) unless @lip_on
    when :emotion
      publish(TOPIC, { "type" => "emotion", "name" => b[:value] })
    when :emote
      publish(TOPIC, { "type" => "emote", "name" => b[:value] })
    end
    draw_screen
  end

  def on_update
    if @lip_on
      @t += 1
      # Sine sweep 5..95 to demonstrate the lip-sync level stream (~20Hz).
      level = (50 + 45 * Math.sin(@t * 0.5)).to_i
      publish(TOPIC, { "type" => "mouth", "level" => level })
    end
    50
  end
end

begin
  app = StackChanRemoteApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
