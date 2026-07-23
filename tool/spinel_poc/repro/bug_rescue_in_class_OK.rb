module Log
  def self.error(m); puts "E #{m}"; end
end
class K
  def handle(n)
    begin
      raise "boom" if n > 0
    rescue => e
      Log.error("h: #{e.class}: #{e.message}")
    end
  end
end
K.new.handle(1)
puts "done"
