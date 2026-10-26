# frozen_string_literal: true
#
# calibrate_test.rb - the arithmetic of rake regress:boot:calibrate and
# regress:run:calibrate (tools/regress/calibrate.rb) on made-up runs. No
# device, no docker:
#   ruby test/regress/calibrate_test.rb   (rake regress:boot:test, part of rake test)
require "minitest/autorun"
require_relative "../../tools/regress/calibrate"
require_relative "../../tools/regress/boot_log"
require_relative "../../tools/regress/sim_run"

class CalibrateTest < Minitest::Test
  C = Regress::Calibrate
  BL = Regress::BootLog

  def test_median_is_a_seen_value
    assert_equal 3, C.median([5, 1, 3])
    assert_equal 2, C.median([4, 1, 2, 3]), "the lower of the two middle ones"
  end

  def test_proposal_rule
    rule = { factor: 2, unit: 256, floor: 512 }
    assert_equal 512, C.propose(0, rule), "floor when nothing moved"
    assert_equal 1024, C.propose(436, rule), "872 rounded up to 256"
    assert_equal 1024, C.propose(512, rule)
    assert_equal 1280, C.propose(513, rule)
  end

  # The RG4 measurements (five boots of one P4-Nano firmware) give back the
  # tolerances that were chosen by hand then.
  def test_boot_rules_reproduce_rg4
    spreads = { "m1_internal" => 436, "m1_largest" => 8192, "m1_psram" => 0, "iram_free" => 440, "stack_free" => 144 }
    got = spreads.to_h { |k, v| [k, C.propose(v, BL::CALIBRATE.fetch(k))] }
    assert_equal BL::TOLERANCE, got
  end

  # The RG3b measurements give back the sim tolerances' absolute parts
  # (stack: 276 B seen, 512 chosen then; the rule now says 768).
  def test_sim_rules_against_rg3b
    rules = Regress::SimRun::CALIBRATE
    assert_equal 512, C.propose(0, rules["app_used"])
    assert_equal 512, C.propose(88, rules["app_used"])
    assert_equal 4096, C.propose(1928, rules["gc_used"])
    assert_equal 256, C.propose(0, rules["compile"])
    assert_equal 768, C.propose(276, rules["stack_free"])
  end

  def test_measure_finds_the_widest_number
    items = C.collect([
      [["a", 100, "k"], ["b", 50, "k"], ["s", 10, "j"]],
      [["a", 120, "k"], ["b", 50, "k"], ["s", 10, "j"]],
      [["a", 110, "k"], ["s", 10, "j"]]
    ])
    stats = C.measure(items, { "k" => { factor: 2, unit: 16, floor: 16 }, "j" => { factor: 2, unit: 16, floor: 16 } }, 3)
    assert_equal({ "items" => 2, "moved" => 1, "partial" => 1, "spread" => 20, "where" => "a", "proposal" => 48 }, stats["k"])
    assert_equal 0, stats["j"]["moved"]
    assert_equal 16, stats["j"]["proposal"]
  end

  def test_median_record_walks_the_numbers_only
    runs = [
      { "m1" => { "x" => { "internal" => 10, "largest" => 4 } }, "iram_free" => 7, "version" => "v1" },
      { "m1" => { "x" => { "internal" => 30, "largest" => 4 } }, "iram_free" => 5, "version" => "v1" },
      { "m1" => { "x" => { "internal" => 20, "largest" => 8 }, "y" => { "internal" => 1, "largest" => 1 } },
        "iram_free" => 6, "version" => "v2" }
    ]
    rec = C.median_record(runs, %w[m1 iram_free])
    assert_equal 20, rec["m1"]["x"]["internal"]
    assert_equal 4, rec["m1"]["x"]["largest"]
    assert_equal 6, rec["iram_free"]
    assert_equal({ "internal" => 1, "largest" => 1 }, rec["m1"]["y"], "only in the last run: its own value")
    assert_equal "v2", rec["version"], "text comes from the last run"
    assert_equal 10, runs[0]["m1"]["x"]["internal"], "the runs are not changed"
  end

  # Three made-up boots through the real log reader: the spread per kind,
  # the median baseline, and that every boot passes against it with the
  # proposed tolerance.
  def boot_log(internal, largest, iram, led)
    <<~LOG
      [  0.701] rst:0x1 (POWERON),boot:0x30f (SPI_FAST_FLASH_BOOT)
      [  3.104] I (1) fmrb_alloc: M1|boot_start|internal=505244|largest=442368|psram=15403060
      [  5.208] I (2) fmrb_alloc: M1|wifi_rd|internal=#{internal}|largest=#{largest}|psram=11541844
      [  9.714] I (3) status_led: Boot complete: switching to heartbeat
      I (4) fmrb_task: --- Task Status (2 tracked) ---
      I (4) fmrb_task: hw_proxy           Y    0  6144  4300 IRAM  PIN0
      I (4) fmrb_task: status_led         Y    1  4096  #{led} IRAM  PIN1
      I (4) fmrb_task: IRAM free: #{iram} bytes, PSRAM free: 11000000 bytes
    LOG
  end

  def test_boot_runs_end_to_end
    runs = [boot_log(212_636, 172_032, 208_600, 1508), boot_log(212_200, 176_128, 209_040, 1428),
            boot_log(212_500, 172_032, 208_800, 1500)].map { |l| BL.parse(l) }
    assert(runs.all? { |r| BL.clean?(r) })
    stats = C.measure(C.collect(runs.map { |r| BL.flat(r) }), BL::CALIBRATE, runs.size)
    assert_equal 436, stats["m1_internal"]["spread"]
    assert_equal "M1|wifi_rd internal", stats["m1_internal"]["where"]
    assert_equal 1024, stats["m1_internal"]["proposal"]
    assert_equal 4096, stats["m1_largest"]["spread"]
    assert_equal 8192, stats["m1_largest"]["proposal"]
    assert_equal 440, stats["iram_free"]["spread"]
    assert_equal 80, stats["stack_free"]["spread"]
    assert_equal 256, stats["stack_free"]["proposal"]
    assert_equal 1, stats["m1_internal"]["moved"], "boot_start did not move"

    base = C.median_record(runs, BL::MEASURED)
    assert_equal 212_500, base["m1"]["wifi_rd"]["internal"]
    assert_equal 208_800, base["iram_free"]
    tol = stats.transform_values { |s| s["proposal"] }
    runs.each { |r| assert_empty BL.compare(r, base, tol)[:fails] }
  end

  def test_unclean_boot_is_seen
    r = BL.parse(boot_log(1, 1, 1, 1) + "Guru Meditation Error: Core 1 panic'ed\n")
    refute BL.clean?(r)
  end

  def test_table_marks_a_changed_proposal
    stats = { "a" => { "items" => 3, "moved" => 1, "partial" => 0, "spread" => 300, "where" => "x", "proposal" => 768 },
              "b" => { "items" => 2, "moved" => 0, "partial" => 0, "spread" => 0, "where" => nil, "proposal" => 512 } }
    lines = C.table(stats, { "a" => "512", "b" => "512" })
    assert_equal 3, lines.size
    assert lines[1].end_with?("768  *")
    assert lines[2].end_with?("512")
  end

  # The sim side: tolerance per family over the file's default.
  def test_sim_family_tolerance_overrides
    doc = { "tolerance" => Regress::SimRun::TOLERANCE,
            "families" => { "modern" => { "tolerance" => { "stack_free" => [0.0, 768] } } } }
    assert_equal [0.0, 768], Regress::SimRun.tolerance(doc, "modern")["stack_free"]
    assert_equal [0.0, 512], Regress::SimRun.tolerance(doc, "retro")["stack_free"]
    assert_equal [0.03, 4096], Regress::SimRun.tolerance(doc, "modern")["gc_used"]
  end
end
