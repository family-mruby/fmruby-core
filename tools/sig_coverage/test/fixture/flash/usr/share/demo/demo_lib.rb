# A library the apps load: its classes are not gaps.
class LibThing
  def self.go = 1
end

module Msgs
  # Nested: does not stand for a top-level Clock.
  class Clock
    def self.now = 0
  end
end
