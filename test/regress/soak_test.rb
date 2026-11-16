# frozen_string_literal: true
#
# soak_test.rb - the judging and the periodic-dump reader of rake
# regress:soak (tools/regress/soak.rb) against made-up numbers and logs. No
# sim, no device:
#   ruby test/regress/soak_test.rb   (rake regress:boot:test, part of rake test)
require "minitest/autorun"
require_relative "../../tools/regress/soak"

class SoakTest < Minitest::Test
  S = Regress::Soak

  def test_slope
    assert_in_delta 100.0, S.slope([0, 100, 200, 300]), 0.001
    assert_in_delta 0.0, S.slope([5, 5, 5]), 0.001
  end

  def test_settled_after_warmup
    # grows in the warm-up, then moves by a few hundred bytes
    j = S.judge([150_000, 170_000, 176_000, 176_500, 176_200, 176_700, 176_300, 176_600], 3)
    assert_equal "settled", j["verdict"]
  end

  def test_grows
    j = S.judge([150_000, 170_000, 176_000] + (0..6).map { |i| 176_000 + i * 600 + (i.odd? ? 200 : 0) }, 3)
    assert_equal "grows", j["verdict"]
    assert_operator j["slope"], :>=, 500
    assert_operator j["floor_rise"], :>=, 1024
  end

  def test_sawtooth_with_a_level_floor_is_settled
    # the Modern kernel's shape: dead strings climb, a sweep takes them
    vals = [196_568, 206_872, 215_664, 223_000, 228_520, 190_672, 194_464, 191_480, 197_472, 201_048]
    assert_equal "settled", S.judge(vals, 3, sawtooth: true)["verdict"]
  end

  def test_sawtooth_with_a_rising_floor_grows
    # a sweep every third round, the floor 6 KB higher each time
    vals = (0..14).map { |i| 200_000 + i * 2000 + (i % 3) * 8000 }
    j = S.judge(vals, 3, sawtooth: true)
    assert_equal "grows", j["verdict"]
  end

  def test_one_jump_in_a_short_soak_is_undecided
    # Retro's kernel over 10 rounds: one jump, then a slow slide (a cycle of 11)
    vals = [173_304, 174_912, 175_112, 177_248, 174_888, 173_992, 218_840, 216_904, 212_912, 196_256]
    assert_equal "undecided", S.judge(vals, 3, sawtooth: true)["verdict"]
  end

  def test_a_climb_with_no_fall_is_undecided_on_spinel_only
    vals = (0..9).map { |i| 200_000 + i * 3000 }
    assert_equal "undecided", S.judge(vals, 3, sawtooth: true)["verdict"]
    assert_equal "grows", S.judge(vals, 3)["verdict"]
  end

  def test_small_steady_growth_under_the_net_is_settled
    j = S.judge([0, 0, 0] + (0..6).map { |i| i * 130 }, 3)
    assert_equal "settled", j["verdict"] # 780 B over the soak: under 1 KB
  end

  def test_free_memory_falling_grows
    j = S.judge([200_000, 199_000, 198_000] + (0..6).map { |i| 198_000 - i * 512 }, 3, good: :up)
    assert_equal "grows", j["verdict"]
    j = S.judge([200_000, 199_000, 198_000] + (0..6).map { |i| 198_000 + i * 512 }, 3, good: :up)
    assert_equal "settled", j["verdict"]
  end

  def test_short
    assert_equal "short", S.judge([1, 2, 3, 4], 3)["verdict"]
  end

  def dump(iram, kernel, wasm: true)
    log = +<<~LOG
      I (09:00:11.000) fmrb_task: --- Task Status (3 tracked) ---
      I (09:00:11.001) fmrb_task: hw_proxy           Y    0  6144  4300 IRAM  PIN0
      I (09:00:11.004) fmrb_task: IRAM free: #{iram} bytes, PSRAM free: 11000000 bytes
      I (09:00:11.005) fmrb_task: --------------------------------
      I (09:00:11.010) fmrb_app: --- VM Pools ---
      I (09:00:11.010) fmrb_app: Name               VM     Used     Free    Total     Peak  Largest  Frag  ExcHW
      I (09:00:11.011) fmrb_app: fmrb_kernel       spx   #{kernel}   410248   512000   127056   385264   31%    3/0
      I (09:00:11.012) fmrb_app: Services          mrb   400000   648576  1048576   420000   600000   10%      -
      I (09:00:11.013) fmrb_app: ----------------
    LOG
    log << "I (09:00:11.014) fmrb_wasm: runtime off apps=0 modules=0 natives=124 pool used=0 high=40960 total=1048576 traps=0\n" if wasm
    log
  end

  def test_parse_dumps_and_least
    text = "noise\n" + dump(208_000, 101_000) + dump(208_400, 100_500, wasm: false) +
           "I (09:00:21.000) fmrb_task: --- Task Status (3 tracked) ---\n" # cut short
    dumps = S.parse_dumps(Regress::BootLog.lines(text))
    assert_equal 2, dumps.size
    assert_equal 101_000, dumps[0]["pools"]["fmrb_kernel"]
    assert_equal 40_960, dumps[0]["wasm"]["high"]
    assert_nil dumps[1]["wasm"]
    one = S.least(dumps)
    assert_equal 100_500, one["pools"]["fmrb_kernel"]
    assert_equal 208_400, one["iram_free"]
    assert_equal 40_960, one["wasm"]["high"]
  end

  def test_finish_on_a_board_judges_free_memory_and_every_pool
    rounds = (0..7).map do |i|
      { "pools" => { "fmrb_kernel" => 100_000, "Services" => 400_000 + i * 2048 },
        "free" => { "iram_free" => 208_000, "psram_free" => 11_000_000 } }
    end
    res = S.finish({ "board" => "NARYAv4", "idle_dump" => {}, "rounds" => rounds }, 3)
    assert_equal "settled", res["judged"]["fmrb_kernel"]["verdict"]
    assert_equal "grows", res["judged"]["Services"]["verdict"]
    assert_equal "settled", res["judged"]["iram_free"]["verdict"]
    assert S.grows?(res)
  end
end
