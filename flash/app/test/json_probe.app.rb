# Where does loading a map go? Times the pieces of TileMap.new separately and
# reports them in the log, then repeats the parse on a quarter of the input so
# the shape of the growth is visible: 4x the input costing ~4x is linear (the
# work is the parse itself), ~16x means the cost is superlinear (indexing or
# GC pressure), which calls for a different fix.

class JsonProbeApp < FmrbApp
  PATH = "/app/game/rpg_demo/world.map.json"

  def on_create
    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    say(8, "JSON probe: see the log")
    run_probe
  end

  def on_update
    500
  end

  private

  def say(y, text)
    @gfx.draw_text(8, y, text, FmrbGfx::WHITE)
    @gfx.present
  end

  def stamp(label, t0)
    dt = Machine.board_millis - t0
    Log.info("json_probe: #{label} = #{dt} ms")
    dt
  end

  def noop(a)
    a
  end

  def yielder
    yield 1
  end

  # Cost per call of each operation the JSON parser leans on, all at the same
  # iteration count so the numbers are directly comparable. The question this
  # answers: is one API pathological, or is everything the parser touches
  # expensive?
  def micro_table(text)
    n = 2000
    ws = [' ', "\t", "\n", "\r"]
    arr = [1, 2, 3, 4]
    hash = {}
    s = "abc"

    bench("baseline (empty)", n) { }
    bench("integer +", n) { @acc = 1 + 1 }
    bench("method call", n) { noop(1) }
    bench("block call (yield)", n) { yielder { |v| v } }
    bench("Array#[] int", n) { arr[2] }
    bench("Array#<<", n) { (@tmp ||= []) << 1; @tmp = [] if @tmp.size > 8 }
    bench("Hash#[]=", n) { hash["k"] = 1 }
    bench("String#getbyte", n) { text.getbyte(100) }
    bench("String#[] (1 char)", n) { text[100] }
    bench("String#[i,2]", n) { text[100, 2] }
    bench("String#length", n) { text.length }
    bench("String#==", n) { s == "abc" }
    bench("String#+ (alloc)", n) { s + "d" }
    bench("Array#include? (4)", n) { ws.include?("z") }
    bench("Array#index (4)", n) { ws.index("z") }
  end

  def bench(label, n)
    t = Machine.board_millis
    i = 0
    while i < n
      yield
      i += 1
    end
    ms = Machine.board_millis - t
    us = (ms * 1000) / n
    Log.info("json_probe: micro #{label} = #{ms} ms / #{n} = #{us} us")
  end

  def run_probe
    t = Machine.board_millis
    text = File.open(PATH, "r") { |f| f.read }
    read_ms = stamp("file read (#{text.length} chars, #{text.bytesize} bytes)", t)

    micro_table(text)

    # Below the parser: how much does one pass over the text cost at all?
    # An empty loop gives the interpreter's floor, [] gives character access
    # plus the one-character String it allocates, getbyte gives the same walk
    # without the allocation.
    n = text.length
    Log.info("json_probe: use_regexp=#{::JSON.use_regexp? rescue 'n/a'}")

    t = Machine.board_millis
    i = 0
    while i < n
      i += 1
    end
    stamp("empty loop x#{n}", t)

    t = Machine.board_millis
    i = 0
    while i < n
      c = text[i]
      i += 1
    end
    stamp("text[i] x#{n}", t)

    t = Machine.board_millis
    i = 0
    while i < n
      b = text.getbyte(i)
      i += 1
    end
    stamp("getbyte(i) x#{n}", t)

    t = Machine.board_millis
    i = 0
    ws = [' ', "\t", "\n", "\r"]
    while i < n
      ws.include?(text[i])
      i += 1
    end
    stamp("include?(text[i]) x#{n} (skip_whitespace shape)", t)

    # Same call without allocating anything per iteration: separates the cost
    # of include? itself from the cost of handing it a fresh String.
    probe = "z"
    t = Machine.board_millis
    i = 0
    while i < n
      ws.include?(probe)
      i += 1
    end
    stamp("include?(const) x#{n}", t)

    # Comparison alone, no array walk.
    t = Machine.board_millis
    i = 0
    while i < n
      probe == " "
      i += 1
    end
    stamp("String== x#{n}", t)

    # Allocation alone (no comparison): one short String per iteration.
    t = Machine.board_millis
    i = 0
    while i < n
      s = "ab" + "c"
      i += 1
    end
    stamp("String alloc x#{n}", t)

    gc_ok = (::GC.respond_to?(:disable) rescue false)
    Log.info("json_probe: GC.disable available=#{gc_ok}")
    if gc_ok
      ::GC.disable
      t = Machine.board_millis
      i = 0
      while i < n
        ws.include?(text[i])
        i += 1
      end
      stamp("include?(text[i]) x#{n} GC OFF", t)
      ::GC.enable
    end

    # Full parse, the way TileMap does it.
    t = Machine.board_millis
    obj = ::JSON.parse(text)
    full_ms = stamp("JSON.parse full", t)
    Log.info("json_probe: parsed #{obj["width"]}x#{obj["height"]}")
    obj = nil

    # A quarter of the rows, as its own valid document.
    rows = quarter_doc(text)
    t = Machine.board_millis
    ::JSON.parse(rows)
    q_ms = stamp("JSON.parse quarter (#{rows.bytesize} bytes)", t)

    Log.info("json_probe: read=#{read_ms} full=#{full_ms} quarter=#{q_ms} ratio=#{q_ms > 0 ? full_ms / q_ms : -1}")
    say(24, "read #{read_ms} full #{full_ms} qtr #{q_ms}")
  end

  # Build a valid document holding roughly a quarter of the bytes: the array of
  # row arrays is what dominates, so take the first quarter of the rows.
  def quarter_doc(text)
    open_i = text.index("[[")
    return text if open_i.nil?
    body = text[open_i + 1, text.bytesize - open_i - 1]
    rows = []
    depth = 0
    start = nil
    i = 0
    while i < body.length && rows.size < 16
      c = body[i]
      if c == "["
        start = i if depth == 0
        depth += 1
      elsif c == "]"
        depth -= 1
        if depth == 0 && start
          rows << body[start, i - start + 1]
          start = nil
        end
      end
      i += 1
    end
    "[" + rows.join(",") + "]"
  end
end

begin
  app = JsonProbeApp.new
  app.start
rescue => e
  Log.error("JsonProbe: #{e}")
end
