# Spinel PoC harness for InputRouterMixin + WindowManagerMixin.
#
# Goal: run the REAL kernel input-routing code (input_router.rb,
# window_manager.rb) unmodified under both CRuby and Spinel and diff the
# output byte-for-byte. External C bindings and helpers are replaced by
# deterministic Ruby stubs below.
#
# Usage:
#   ruby harness_input_router.rb            # CRuby, full per-event trace
#   ./spinel -E harness_input_router.rb     # Spinel, full per-event trace
#   ruby harness_input_router.rb bench      # summary only (for timing)
#
# Determinism rules (see doc/spinel_aot/phase0.md):
#   - no wall clock, no randomness
#   - Machine.board_millis is a monotonic per-call counter
#   - no raw Float is printed
#
# Design note: Log / stub bindings print directly to stdout in event order
# (gated by $trace) instead of collecting into a shared buffer. Spinel's
# codegen mishandles a mutated global/module-level Array, so we avoid one.

# $trace controls per-event tracing; off in bench mode. A boolean global is
# safe (the target code itself uses the $menu_bar_height global).
$trace = (ARGV[0] != "bench")

# ---------------------------------------------------------------------------
# Stub external constants / singletons the real code references.
# ---------------------------------------------------------------------------

module FmrbConst
  MSG_TYPE_APP_CONTROL = 0
  MSG_TYPE_HID_EVENT   = 3
end

# Deterministic monotonic millisecond source: +1 on every read.
module Machine
  @counter = 0
  def self.board_millis
    @counter += 1
    @counter
  end
end

module Log
  def self.info(m);  puts "  > LOG I #{m}" if $trace; end
  def self.warn(m);  puts "  > LOG W #{m}" if $trace; end
  def self.error(m); puts "  > LOG E #{m}" if $trace; end
  def self.debug(m); puts "  > LOG D #{m}" if $trace; end
end

# Deterministic stand-in for MessagePack.pack: a stable byte string. Keys are
# sorted so output does not depend on hash ordering. The real msgpack subset
# is validated separately in T0-5.
module MessagePack
  def self.pack(obj)
    parts = []
    obj.keys.sort.each do |k|
      parts << "#{k}=#{obj[k]}"
    end
    "MP{" + parts.join(",") + "}"
  end
end

# ---------------------------------------------------------------------------
# Pull in the REAL code under test (unmodified).
# ---------------------------------------------------------------------------

require_relative "../../main/prebuild_scripts/kernel/fmrb_kernel/window_manager.rb"
require_relative "../../main/prebuild_scripts/kernel/fmrb_kernel/input_router.rb"

# ---------------------------------------------------------------------------
# Test host class: includes the real mixins, stubs the C bindings.
# ---------------------------------------------------------------------------

class TestKernel
  include WindowManagerMixin
  include InputRouterMixin

  def initialize
    @window_list = []
    @window_list_dirty = true
    @hid_target_pid = nil
    @capture_mode = nil
    @capture_pid = nil
    @mouse_down_pid = nil
    @desktop_pid = 2
    @fullscreen_pid = nil
    @suspended_pids = []
    @desktop_overlay_active = false
    @desktop_overlay_rect = { x: 0, y: 0, w: 0, h: 0 }

    # drag / resize state
    @drag_offset_x = 0
    @drag_offset_y = 0
    @resize_start_width = 0
    @resize_start_height = 0
    @resize_start_x = 0
    @resize_start_y = 0
    @resize_min_w = 0
    @resize_min_h = 0
    @resize_preview_win_x = 0
    @resize_preview_win_y = 0
    @resize_preview_w = 0
    @resize_preview_h = 0
    @resize_preview_last_send_ms = 0
  end

  # Window fixtures. Symbol keys, matching what the C _get_window_list returns.
  def _get_window_list
    [
      { pid: 2, app_name: "system_desktop", x: 0, y: 0,
        width: 320, height: 240, z_order: 0,
        resizable: false, min_width: 0, min_height: 0, vm_type: :mruby },
      { pid: 3, app_name: "editor", x: 20, y: 20,
        width: 200, height: 150, z_order: 5,
        resizable: true, min_width: 80, min_height: 60, vm_type: :mruby },
      { pid: 4, app_name: "basic_app", x: 60, y: 100,
        width: 120, height: 90, z_order: 3,
        resizable: false, min_width: 0, min_height: 0, vm_type: :basic },
    ]
  end

  # Stubbed C bindings: print the call (event order), return a deterministic result.
  def _set_hid_target(pid);   puts "  > set_hid_target #{pid}" if $trace; true; end
  def _bring_to_front(pid);   puts "  > bring_to_front #{pid}" if $trace; true; end

  def _send_raw_message(pid, type, data)
    puts "  > send pid=#{pid} type=#{type} len=#{data.bytesize}" if $trace
    true
  end

  def _try_send_raw_message(pid, type, data)
    puts "  > try_send pid=#{pid} type=#{type} len=#{data.bytesize}" if $trace
    true
  end

  def _update_window_position(pid, x, y)
    puts "  > update_pos pid=#{pid} x=#{x} y=#{y}" if $trace
    true
  end

  def _update_window_size(pid, w, h)
    puts "  > update_size pid=#{pid} w=#{w} h=#{h}" if $trace
    true
  end

  def _get_app_info(pid)
    win = find_window_by_pid(pid)
    return nil unless win
    { vm_type: win[:vm_type] }
  end

  # Provided by AppLifecycleMixin in the real kernel; stubbed here.
  def app_suspended?(pid)
    @suspended_pids && @suspended_pids.include?(pid)
  end

  # ---- observable state snapshot for the diff ----
  def state_line
    cap = @capture_mode ? @capture_mode.to_s : "none"
    "state cap=#{cap} cap_pid=#{@capture_pid.inspect} " \
      "hid=#{@hid_target_pid.inspect} mdown=#{@mouse_down_pid.inspect} " \
      "pw=#{@resize_preview_w} ph=#{@resize_preview_h}"
  end
end

# ---------------------------------------------------------------------------
# Deterministic HID event generator (6-byte little-endian packets).
# ---------------------------------------------------------------------------

def hid_packet(subtype, button, x, y)
  s = "\x00\x00\x00\x00\x00\x00"
  s.setbyte(0, subtype)
  s.setbyte(1, button)
  s.setbyte(2, x & 0xFF)
  s.setbyte(3, (x >> 8) & 0xFF)
  s.setbyte(4, y & 0xFF)
  s.setbyte(5, (y >> 8) & 0xFF)
  s
end

# Build a deterministic list of scenarios. Each scenario yields packets.
def build_events(scale)
  events = []

  # Scenario A: mouse-move flood (no capture) -- pure hot path.
  (0...(200 * scale)).each do |i|
    x = 10 + (i % 300)
    y = 15 + ((i * 7) % 220)
    events << hid_packet(3, 0, x, y)
  end

  # Scenario B: clicks that hit / miss windows.
  hits = [[100, 80], [70, 120], [5, 5], [310, 235], [30, 30]]
  (0...(20 * scale)).each do |i|
    x = hits[i % hits.size][0]
    y = hits[i % hits.size][1]
    events << hid_packet(4, 1, x, y)   # down
    events << hid_packet(5, 1, x, y)   # up
  end

  # Scenario C: window drag (down on titlebar -> moves -> up).
  (0...(10 * scale)).each do |k|
    events << hid_packet(4, 1, 30, 25)     # down on editor titlebar
    (0...20).each do |j|
      events << hid_packet(3, 1, 30 + j, 25 + (j % 5))
    end
    events << hid_packet(5, 1, 60, 40)     # up
  end

  # Scenario D: resize (down on editor bottom-right handle -> moves -> up).
  (0...(10 * scale)).each do |k|
    events << hid_packet(4, 1, 218, 168)   # editor handle area
    (0...20).each do |j|
      events << hid_packet(3, 1, 218 + j * 2, 168 + j)
    end
    events << hid_packet(5, 1, 260, 210)
  end

  # Scenario E: close button on the non-mruby basic_app
  # (exercises MessagePack.pack + APP_CONTROL send).
  (0...(10 * scale)).each do |k|
    events << hid_packet(4, 1, 175, 103)   # basic_app close button
    events << hid_packet(5, 1, 175, 103)
  end

  events
end

# ---------------------------------------------------------------------------
# Drive.
# ---------------------------------------------------------------------------

scale = $trace ? 1 : 480
events = build_events(scale)

kernel = TestKernel.new

processed = 0
state_changes = 0
prev_state = ""

events.each_with_index do |pkt, idx|
  if $trace
    subtype = pkt.getbyte(0)
    x = pkt.getbyte(2) | (pkt.getbyte(3) << 8)
    y = pkt.getbyte(4) | (pkt.getbyte(5) << 8)
    puts "EV ##{idx} sub=#{subtype} at=(#{x},#{y})"
  end

  msg = { data: pkt, src_pid: "1" }
  kernel.handle_hid_event(msg)
  processed += 1

  st = kernel.state_line
  state_changes += 1 if st != prev_state
  prev_state = st

  puts "  #{st}" if $trace
end

puts "SUMMARY events=#{processed} state_changes=#{state_changes}"
