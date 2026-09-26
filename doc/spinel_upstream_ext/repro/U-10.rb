# The format string of sprintf is a call whose argument needs a GC root
# (a Symbol boxed into a poly parameter). The root's statement is emitted
# into the middle of the format temp's initializer.
module I18n
  STRINGS = { done: "done %d", fail: "fail" }
  def self.t(key)
    STRINGS[key] || key.to_s
  end
end
class App
  def initialize
    @status = "idle"
  end
  def run(res)
    if res && res[:ok]
      deleted = res[:deleted] || 0
      @status = sprintf(I18n.t(:done), deleted)
    else
      @status = I18n.t(:fail)
    end
    puts @status
  end
end
App.new.run({ ok: true, deleted: 3 })
App.new.run(nil)
I18n.t("x")
