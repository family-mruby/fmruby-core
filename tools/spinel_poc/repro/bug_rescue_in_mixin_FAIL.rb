module Log
  def self.error(m); puts "E #{m}"; end
end
module M
  def handle(n)
    begin
      raise "boom" if n > 0
    rescue => e
      Log.error("h: #{e.class}: #{e.message}")
    end
  end
end
class K
  include M
end
K.new.handle(1)
puts "done"
