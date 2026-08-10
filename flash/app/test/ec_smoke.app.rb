# EditorCore smoke test: exercises the document API and logs each result, so the
# gem can be validated without driving the editor UI.
class EcSmokeApp < FmrbApp
  def on_create
    @done = false
    Log.info("EC: start")
    run_checks
    @done = true
  end

  def ck(name, got, want)
    ok = (got == want)
    Log.info("EC: #{ok ? 'ok  ' : 'FAIL'} #{name}: got=#{got.inspect} want=#{want.inspect}")
  end

  def run_checks
    EditorCore.reset
    ck("empty count", EditorCore.line_count, 1)
    ck("empty len", EditorCore.line_length(0), 0)

    # insert / render
    nx = EditorCore.insert_text(0, 0, "hello")
    ck("insert ret", nx, 5)
    ck("len", EditorCore.line_length(0), 5)
    ck("render", EditorCore.render_text(0, 0, 80), "hello")
    ck("render window", EditorCore.render_text(0, 1, 3), "ell")
    ck("char_at", EditorCore.char_at(0, 1), "e")
    ck("char_at eol", EditorCore.char_at(0, 5), "")

    # multibyte columns are characters, not bytes
    EditorCore.reset
    EditorCore.insert_text(0, 0, "あiう")
    ck("mb len", EditorCore.line_length(0), 3)
    ck("mb char_at", EditorCore.char_at(0, 2), "う")
    ck("mb render", EditorCore.render_text(0, 1, 2), "iう")
    EditorCore.delete_char(0, 0)
    ck("mb after delete", EditorCore.render_text(0, 0, 80), "iう")

    # split / join
    EditorCore.reset
    EditorCore.insert_text(0, 0, "abcdef")
    EditorCore.split_line(0, 3)
    ck("split count", EditorCore.line_count, 2)
    ck("split l0", EditorCore.render_text(0, 0, 80), "abc")
    ck("split l1", EditorCore.render_text(1, 0, 80), "def")
    ck("join ret", EditorCore.join_line(0), 3)
    ck("join count", EditorCore.line_count, 1)
    ck("join text", EditorCore.render_text(0, 0, 80), "abcdef")

    # multiline insert
    EditorCore.reset
    EditorCore.insert_text(0, 0, "XY")
    rec = EditorCore.insert_multiline(0, 1, "1\n22\n333")
    ck("ml count", EditorCore.line_count, 3)
    ck("ml y", EditorCore.pos_y(rec), 2)
    ck("ml x", EditorCore.pos_x(rec), 3)
    ck("ml l0", EditorCore.render_text(0, 0, 80), "X1")
    ck("ml l2", EditorCore.render_text(2, 0, 80), "333Y")

    # delete_range across lines
    EditorCore.reset
    EditorCore.insert_multiline(0, 0, "aaa\nbbb\nccc")
    EditorCore.delete_range(0, 1, 2, 1)
    ck("dr count", EditorCore.line_count, 1)
    ck("dr text", EditorCore.render_text(0, 0, 80), "acc")

    # clipboard
    EditorCore.reset
    EditorCore.insert_multiline(0, 0, "one\ntwo\nthree")
    len = EditorCore.copy_range(0, 1, 1, 2)
    ck("copy len", len, 5)  # "ne" + "\n" + "tw"
    ck("clip len", EditorCore.clipboard_length, 5)
    rec = EditorCore.paste_at(2, 0)
    ck("paste count", EditorCore.line_count, 4)
    ck("paste text l2", EditorCore.render_text(2, 0, 80), "ne")
    ck("paste text l3", EditorCore.render_text(3, 0, 80), "twthree")

    # find
    EditorCore.reset
    EditorCore.insert_multiline(0, 0, "alpha\nbeta\ngamma beta")
    r = EditorCore.find("beta", 0, 0, false)
    ck("find hit", EditorCore.found?(r), true)
    ck("find y", EditorCore.find_y(r), 1)
    ck("find x", EditorCore.find_x(r), 0)
    r2 = EditorCore.find("beta", 1, 0, true)
    ck("find next y", EditorCore.find_y(r2), 2)
    ck("find next x", EditorCore.find_x(r2), 6)
    r3 = EditorCore.find("nope", 0, 0, false)
    ck("find miss", EditorCore.found?(r3), false)

    # highlight map: one byte per character, keyword coloured
    EditorCore.reset
    EditorCore.hl_enabled = true
    EditorCore.insert_text(0, 0, "def foo")
    hl = EditorCore.render_hl(0, 0, 80)
    ck("hl len", hl.length, 7)
    ck("hl keyword", hl.getbyte(0), 1)
    EditorCore.hl_enabled = false
    ck("hl off", EditorCore.render_hl(0, 0, 80).length, 0)
    EditorCore.hl_enabled = true

    # file round trip
    EditorCore.reset
    EditorCore.insert_multiline(0, 0, "line1\nline2\nline3")
    n = EditorCore.save_file("/home/ec_smoke_out.rb")
    ck("save bytes", n, 17)
    ck("doc bytesize", EditorCore.doc_bytesize, 17)
    EditorCore.reset
    ck("load count", EditorCore.load_file("/home/ec_smoke_out.rb"), 3)
    ck("load l1", EditorCore.render_text(1, 0, 80), "line2")
    ck("load missing", EditorCore.load_file("/home/definitely_absent.rb"), -3)

    Log.info("EC: mem_used=#{EditorCore.mem_used}")
    Log.info("EC: done")
  end

  def on_update
    stop if @done
    100
  end
end

begin
  EcSmokeApp.new.start
rescue => e
  Log.error("EC: exception #{e.class}: #{e.message}")
end
