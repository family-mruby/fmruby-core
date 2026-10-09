# Ruby profile runner: parses probes.txt and runs each probe on the Ruby it
# is loaded in. The same file runs on CRuby, on mruby (the `mruby` command)
# and in Family mruby's app VM (the PicoRuby compiler plus the mruby VM), so
# it keeps to what all of them have: no Regexp, no defined?, while loops,
# byte-level string work, and `eval` to compile each probe on its own (a
# probe the compiler rejects then fails alone, as an exception from eval).
#
#   probes = RubyProfile.parse(text)        # text of probes.txt
#   runner = RubyProfile::Runner.new(probes)
#   runner.size.times { |i| puts runner.line(i) }
#
# A line is "RPROF|<id>|<status>|<detail>": status is ok, differs, ng or
# value (a measurement); detail is the answer (differs), the exception (ng)
# or the number (value). RubyProfile.header / footer frame a run.
module RubyProfile
  VERSION = 1
  DETAIL_MAX = 72

  class Probe
    attr_reader :id, :area, :title, :expect, :mrblib, :kind, :code

    def initialize(fields, code)
      @id = fields["id"].to_s
      @area = fields["area"].to_s
      @title = fields["title"].to_s
      @expect = fields["expect"].to_s
      @mrblib = fields["mrblib"].to_s
      @kind = fields["kind"].to_s
      @code = code
    end

    def measure?
      @kind == "measure"
    end
  end

  # probes.txt -> [Probe]
  def self.parse(text)
    probes = []
    fields = nil
    code = nil
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i]
      i += 1
      if line.start_with?("## ")
        sep = line.index(": ")
        key = sep ? line.byteslice(3, sep - 3) : ""
        val = sep ? line.byteslice(sep + 2, line.bytesize - sep - 2) : ""
        if key == "id"
          probes << Probe.new(fields, code) if fields
          fields = {}
          code = "".dup
        end
        fields[key] = val if fields
      elsif fields
        code << line << "\n"
      end
    end
    probes << Probe.new(fields, code) if fields
    probes
  end

  def self.header(impl, count)
    "RPROF-BEGIN|#{impl}|#{count}|v#{VERSION}"
  end

  def self.footer(impl, counts)
    "RPROF-END|#{impl}|ok=#{counts['ok']}|differs=#{counts['differs']}|ng=#{counts['ng']}|value=#{counts['value']}"
  end

  # One line of log-safe text: no "|" or line breaks, at most DETAIL_MAX
  # bytes (cut only before a byte below 0x80, so a character is not split).
  def self.clean(s)
    s = s.to_s
    out = "".dup
    i = 0
    n = s.bytesize
    while i < n
      b = s.getbyte(i)
      if out.bytesize >= DETAIL_MAX && b < 0x80
        out << "..."
        break
      end
      if b == 124 || b == 10 || b == 13
        out << " "
      else
        out << s.byteslice(i, 1)
      end
      i += 1
    end
    out
  end

  class Runner
    attr_reader :probes, :counts

    def initialize(probes)
      @probes = probes
      @counts = {"ok" => 0, "differs" => 0, "ng" => 0, "value" => 0}
    end

    def size
      @probes.size
    end

    # [id, status, detail] for probe i.
    def run(i)
      pr = @probes[i]
      status = nil
      detail = ""
      begin
        got = rp_eval(pr.code)
        if pr.measure?
          status = "value"
          detail = got.is_a?(Numeric) ? ((got * 10000).round / 10000.0).to_s : got.inspect
        else
          want = rp_eval(pr.expect)
          if got == want
            status = "ok"
          else
            status = "differs"
            detail = got.inspect
          end
        end
      rescue Exception => e
        status = "ng"
        detail = "#{e.class}: #{e.message}"
      end
      @counts[status] += 1
      [pr.id, status, RubyProfile.clean(detail)]
    end

    def line(i)
      r = run(i)
      "RPROF|#{r[0]}|#{r[1]}|#{r[2]}"
    end

    # Microseconds from some fixed point, on whatever clock this Ruby has.
    def rp_clock_us
      begin
        return Process.clock_gettime(Process::CLOCK_MONOTONIC, :microsecond)
      rescue Exception
      end
      begin
        return Machine.board_millis * 1000
      rescue Exception
      end
      (Time.now.to_f * 1_000_000).to_i
    end

    # Microseconds per operation: yields a count, times the block, and
    # quadruples the count until one run takes 100 ms (so a clock with
    # millisecond steps is still precise to 1%).
    def rp_per_op_us
      count = 1000
      while true
        t0 = rp_clock_us
        yield count
        dt = rp_clock_us - t0
        return dt.to_f / count if dt >= 100_000 || count >= 100_000_000
        count *= 4
      end
    end

    private

    # The probe's own scope: few locals, so a probe's names do not collide.
    def rp_eval(rp_src)
      eval(rp_src)
    end
  end
end
