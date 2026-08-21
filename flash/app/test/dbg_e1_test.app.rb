# FMRB::Debug end-to-end test (Phase E1).
#
# Exercises the on-device debugger API against the dbg_sample target:
#   acquire -> find pid -> attach -> bp_set -> (stopped) -> stack_trace /
#   frame_vars -> step_over -> (stopped/step) -> continue -> (resumed) ->
#   detach. Prints "E1 TEST: PASS" / "E1 TEST: FAIL <why>" to the log and draws
#   the result on its window so the headless harness can verify both ways.
#
# The dbg_sample target must already be running (the harness spawns it first).
# On PASS the app keeps the local session acquired (owner=LOCAL) so the
# exclusion check -- a remote attach must be refused with BUSY -- can run while
# this window shows PASS. It releases on destroy.

TARGET_NAME = "Debug Sample"
TARGET_PATH = "/app/debug/dbg_sample.app.rb"
BP_LINE     = 60   # compute(): locals of every type are in scope here

class DbgE1TestApp < FmrbApp
  def on_create
    @lines  = ["E1 TEST: running..."]
    @result = nil
    draw_screen
    run_test
  end

  def on_update
    300
  end

  def on_destroy
    FMRB::Debug.release
  end

  private

  def now_ms
    Machine.board_millis
  end

  # Find a running app's pid by its ps name (skips ourselves).
  def find_pid(name)
    FmrbApp.ps.each do |a|
      return a[:id] if a[:name] == name && a[:state] == 2  # PROC_STATE_RUNNING
    end
    nil
  end

  # Poll until a matching event arrives or the deadline passes.
  #   want_type: :stopped / :resumed / :exited
  def wait_event(pid, want_type, budget_ms)
    deadline = now_ms + budget_ms
    while now_ms < deadline
      ev = FMRB::Debug.poll_event(100)
      next unless ev
      return ev if ev[:pid] == pid && ev[:type] == want_type
    end
    nil
  end

  def fail_test(why)
    Log.error("E1 TEST: FAIL #{why}")
    @result = false
    @lines << "FAIL: #{why}"
    draw_screen
    false
  end

  def pass_test(nframes, nvars)
    Log.info("E1 TEST: PASS frames=#{nframes} vars=#{nvars}")
    @result = true
    @lines << "frames=#{nframes} vars=#{nvars}"
    @lines << "PASS"
    draw_screen
    true
  end

  def run_test
    unless FMRB::Debug.acquire
      return fail_test("acquire (owner=#{FMRB::Debug.owner})")
    end
    @lines << "acquired"

    pid = nil
    20.times do
      pid = find_pid(TARGET_NAME)
      break if pid
      Machine.delay_ms(100)
    end
    return fail_test("target not found") unless pid
    @lines << "pid=#{pid}"

    return fail_test("attach") unless FMRB::Debug.attach(pid)

    bp = FMRB::Debug.bp_set(pid, TARGET_PATH, BP_LINE)
    return fail_test("bp_set") unless bp

    ev = wait_event(pid, :stopped, 6000)
    return fail_test("no stop") unless ev
    return fail_test("stop reason #{ev[:reason]}") unless ev[:reason] == :breakpoint

    frames = FMRB::Debug.stack_trace(pid, 16)
    return fail_test("no frames") unless frames && frames.size >= 1

    vars = FMRB::Debug.frame_vars(pid, 0)
    return fail_test("no vars") unless vars
    known = false
    vars.each do |v|
      known = true if v["name"] == "doubled" || v["name"] == "label"
    end
    return fail_test("expected local missing") unless known

    return fail_test("step_over") unless FMRB::Debug.step_over(pid)
    sev = wait_event(pid, :stopped, 3000)
    return fail_test("no step stop") unless sev
    return fail_test("step reason #{sev[:reason]}") unless sev[:reason] == :step

    return fail_test("continue") unless FMRB::Debug.continue(pid)
    return fail_test("no resume") unless wait_event(pid, :resumed, 3000)

    FMRB::Debug.detach(pid)
    # Keep the local session (owner=LOCAL) so the exclusion check can observe a
    # remote attach being refused; released in on_destroy.
    pass_test(frames.size, vars.size)
  end

  def draw_screen
    return unless @gfx
    clear_user_area
    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @lines.each do |ln|
      color = if ln.start_with?("PASS")
                0x14  # dark green, reads on the pale page
              elsif ln.start_with?("FAIL")
                FmrbGfx::RED
              else
                theme_fg
              end
      @gfx.draw_text(x, y, ln, color)
      y += 12
    end
    draw_window_frame
    @gfx.present
  end
end

begin
  app = DbgE1TestApp.new
  app.start
rescue => e
  Log.error("DbgE1TestApp: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
