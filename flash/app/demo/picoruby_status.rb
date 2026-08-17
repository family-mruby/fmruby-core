# The System page of picoruby.app.rb, in a file of its own.
#
# This is here to be required: an app can be split across files, and the
# required one is read from the path given (the kernel limits an app to /app
# and /home).
#
# Unlike a Python module, a required Ruby file shares the app's namespace:
# FmrbApp, FmrbGfx and Log are all visible in here. It is still handed the app
# rather than reaching for it, so the two demos read the same way and this file
# can be understood on its own.

module PicoRubyStatus
  # Text for the System page: [label, value] pairs, in drawing order.
  def self.rows(app)
    got = app.received
    [
      ["uptime s", (app.uptime / 1000).to_s],
      ["language", app.lang.to_s],
      ["file read", "#{app.toml_bytes} bytes"],
      ["random", random_digits(4).to_s],
      ["published", app.sent.to_s],
      ["received", got ? got["n"].to_s : "-"],
      ["from", got ? got["msg"].to_s : "-"],
      ["list", got ? got["list"].to_s : "-"],
    ]
  end

  # 4.times.map would need an Enumerator; picoruby has none, so the list is
  # built by hand.
  def self.random_digits(n)
    out = []
    i = 0
    while i < n
      out << (RNG.random_int % 10)
      i += 1
    end
    out
  end
end
