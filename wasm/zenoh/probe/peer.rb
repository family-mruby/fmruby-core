# The PC side of the Zenoh probe page (wasm/zenoh/probe/, doc/ruby_asterism
# B1). Run it with the CRuby gem asterism-zenoh, e.g. from the parent
# repository's asterism-console (whose bundle has it):
#
#   cd asterism-console && bundle exec ruby ../fmruby-core/wasm/zenoh/probe/peer.rb [seconds] [locator]
#
# It subscribes to what the page puts (b1/probe/from_browser) and to the
# page's answers (b1/probe/echo), puts "pc <n>" on b1/probe/to_browser once a
# second, and prints the round trip of each answer. Exit status 0 when it
# heard the page and, from the first answer on, every "pc <n>" came back (the
# ones sent before the page was connected have nobody to answer them).
require "asterism/zenoh"

seconds = (ARGV[0] || 20).to_f
locator = ARGV[1] || "tcp/localhost:7447"
s = Asterism::Zenoh::Session.open(locator)
from = s.subscribe("b1/probe/from_browser")
echo = s.subscribe("b1/probe/echo")
sent = {}
rtts = []
first_echo = nil
heard = 0
n = 0
t_end = Time.now + seconds
next_put = Time.now + 1
while Time.now < t_end
  break unless s.poll
  if Time.now >= next_put
    n += 1
    text = "pc #{n}"
    sent[text] = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    s.put("b1/probe/to_browser", text)
    next_put += 1
  end
  from.each_pending do |key, payload|
    heard += 1
    puts "from browser: #{payload}"
  end
  echo.each_pending do |key, payload|
    text = payload.sub(/\Aecho /, "")
    first_echo ||= text[/\d+/].to_i
    t0 = sent.delete(text)
    if t0
      rtt = (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0) * 1000
      rtts << rtt
      puts format("echo: %s (%.1f ms)", payload, rtt)
    else
      puts "echo: #{payload}"
    end
  end
  sleep 0.005
end
s.close
puts "heard #{heard} puts from the browser; #{rtts.size}/#{n} echoes" +
     (rtts.empty? ? "" : format(", round trip min %.1f / median %.1f / max %.1f ms",
                                rtts.min, rtts.sort[rtts.size / 2], rtts.max))
# The last put may still be on its way when the time runs out.
expected = first_echo ? n - first_echo + 1 : 1
exit(heard > 0 && first_echo && rtts.size >= expected - 1 ? 0 : 1)
