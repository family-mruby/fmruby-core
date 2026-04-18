# GfxBlock - compile a Ruby drawing block to bytecode and cache it on WROVER.
# Subsequent draw(**kwargs) calls re-evaluate the block to compute register
# values, then send only the changed registers.
#
# Design constraints (see doc/window_roundcorner_and_batch_vm.md):
# - Block must produce the SAME command sequence every time (no branches that
#   change command count, no dynamic strings, no rand/time).
# - Only Integer/Float kwargs are supported. Booleans raise.
# - String args are fixed (stored in strtable at new time).
# - Max 16 registers per block.

class GfxBlock
  class Error < StandardError; end
  class StructureError < Error; end
  class TooManyRegsError < Error; end
  class UnsupportedKwargError < Error; end
  class PayloadTooLargeError < Error; end

  MAX_REGS = 16
  # Max DEFINE_PROG payload that fits in a single UART frame (conservative).
  # Must match FMRB_GFX_DEFINE_PROG_SINGLE_FRAME_LIMIT in fmrb_gfx.c.
  # Payload layout: header(6) + bytecode + strtable.
  DEFINE_PROG_MAX_PAYLOAD = 220

  # Recorder used while evaluating the block. Captures drawing calls as
  # [opcode, arg, arg, ...] entries and interns strings into a table.
  class Recorder
    attr_reader :cmds, :strings

    def initialize
      @cmds = []
      @strings = []
    end

    def clear(color)
      @cmds << [FmrbGfx::GFXVM_OP_CLEAR, to_i(color)]
    end

    def fill_rect(x, y, w, h, color)
      @cmds << [FmrbGfx::GFXVM_OP_FILL_RECT, to_i(x), to_i(y), to_i(w), to_i(h), to_i(color)]
    end

    def draw_rect(x, y, w, h, color)
      @cmds << [FmrbGfx::GFXVM_OP_DRAW_RECT, to_i(x), to_i(y), to_i(w), to_i(h), to_i(color)]
    end
    alias rect draw_rect

    def fill_round_rect(x, y, w, h, r, color)
      @cmds << [FmrbGfx::GFXVM_OP_FILL_ROUND_RECT,
                to_i(x), to_i(y), to_i(w), to_i(h), to_i(r), to_i(color)]
    end

    def draw_round_rect(x, y, w, h, r, color)
      @cmds << [FmrbGfx::GFXVM_OP_DRAW_ROUND_RECT,
                to_i(x), to_i(y), to_i(w), to_i(h), to_i(r), to_i(color)]
    end

    def draw_line(x0, y0, x1, y1, color)
      @cmds << [FmrbGfx::GFXVM_OP_DRAW_LINE, to_i(x0), to_i(y0), to_i(x1), to_i(y1), to_i(color)]
    end
    alias line draw_line

    def fill_circle(x, y, r, color)
      @cmds << [FmrbGfx::GFXVM_OP_FILL_CIRCLE, to_i(x), to_i(y), to_i(r), to_i(color)]
    end

    def draw_text(x, y, str, color)
      str_id = intern_string(str)
      @cmds << [FmrbGfx::GFXVM_OP_DRAW_TEXT, to_i(x), to_i(y), to_i(color), str_id]
    end
    alias text draw_text

    private

    def to_i(v)
      case v
      when Integer then v
      when Float   then v.to_i
      when true    then 1
      when false   then 0
      else
        raise GfxBlock::Error, "unsupported arg type: #{v.class}"
      end
    end

    def intern_string(s)
      unless s.is_a?(String)
        raise GfxBlock::Error, "draw_text expects a String"
      end
      idx = @strings.index(s)
      return idx if idx
      @strings << s
      @strings.size - 1
    end
  end

  # @param gfx [FmrbGfx] target graphics instance
  # @param initial_values [Hash{Symbol => Integer|Float}] sample kwargs
  # @param block [Proc] drawing block; receives **kwargs and calls DSL methods
  def initialize(gfx, **initial_values, &block)
    raise ArgumentError, "block required" unless block
    @gfx = gfx
    @block = block
    @destroyed = false

    keys = initial_values.keys
    keys.each do |k|
      v = initial_values[k]
      unless v.is_a?(Integer) || v.is_a?(Float) || v.is_a?(String)
        raise UnsupportedKwargError,
              "kwarg :#{k} has unsupported type #{v.class} (Integer/Float/String only)"
      end
    end
    @initial_values = initial_values

    # Pass 1: record commands with initial values. Block receives the recorder
    # as first positional arg plus kwargs (PicoRuby lacks instance_exec).
    rec1 = Recorder.new
    block.call(rec1, **initial_values)

    # Pass 2: record with sentinel values. Integers get +1, Floats get +1.0.
    # Strings stay fixed (their str_id is assigned at intern time and remains
    # stable across passes since Recorder's #intern_string re-uses the same id).
    sentinel_values = {}
    keys.each do |k|
      v = initial_values[k]
      sentinel_values[k] = case v
                            when Integer then v + 1
                            when Float   then v + 1.0
                            else              v
                            end
    end
    rec2 = Recorder.new
    block.call(rec2, **sentinel_values)

    # Detect structure equivalence (same number of commands, same opcodes, same arity)
    unless rec1.cmds.length == rec2.cmds.length
      raise StructureError,
            "sentinel pass produced #{rec2.cmds.length} cmds (expected #{rec1.cmds.length})"
    end
    i = 0
    while i < rec1.cmds.length
      c1 = rec1.cmds[i]
      c2 = rec2.cmds[i]
      if c1[0] != c2[0] || c1.length != c2.length
        raise StructureError, "cmd #{i} structure differs between passes"
      end
      i += 1
    end

    # Variable position detection. For each (cmd_idx, arg_idx): value differs => register.
    @kwarg_keys = keys
    @var_map = []        # parallel to rec1.cmds
    @var_positions = []  # list of [cmd_idx, arg_idx] for each reg in order
    reg_id = 0
    i = 0
    while i < rec1.cmds.length
      c1 = rec1.cmds[i]
      c2 = rec2.cmds[i]
      row = Array.new(c1.length)  # all nil by default
      j = 1
      while j < c1.length
        if c1[j] != c2[j]
          raise TooManyRegsError, "program needs >#{MAX_REGS} registers" if reg_id >= MAX_REGS
          row[j] = reg_id
          @var_positions << [i, j]
          reg_id += 1
        end
        j += 1
      end
      @var_map << row
      i += 1
    end
    @reg_count = reg_id

    # Compile bytecode and strtable.
    bc_str, st_str = @gfx._gfx_compile_block(rec1.cmds, @var_map, rec1.strings)

    # Reject programs that would need transport-layer fragmentation.
    payload_size = 6 + bc_str.length + st_str.length   # header + bytecode + strtable
    if payload_size > DEFINE_PROG_MAX_PAYLOAD
      raise PayloadTooLargeError,
            "GfxBlock payload #{payload_size} B exceeds single-frame limit " \
            "#{DEFINE_PROG_MAX_PAYLOAD} B (bytecode=#{bc_str.length} strtable=#{st_str.length})"
    end

    # Register program on WROVER (sync; returns prog_id).
    @prog_id = @gfx._gfx_define_prog(bc_str, st_str)
    @canvas_gfx = gfx

    # Initial EXEC sends every register.
    @prev_regs = extract_regs(rec1.cmds)
    initial_pairs = []
    k = 0
    while k < @prev_regs.length
      initial_pairs << [k, @prev_regs[k]]
      k += 1
    end
    exec_updates(initial_pairs)
  end

  # @param kwargs [Hash] new kwarg values (must have same keys as initial_values)
  def draw(**kwargs)
    return if @destroyed

    # Evaluate block with new kwargs. Block receives the recorder as first arg.
    rec = Recorder.new
    @block.call(rec, **kwargs)

    # Structural assertion: command count + opcode must match.
    unless rec.cmds.length == @var_map.length
      raise StructureError,
            "draw produced #{rec.cmds.length} cmds, expected #{@var_map.length}"
    end

    new_regs = extract_regs(rec.cmds)
    changed = []
    i = 0
    while i < new_regs.length
      changed << [i, new_regs[i]] if new_regs[i] != @prev_regs[i]
      i += 1
    end
    return if changed.empty?
    exec_updates(changed)
    @prev_regs = new_regs
  end

  def destroy
    return if @destroyed
    @canvas_gfx._gfx_delete_prog(@prog_id) if @prog_id
    @destroyed = true
  end

  def destroyed?
    @destroyed
  end

  private

  def extract_regs(cmds)
    regs = Array.new(@reg_count, 0)
    reg = 0
    while reg < @var_positions.length
      ci, ai = @var_positions[reg]
      regs[reg] = cmds[ci][ai]
      reg += 1
    end
    regs
  end

  def exec_updates(pairs)
    # Pairs: Array of [reg_id, value]. Clips value to int16 range on C side.
    @canvas_gfx._gfx_exec_prog(@prog_id, pairs)
  end
end
