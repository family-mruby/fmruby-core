# frozen_string_literal: true
#
# calibrate.rb - how far numbers move between runs of one build, and the
# tolerance that follows (rake regress:boot:calibrate, rake
# regress:run:calibrate; doc/regression/usage.md chapters 7-8). Plain
# arithmetic over the numbers of N runs: no device, no docker
# (test/regress/calibrate_test.rb feeds it made-up runs).
#
# The tolerances of RG3b (sim) and RG4 (boot) were set by hand from five runs
# of one build: about twice the largest spread seen, rounded up. The same
# rule here, per kind of number:
#
#   proposal = max(floor, ceil_to(spread * factor, unit))
#
# where spread is the largest (max - min) over every number of that kind.
# The floor covers what a few runs cannot show (a GC that happened to run
# in none of them, an allocation order that did not change); the unit is
# the granularity the number moves in (4 KB for a heap's largest block).
#
# The baseline candidate is the per-number median (the lower one for an even
# count, so it is a value that was actually seen) over all runs, written
# into the last run's record.
module Regress
  module Calibrate
    module_function

    def median(vals)
      s = vals.sort
      s[(s.size - 1) / 2]
    end

    def ceil_to(n, unit)
      return 0 if n <= 0
      ((n + unit - 1) / unit) * unit
    end

    def propose(spread, rule)
      [rule[:floor], ceil_to((spread * rule[:factor]).ceil, rule[:unit])].max
    end

    # items: { key => [kind, [v1, v2, ...]] } (one value per run that had
    # the number). Returns { kind => stats } for every kind in `rules`:
    #   items   how many numbers of this kind
    #   moved   how many of them moved at all
    #   partial how many were missing from some run
    #   spread  the largest max - min, and `where` the number it was
    #   proposal the tolerance this rule gives
    def measure(items, rules, runs)
      out = rules.keys.to_h { |k| [k, { "items" => 0, "moved" => 0, "partial" => 0, "spread" => 0, "where" => nil }] }
      items.each do |key, (kind, vals)|
        s = out[kind] or next
        s["items"] += 1
        s["partial"] += 1 if vals.size < runs
        next if vals.empty?
        d = vals.max - vals.min
        s["moved"] += 1 if d.positive?
        if d > s["spread"]
          s["spread"] = d
          s["where"] = key
        end
      end
      out.each { |kind, s| s["proposal"] = propose(s["spread"], rules[kind]) }
      out
    end

    # Collect { key => [kind, [values...]] } from runs, each flattened to
    # [[key, value, kind], ...] by the caller.
    def collect(flat_runs)
      items = {}
      flat_runs.each do |flat|
        flat.each do |key, val, kind|
          next if val.nil?
          (items[key] ||= [kind, []])[1] << val
        end
      end
      items
    end

    # The last run's record with every number under `keys` replaced by its
    # median over the runs (the same path in each run; a path missing from
    # a run is left out of its median).
    def median_record(runs, keys)
      base = Marshal.load(Marshal.dump(runs.last))
      keys.each do |k|
        next unless base.key?(k)
        base[k] = median_walk(base[k], runs.map { |r| r[k] })
      end
      base
    end

    def median_walk(node, others)
      case node
      when Hash
        node.to_h do |k, v|
          [k, median_walk(v, others.filter_map { |o| o[k] if o.is_a?(Hash) && o.key?(k) })]
        end
      when Numeric
        vals = others.grep(Numeric)
        vals.empty? ? node : median(vals)
      else
        node
      end
    end

    def fmt(n)
      return n.to_s unless n.is_a?(Integer)
      s = n.abs.to_s.reverse.scan(/\d{1,3}/).join(",").reverse
      n.negative? ? "-#{s}" : s
    end

    # The summary table: one line per kind, a "*" where the proposal differs
    # from the tolerance in force.
    #   now:  { kind => "1,024" } (the tolerance in force, as text)
    #   shown: { kind => text } the proposal as text (default: its number)
    def table(stats, now, shown = {})
      lines = [format("  %-12s %5s %5s  %-38s %-14s %s", "kind", "items", "moved", "spread (where)", "now", "proposal")]
      stats.each do |kind, s|
        where = s["where"] ? "#{fmt(s['spread'])} (#{s['where']})" : fmt(s["spread"])
        where = "#{where[0, 35]}..." if where.size > 38
        prop = shown[kind] || fmt(s["proposal"])
        lines << format("  %-12s %5d %5d  %-38s %-14s %s%s", kind, s["items"], s["moved"], where, now[kind], prop,
                        now[kind] == prop ? "" : "  *")
      end
      partial = stats.sum { |_, s| s["partial"] }
      lines << "  #{partial} numbers were missing from some run (left out of their spread's runs)" if partial.positive?
      lines
    end
  end
end
