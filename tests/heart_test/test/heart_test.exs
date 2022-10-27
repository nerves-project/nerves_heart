defmodule HeartTestTest do
  use ExUnit.Case, async: true
  doctest Heart

  setup do
    # We need short temporary directory paths, so create them ourselves.
    path = "/tmp/heart_test/#{:rand.uniform(10000)}"
    File.mkdir_p!(path)
    [tmp_dir: path]
  end

  test "heart acks on start and exits on shutdown", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}

    Heart.shutdown(heart)
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "heart pets watchdog when petted itself", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}

    Heart.pet(heart)
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Heart.pet(heart)
    assert Heart.next_event(heart) == {:event, "pet(1)"}

    Heart.shutdown(heart)
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "heart doesn't pet watchdog when not petted", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}

    assert Heart.next_event(heart, 1000) == :timeout

    Heart.shutdown(heart)
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "heart reboots when not petted", context do
    # Shortest timeout is 11 seconds
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir, heart_beat_timeout: 11})
    assert Heart.next_event(heart) == {:heart, :heart_ack}

    Process.sleep(15000)

    assert Heart.next_event(heart) == {:event, "pet(1)"}
    assert Heart.next_event(heart) == {:event, "pet(1)"}
    assert Heart.next_event(heart) == {:event, "reboot(0x01234567)"}
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "getting heart status", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}

    {:ok, {:heart_cmd, cmd}} = Heart.get_cmd(heart)

    assert cmd == """
           program_name=nerves_heart
           program_version=1.1.0
           heartbeat_timeout=60
           identity=OMAP Watchdog
           firmware_version=0
           options=settimeout,magicclose,keepaliveping,
           time_left=116
           pre_timeout=0
           timeout=120
           last_boot=power_on
           """

    Heart.shutdown(heart)
    assert Heart.next_event(heart) == {:exit, 0}
  end

  test "sending disable stops petting the hardware watchdog", context do
    heart = start_supervised!({Heart, tmp_dir: context.tmp_dir})
    assert Heart.next_event(heart) == {:heart, :heart_ack}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "disable")

    Process.sleep(10000)

    assert Heart.next_event(heart, 1000) == :timeout

    Heart.shutdown(heart)
    assert Heart.next_event(heart) == {:exit, 0}
  end
end
