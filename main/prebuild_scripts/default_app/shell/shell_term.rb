# What a Linux program prints, turned into Shell lines (doc/linux_native L10).
#
# The terminal is TERM=dumb and only a little of it is understood: lines
# (\n), going back to the start of the line to write it again (\r, the way
# progress counters redraw themselves), backspace, tab, and the SGR colours
# (the 8 colours and their bright 8, for the text and the background, bold,
# and the reset). Every other control sequence -- cursor movement, erasing,
# titles (OSC) -- is read and dropped. Full-screen programs are not for this.
#
# Bytes in, entries out. A finished line is a String when it has no colour,
# else {:type => :ansi, :text => String, :spans => [[first_char, attr], ...]}:
# each span's attr holds from its first character to the next span's. attr
# packs the colour indexes (fg + 1) | ((bg + 1) << 5): 0 is the Shell's own
# colours, 1..16 the SGR colours 0..15. Bold shows as the bright colour.
#
# Plain Ruby that mruby and CRuby both run (test/native/shell_term_test.rb
# feeds it on the host): bytes are read with getbyte and cut with byteslice,
# no Regexp, no pack.
class ShellTerm
  ESC = 27
  ST_GROUND = 0
  ST_ESC = 1
  ST_CSI = 2
  ST_OSC = 3
  ST_OSC_ESC = 4
  ST_CHARSET = 5
  TAB_WIDTH = 8

  attr_reader :cursor

  def initialize
    @state = ST_GROUND
    @params = ""
    @fg = -1
    @bg = -1
    @bold = false
    @attr = 0
    @pending = ""     # the start of a UTF-8 character cut by the read
    @done = []
    clear_line
  end

  # The finished lines since the last call (the Array is the caller's).
  def take_lines
    d = @done
    @done = []
    d
  end

  def lines_pending?
    !@done.empty?
  end

  # The line being written (no newline yet), as an entry, and the column the
  # next character goes to (in characters).
  def partial_entry
    entry_of(@text, @spans)
  end

  def partial_empty?
    @text.empty?
  end

  # The program ended: its last line, if it did not end with a newline.
  def finish
    newline unless @text.empty?
    take_lines
  end

  def feed(data)
    s = @pending.empty? ? data : @pending + data
    @pending = ""
    n = s.bytesize
    i = 0
    run = -1  # first byte of a run of printable bytes
    while i < n
      b = s.getbyte(i)
      if @state == ST_GROUND
        if b >= 32 && b != 127
          run = i if run < 0
          i += 1
          next
        end
        if run >= 0
          put_text(s.byteslice(run, i - run))
          run = -1
        end
        control(b)
      elsif @state == ST_ESC
        escape(b)
      elsif @state == ST_CSI
        if b >= 0x40 && b <= 0x7E
          sgr(@params) if b == 109  # 'm'
          @state = ST_GROUND
        elsif b >= 0x20
          @params << b.chr if @params.bytesize < 64
        else
          control(b)  # a control byte inside a sequence still acts
        end
      elsif @state == ST_OSC
        if b == 7
          @state = ST_GROUND
        elsif b == ESC
          @state = ST_OSC_ESC
        end
      elsif @state == ST_OSC_ESC
        @state = b == 92 ? ST_GROUND : ST_OSC  # ESC \ ends it
      else  # ST_CHARSET: the one byte after ESC ( ) * +
        @state = ST_GROUND
      end
      i += 1
    end
    if run >= 0
      cut = utf8_cut(s, run, n)
      put_text(s.byteslice(run, cut - run)) if cut > run
      @pending = s.byteslice(cut, n - cut) if cut < n
    end
    nil
  end

  private

  def clear_line
    @text = ""
    @spans = []
    @cursor = 0
  end

  # Where a run ending at n can be cut so that no UTF-8 character is split:
  # n, or the start of an unfinished last character.
  def utf8_cut(s, from, n)
    k = n - 1
    back = 0
    while k >= from && back < 4
      b = s.getbyte(k)
      if b < 0x80
        return n
      elsif b >= 0xC0
        need = b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2)
        return n - k >= need ? n : k
      end
      k -= 1
      back += 1
    end
    n
  end

  def control(b)
    if b == 10       # \n
      newline
    elsif b == 13    # \r: the line is written again from its start
      @cursor = 0
    elsif b == 8     # backspace
      @cursor -= 1 if @cursor > 0
    elsif b == 9     # tab
      put_text(" " * (TAB_WIDTH - @cursor % TAB_WIDTH))
    elsif b == ESC
      @state = ST_ESC
    end
    # BEL and the rest: nothing to show
  end

  def escape(b)
    if b == 91       # '['
      @state = ST_CSI
      @params = ""
    elsif b == 93    # ']'
      @state = ST_OSC
    elsif b == 40 || b == 41 || b == 42 || b == 43  # ( ) * +
      @state = ST_CHARSET
    else
      @state = ST_GROUND
    end
  end

  def newline
    @done << entry_of(@text, @spans)
    clear_line
  end

  def entry_of(text, spans)
    if spans.empty? || (spans.size == 1 && spans[0][1] == 0)
      return text
    end
    { :type => :ansi, :text => text, :spans => spans.dup }
  end

  # The SGR parameters "1;31" (empty means 0).
  def sgr(params)
    codes = []
    num = -1
    i = 0
    n = params.bytesize
    while i <= n
      b = i < n ? params.getbyte(i) : 59
      if b >= 48 && b <= 57
        num = (num < 0 ? 0 : num * 10) + (b - 48)
      elsif b == 59  # ';'
        codes << (num < 0 ? 0 : num)
        num = -1
      else
        return  # a private sequence (<, =, >, ?): not colours
      end
      i += 1
    end
    k = 0
    while k < codes.size
      c = codes[k]
      if c == 0
        @fg = -1
        @bg = -1
        @bold = false
      elsif c == 1
        @bold = true
      elsif c == 22
        @bold = false
      elsif c >= 30 && c <= 37
        @fg = c - 30
      elsif c == 39
        @fg = -1
      elsif c >= 40 && c <= 47
        @bg = c - 40
      elsif c == 49
        @bg = -1
      elsif c >= 90 && c <= 97
        @fg = c - 90 + 8
      elsif c >= 100 && c <= 107
        @bg = c - 100 + 8
      elsif c == 38 || c == 48
        # 38;5;n / 38;2;r;g;b: the 256 and true colours. The first 16 of
        # the 256 are the SGR ones; the rest keep the Shell's colour.
        if codes[k + 1] == 5
          v = codes[k + 2]
          v = (v && v < 16) ? v : -1
          if c == 38
            @fg = v
          else
            @bg = v
          end
          k += 2
        elsif codes[k + 1] == 2
          k += 4
        end
      end
      k += 1
    end
    fg = @fg
    fg += 8 if @bold && fg >= 0 && fg < 8
    @attr = (fg + 1) | ((@bg + 1) << 5)
  end

  # Write text at the cursor, over what is there.
  def put_text(str)
    len = str.length
    return if len == 0
    tlen = @text.length
    if @cursor == tlen
      if @spans.empty? ? @attr != 0 : @spans[-1][1] != @attr
        @spans << [tlen, @attr]
      end
      @text << str
      @cursor += len
      return
    end
    # Over existing characters (after \r or backspace): rebuild the line
    # one character at a time. Rare (progress lines), so kept simple.
    attrs = expand_attrs(tlen)
    chars = []
    i = 0
    while i < tlen
      chars << @text[i]
      i += 1
    end
    while chars.size < @cursor
      chars << " "
      attrs << 0
    end
    j = 0
    while j < len
      chars[@cursor + j] = str[j]
      attrs[@cursor + j] = @attr
      j += 1
    end
    @cursor += len
    @text = chars.join
    @spans = compress_attrs(attrs)
  end

  def expand_attrs(tlen)
    attrs = Array.new(tlen, 0)
    s = 0
    while s < @spans.size
      from = @spans[s][0]
      to = s + 1 < @spans.size ? @spans[s + 1][0] : tlen
      a = @spans[s][1]
      p = from
      while p < to
        attrs[p] = a
        p += 1
      end
      s += 1
    end
    attrs
  end

  def compress_attrs(attrs)
    spans = []
    last = 0
    i = 0
    while i < attrs.size
      if attrs[i] != last
        spans << [i, attrs[i]]
        last = attrs[i]
      end
      i += 1
    end
    spans
  end
end
