#---fmrb
# default_window_mode = "background"
# task_stack_kb = 32
#---
# A headless worker: no window, no keyboard, just work. This is the shape to
# reach for when the main app should stay responsive while something slow
# happens -- map generation, a search, a file conversion.
#
# The result goes to /tmp (RAM, gone at reboot) and only its path is published,
# because a message caps out at 176 bytes. Whoever is listening on the topic
# reads the file.
#
# Work in slices and return from on_update between them: this task shares the
# CPU with the app that started it.
class MyApp < FmrbApp
  TOPIC = "worker"
  OUT = "/tmp/worker_result.txt"
  SLICES = 10

  def on_create
    @slice = 0
    @total = 0
    Log.info("worker: start")
  end

  # One slice of the real work. Keep it short.
  def work_slice(n)
    sum = 0
    i = 0
    while i < 1000
      sum += (n * 1000 + i) % 7
      i += 1
    end
    sum
  end

  def finish
    File.open(OUT, "w") { |f| f.write("total=#{@total}\nslices=#{SLICES}\n") }
    publish(TOPIC, { "path" => OUT, "ok" => true })
    Log.info("worker: wrote #{OUT} (total=#{@total})")
  rescue => e
    publish(TOPIC, { "path" => OUT, "ok" => false })
    Log.error("worker: #{e.class}: #{e.message}")
  end

  def on_update
    if @slice < SLICES
      @total += work_slice(@slice)
      @slice += 1
      return 10
    end
    finish
    stop
    100
  end

  def on_destroy
    Log.info("worker: done")
  end
end

# An app file that stops here loads and does nothing: the class has to be
# instantiated and started.
begin
  MyApp.new.start
rescue => e
  Log.error("MyApp: #{e.class}: #{e.message}")
end
