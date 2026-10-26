# frozen_string_literal: true
#
# boot_log_test.rb - the boot log reader of rake regress:boot
# (tools/regress/boot_log.rb) against made-up logs. No device, no docker:
#   ruby test/regress/boot_log_test.rb   (rake regress:boot:test, part of rake test)
require "minitest/autorun"
require_relative "../../tools/regress/boot_log"

class BootLogTest < Minitest::Test
  BL = Regress::BootLog

  def dump(iram, status_led_free, kernel_used)
    <<~LOG
      I (09:00:11.000) fmrb_task: --- Task Status (3 tracked) ---
      I (09:00:11.000) fmrb_task: Name             Act Core Stack  Free Mem   Pin
      I (09:00:11.001) fmrb_task: hw_proxy           Y    0  6144  4300 IRAM  PIN0
      I (09:00:11.002) fmrb_task: status_led         Y    1  4096  #{status_led_free} IRAM  PIN1
      I (09:00:11.003) fmrb_task: StackChan Remote   Y    -  8192  2000 PSRAM ANY
      I (09:00:11.004) fmrb_task: IRAM free: #{iram} bytes, PSRAM free: 11000000 bytes
      I (09:00:11.005) fmrb_task: --------------------------------
      I (09:00:11.010) fmrb_app: --- VM Pools ---
      I (09:00:11.010) fmrb_app: Name               VM     Used     Free    Total     Peak  Largest  Frag  ExcHW
      I (09:00:11.011) fmrb_app: fmrb_kernel       spx   #{kernel_used}   410248   512000   127056   385264   31%    3/0
      I (09:00:11.012) fmrb_app: StackChan Remote  mrb   400000   648576  1048576   420000   600000   10%      -
      I (09:00:11.013) fmrb_app: ----------------
    LOG
  end

  def boot(internal: 212_636, largest: 172_032, extra: "", dumps: [[208_616, 1508, 101_424], [208_600, 1428, 101_400]])
    log = +<<~LOG
      [  0.701] ESP-ROM:esp32p4-eco6-20251011
      [  0.701] rst:0x1 (POWERON),boot:0x30f (SPI_FAST_FLASH_BOOT)
      [  2.704] I (2413) app_init: App version:      2.1.0-847-g802b16a3
      [  2.704] I (00:00:00.159) boot: Running on ESP32-P4 (Waveshare) - Production mode
      [  3.104] I (00:00:00.266) fmrb_alloc: M1|boot_start|internal=505244|largest=442368|psram=15403060
      [  4.707] I (09:00:02.013) fmrb_alloc: M1|spawn:fmrb_kernel|internal=374992|largest=335872|psram=12404096
      [  5.208] I (09:00:02.229) fmrb_alloc: M1|spawn:Services|internal=301592|largest=262144|psram=12392060
      [  6.000] I (09:00:03.000) fmrb_alloc: M1|spawn:Services|internal=290000|largest=250000|psram=12392060
      [  9.714] I (09:00:07.336) status_led: Boot complete: switching to heartbeat
      [ 12.217] I (09:00:09.663) wifi: STA starting, ssid=MyHomeAP
      [ 12.217] I (09:00:09.682) fmrb_alloc: M1|wifi_rd|internal=#{internal}|largest=#{largest}|psram=11541844
    LOG
    log << extra
    dumps.each { |d| log << dump(*d) }
    log
  end

  def test_reads_a_healthy_boot
    r = BL.parse(boot)
    assert_empty r["crash"]
    assert_equal 1, r["boots"]
    assert_equal "POWERON", r["reset"]
    assert_equal "2.1.0-847-g802b16a3", r["version"]
    assert_equal %w[boot_start spawn:fmrb_kernel spawn:Services spawn:Services#2 wifi_rd], r["m1"].keys
    assert_equal({ "internal" => 212_636, "largest" => 172_032, "psram" => 11_541_844 }, r["m1"]["wifi_rd"])
    assert r["boot_complete"]
    assert_in_delta 9.0, r["boot_seconds"], 0.01
    assert_equal 2, r["dumps"]
    assert_equal 208_600, r["iram_free"], "the last dump wins"
    assert_equal 1428, r["tasks"]["status_led"]["free"]
    assert_equal 2000, r["tasks"]["StackChan Remote"]["free"], "a name with a space"
    assert_equal 101_400, r["pools"]["fmrb_kernel"]["used"]
    assert_equal 127_056, r["pools"]["fmrb_kernel"]["peak"]
    assert_equal "3/0", r["pools"]["fmrb_kernel"]["exchw"]
    assert_equal "mrb", r["pools"]["StackChan Remote"]["vm"]
  end

  def test_old_pool_table_without_peak
    log = boot(dumps: []) + <<~LOG
      I (09:00:11.004) fmrb_task: IRAM free: 208652 bytes, PSRAM free: 11000000 bytes
      I (09:00:11.010) fmrb_app: --- VM Pools ---
      I (09:00:11.010) fmrb_app: Name               VM     Used     Free    Total  Frag  ExcHW
      I (09:00:11.011) fmrb_app: fmrb_kernel       spx   101376   410248   512000   31%    3/0
      I (09:00:11.013) fmrb_app: ----------------
    LOG
    r = BL.parse(log)
    assert_equal 101_376, r["pools"]["fmrb_kernel"]["used"]
    assert_nil r["pools"]["fmrb_kernel"]["peak"]
  end

  def test_same_boot_passes
    base = BL.parse(boot)
    c = BL.compare(BL.parse(boot), base)
    assert_empty c[:fails]
  end

  def test_crash_marker_fails
    crash = "I (09:00:05.000) kernel: x\nGuru Meditation Error: Core  1 panic'ed (Load access fault). Exception was unhandled.\n"
    base = BL.parse(boot)
    c = BL.compare(BL.parse(boot(extra: crash)), base)
    assert(c[:fails].any? { |f| f.start_with?("crash markers: 1") }, c[:fails].inspect)
  end

  def test_each_crash_marker_is_seen
    ["abort() was called at PC 0x4000", "Backtrace: 0x40001 0x40002", "assert failed: xQueueSemaphoreTake queue.c:1709",
     "***ERROR*** A stack overflow in task fmrb_kernel has been detected.", "CORRUPT HEAP: Bad head at 0x4ff1",
     "Stack protection fault"].each do |line|
      assert_equal 1, BL.parse(boot(extra: "#{line}\n"))["crash"].size, line
    end
  end

  def test_reboot_fails
    again = "[ 20.000] ESP-ROM:esp32p4-eco6-20251011\n[ 20.000] rst:0xc (SW_CPU_RESET),boot:0x30f (SPI_FAST_FLASH_BOOT)\n"
    r = BL.parse(boot(extra: again))
    assert_equal 2, r["boots"]
    assert(BL.compare(r, BL.parse(boot))[:fails].any? { |f| f.include?("booted 2 times") })
  end

  def test_less_internal_ram_beyond_tolerance_fails
    base = BL.parse(boot)
    ok = BL.compare(BL.parse(boot(internal: 212_636 - 1000)), base)
    assert_empty ok[:fails], "within 1024"
    bad = BL.compare(BL.parse(boot(internal: 212_636 - 2000)), base)
    assert_equal ["M1|wifi_rd internal 210,636 (-2,000, tolerance 1,024)"], bad[:fails]
  end

  def test_more_ram_is_moved_not_failed
    c = BL.compare(BL.parse(boot(internal: 212_636 + 5000)), BL.parse(boot))
    assert_empty c[:fails]
    assert_equal ["M1|wifi_rd internal +5,000"], c[:moved]
  end

  def test_stack_and_iram_free
    base = BL.parse(boot)
    c = BL.compare(BL.parse(boot(dumps: [[208_600 - 2000, 1428 - 500, 101_400]])), base)
    assert_equal ["IRAM free 206,600 (-2,000, tolerance 1,024)", "stack status_led 928 (-500, tolerance 384)"], c[:fails]
  end

  def test_missing_step_task_and_dump_fail
    base = BL.parse(boot)
    cur = BL.parse(boot(dumps: []).sub(/^.*M1\|wifi_rd.*\n/, ""))
    fails = BL.compare(cur, base)[:fails]
    assert_includes fails, "no periodic dump (IRAM free)"
    assert_includes fails, "M1|wifi_rd missing"
    assert_includes fails, "task status_led missing from the dump"
  end

  def test_no_baseline_fails
    assert_includes BL.compare(BL.parse(boot), nil)[:fails], "no baseline"
  end

  def test_masks_device_names
    text = "wifi: Connected, ip=10.20.30.40 (http://fmruby-0f1e2d.local/)\nwifi: STA starting, ssid=MyHomeAP\n" \
           "ble_task: BLE address: 12:34:56:78:9a:bc\nServices: svc[net] up: 10.20.30.40 (MyHomeAP)\n"
    m = BL.mask(text)
    refute_match(/10\.20|0f1e2d|MyHomeAP|12:34/, m)
    assert_includes m, "fmruby-a1b2c3"
  end

  def test_table_is_short
    lines, failed = BL.table({ "naryav4" => BL.parse(boot).merge("firmware" => "NARYAv4") },
                             { "naryav4" => BL.parse(boot) })
    assert_empty failed
    assert_equal 3, lines.size
    assert_match(/\ANARYAv4\s+POWERON\s+9.0s\s+0\s+5\s+212,636 \+0/, lines[1])
  end
end
