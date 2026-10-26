# frozen_string_literal: true
#
# steps.rb - a step sensor on Asterism, played by tool/imu/step_send.rb (the
# CRuby sender the step sensor app's readers are tried with), with
# --measure: it also listens to the progress walk_scroller puts after the
# frame that shows a step, so the counterpart sees what the screen shows.
#
#   st = s.counterpart :steps
#   st.walk(10, cadence: 120)   # blocks until the sender is done
#   st.state  # {"sent" => 10, "shown" => [[steps, count, mark], ...]}
require "open3"

module Regress
  module Counterparts
    class Steps < Base
      NODE = "rgsteps"
      SENDER = File.join(CORE, "tool", "imu", "step_send.rb")

      def start
        Counterparts.wait_for("zenohd on 127.0.0.1:7447", 30) { Counterparts.listening?(7447) }
        @sent = 0
        @shown = []
      end

      # count steps at cadence steps a minute, after the "state" value that
      # tells a reader where the total starts (the sender's --wait).
      def walk(count, cadence: 120)
        cmd = [Gem.ruby, SENDER, "--router", ROUTER, "--node", NODE, "--count", count.to_s,
               "--cadence", cadence.to_s, "--start", @sent.to_s, "--measure"]
        out, st = Open3.capture2e(*cmd, chdir: CORE)
        File.write(File.join(@dir, "step_send.log"), out, mode: "a")
        raise Error, "steps: step_send.rb failed: #{out.lines.last(3).join.strip}" unless st.success?
        out.scan(/shown on \S+: steps (\d+) -> count (\d+), mark (\d+)/) do |s, c, m|
          @shown << [s.to_i, c.to_i, m.to_i]
        end
        @sent += count
        self
      end

      def state = { "sent" => @sent, "shown" => @shown }
    end

    register :steps, Steps
  end
end
