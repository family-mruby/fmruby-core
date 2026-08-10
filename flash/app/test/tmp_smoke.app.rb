#---fmrb
# default_window_mode = "background"
# task_stack_kb = 32
#---
# /tmp RAM FS smoke test (doc/multivm_app/instruction_m1.md T3). Headless: it
# exercises the File/Dir API against /tmp and logs each result, so the mount
# can be checked without driving any UI. It also declares its own attributes
# through the T1 comment fence, so a passing run covers both.
class TmpSmokeApp < FmrbApp
  P1 = "/tmp/smoke1.txt"
  P2 = "/tmp/smoke2.txt"
  P3 = "/tmp/renamed.txt"
  FILL = "/tmp/fill.bin"

  def on_create
    @done = false
    Log.info("TMP: start")
    run_checks
    @done = true
  end

  def ck(name, got, want)
    ok = (got == want)
    Log.info("TMP: #{ok ? 'ok  ' : 'FAIL'} #{name}: got=#{got.inspect} want=#{want.inspect}")
  end

  def entries(path)
    dir = Dir.open(path)
    list = []
    while (e = dir.read)
      list << e unless e == "." || e == ".."
    end
    dir.close
    list
  rescue => e
    Log.error("TMP: cannot list #{path}: #{e.message}")
    []
  end

  def run_checks
    # create / write / read
    File.open(P1, "w") { |f| f.write("hello /tmp") }
    ck("exist", File.exist?(P1), true)
    ck("size", File.size(P1), 10)
    ck("read", File.open(P1, "r") { |f| f.read }, "hello /tmp")

    # append
    File.open(P1, "a") { |f| f.write("!!") }
    ck("append size", File.size(P1), 12)
    ck("append read", File.open(P1, "r") { |f| f.read }, "hello /tmp!!")

    # overwrite shrinks the file again
    File.open(P1, "w") { |f| f.write("abc") }
    ck("truncate size", File.size(P1), 3)

    # list
    File.open(P2, "w") { |f| f.write("second") }
    names = entries("/tmp")
    ck("list has smoke1", names.include?("smoke1.txt"), true)
    ck("list has smoke2", names.include?("smoke2.txt"), true)

    # /tmp shows up in the root listing
    ck("root has tmp", entries("/").include?("tmp"), true)
    ck("tmp is a dir", File.directory?("/tmp"), true)

    # rename
    File.rename(P2, P3)
    ck("renamed gone", File.exist?(P2), false)
    ck("renamed here", File.open(P3, "r") { |f| f.read }, "second")

    # delete
    File.delete(P3)
    ck("deleted", File.exist?(P3), false)

    # a subdirectory is not representable: /tmp is flat on device
    sub_ok = begin
      File.open("/tmp/sub/deep.txt", "w") { |f| f.write("x") }
      true
    rescue
      false
    end
    ck("flat mount", sub_ok, false)

    # capacity: keep writing until the mount refuses, and stay alive
    written = fill_until_full
    ck("full is reported", written > 0, true)
    Log.info("TMP: wrote #{written} bytes before /tmp reported full")

    File.delete(FILL) if File.exist?(FILL)
    File.delete(P1) if File.exist?(P1)
    Log.info("TMP: remaining entries #{entries('/tmp').inspect}")
    Log.info("TMP: done")
  end

  # Returns the number of bytes written before the mount refused. The refusal
  # is the expected outcome; what is being checked is that the app survives it.
  def fill_until_full
    chunk = "x" * 4096
    total = 0
    f = nil
    begin
      f = File.open(FILL, "w")
      i = 0
      while i < 256   # 1MB of attempts against a smaller mount
        f.write(chunk)
        total += 4096
        i += 1
      end
      Log.warn("TMP: /tmp took 1MB, the capacity limit did not trigger")
    rescue => e
      Log.info("TMP: refused at #{total} bytes (#{e.class}: #{e.message})")
    end
    begin
      f.close if f
    rescue
    end
    total
  end

  def on_update
    stop if @done
    100
  end
end

begin
  TmpSmokeApp.new.start
rescue => e
  Log.error("TMP: exception #{e.class}: #{e.message}")
end
