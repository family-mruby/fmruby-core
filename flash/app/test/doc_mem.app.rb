# Where a 20KB document lives: the old model (Array of Strings on the VM's own
# mruby pool) versus EditorCore (flat arena in POOL_ID_EDITOR_DOC). Both are
# measured in one VM with the same 500KB pool, so the numbers are comparable.
class DocMemApp < FmrbApp
  PATH = "/home/mid20k.rb"

  def on_create
    @done = false
    Log.info("DOCMEM: baseline pool=#{FmrbApp.pool_usage}%")

    # New model: the document is read into the C arena.
    n = EditorCore.load_file(PATH)
    Log.info("DOCMEM: editor_core lines=#{n} bytes=#{EditorCore.doc_bytesize} " \
             "arena=#{EditorCore.mem_used} pool=#{FmrbApp.pool_usage}%")
    EditorCore.reset
    Log.info("DOCMEM: after reset pool=#{FmrbApp.pool_usage}% arena=#{EditorCore.mem_used}")

    # Old model: whole file as a String, then an Array of line Strings, which is
    # exactly what the editor held before P4.
    begin
      f = File.open(PATH, "r")
      content = f.read
      f.close
      lines = content.split("\n")
      Log.info("DOCMEM: ruby_lines lines=#{lines.length} bytes=#{content.bytesize} " \
               "pool=#{FmrbApp.pool_usage}%")
      # Touch it so nothing is optimized away, then drop it.
      Log.info("DOCMEM: first=#{lines[0]} last_len=#{lines[-1].length}")
      lines = nil
      content = nil
    rescue => e
      Log.error("DOCMEM: ruby model failed: #{e.class}: #{e.message}")
    end
    @done = true
  end

  def on_update
    stop if @done
    100
  end
end

begin
  DocMemApp.new.start
rescue => e
  Log.error("DOCMEM: exception #{e.class}: #{e.message}")
end
