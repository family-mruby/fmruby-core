# A movable app that keeps a file open (doc/checkpoint F3): an open File is C
# data the image cannot carry yet (its handle is this program's), so the
# request is refused at the safe point and the app goes on counting. The
# checkpoint check (tools/regress/checkpoint.rb) reads the refusal and the
# next count.

class CkptFileApp < FmrbApp
  def on_create
    @n = 0
    @f = File.open("/tmp/ckpt_file.txt", "w")
  end

  def on_update
    @n += 1
    @f.write("n=#{@n}\n")
    Log.info("ckpt_file: n=#{@n}")
    250
  end

  def on_destroy
    @f.close if @f
  end
end

begin
  app = CkptFileApp.new
  app.start
rescue => e
  Log.error("ckpt_file: #{e.class}: #{e.message}")
end
