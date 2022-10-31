defmodule HeartTestTest do
  use ExUnit.Case, async: true
  doctest Heart

  setup_all do
    File.rm_rf!("/tmp/heart_test")
    []
  end

  setup do
    # We need short temporary directory paths, so create them ourselves.
    path = "/tmp/heart_test/#{:rand.uniform(1_000_000)}"
    File.mkdir_p!(path)
    [tmp_dir: path]
  end

  defp graceful_shutdown(heart) do
    # The event queue should be empty
    assert Heart.next_event(heart, 0) == :timeout

    # Request a graceful shutdown and then wait for the
    # final WDT pet and exit
    Heart.shutdown(heart)
    assert Heart.next_event(heart) == {:event, "pet(1)"}
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "heart acks on start and exits on shutdown", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "heart pets watchdog when petted itself", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Heart.pet(heart)
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Heart.pet(heart)
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "heart doesn't pet watchdog when not petted", context do
    # The default wdt_timeout is 120s and the VM timeout is 60s, so no
    # pet should happen. Wait for 6s to detect whether the default pet
    # timeout of 5 seconds was erroneously used.
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    assert Heart.next_event(heart, 6000) == :timeout

    graceful_shutdown(heart)
  end

  test "heart reboots when not petted", context do
    # Shortest timeout is 11 seconds
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, heart_beat_timeout: 11})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Process.sleep(12000)

    assert Heart.next_event(heart) == {:event, "sync()"}
    assert Heart.next_event(heart) == {:event, "reboot(0x01234567)"}
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "hw watchdog gets pet when shorter than vm timeout", context do
    # The VM timeout defaults to 60. The wdt_timeout of 12 will make the
    # hw watchdog get pet every 6 seconds.
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, wdt_timeout: 12})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Process.sleep(5000)
    assert Heart.next_event(heart, 100) == :timeout
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "hw watchdog gets pet every 5 seconds when bogus timeout read", context do
    # The VM timeout defaults to 60. The bogus wdt_timeout of 0 will make the
    # hw watchdog get pet every 5 seconds.
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, wdt_timeout: 0})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Process.sleep(5000)
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "getting heart status", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    {:ok, {:heart_cmd, cmd}} = Heart.get_cmd(heart)

    assert cmd == """
           program_name=nerves_heart
           program_version=1.2.0
           heartbeat_timeout=60
           identity=OMAP Watchdog
           firmware_version=0
           options=settimeout,magicclose,keepaliveping,
           time_left=116
           pre_timeout=0
           timeout=120
           last_boot=power_on
           """

    graceful_shutdown(heart)
  end

  test "sending disable_hw stops petting the hardware watchdog", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "disable_hw")

    Process.sleep(10000)

    assert Heart.next_event(heart, 1000) == :timeout

    # NOTE: even graceful shutdown doesn't do a final pet of the WDT
    Heart.shutdown(heart)
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "sending disable_vm causes a heart timeout exit", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "disable_vm")

    assert Heart.next_event(heart) == {:event, "sync()"}
    assert Heart.next_event(heart) == {:event, "reboot(0x01234567)"}
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "retrying opening the hardware watchdog", context do
    # This tests the case when the hardware watchdog driver
    # comes up after heart does. This can happen if the driver
    # is compiled as a module.
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, open_tries: 1})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) failed"}

    Process.sleep(6000)

    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "non-default watchdog files", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, watchdog_path: "/dev/watchdog1"})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog1) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "crash dump waits for notification", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, crash_dump_seconds: 10})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Heart.preparing_crash(heart)

    # Capture the WDT pet that's sent before the crash
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    # Nothing should happen now
    assert Heart.next_event(heart, 500) == :timeout

    # Any write to the socket will cause a reboot now.
    Heart.pet(heart)

    assert Heart.next_event(heart) == {:event, "sync()"}
    assert Heart.next_event(heart) == {:event, "reboot(0x01234567)"}
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "crash dump crashes anyway after timeout", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, crash_dump_seconds: 2})
    assert Heart.next_event(heart) == {:heart, :heart_ack}
    assert Heart.next_event(heart) == {:event, "open(/dev/watchdog0) succeeded"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Heart.preparing_crash(heart)

    # Capture the WDT pet that's sent before the crash
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    # Nothing should happen
    assert Heart.next_event(heart, 1500) == :timeout

    # Timeout should trigger a crash now
    assert Heart.next_event(heart) == {:event, "sync()"}
    assert Heart.next_event(heart) == {:event, "reboot(0x01234567)"}
    assert Heart.next_event(heart) == {:exit, 0}
  end
end
