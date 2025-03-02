# SPDX-FileCopyrightText: 2022 Frank Hunleth
#
# SPDX-License-Identifier: Apache-2.0

defmodule BasicTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "heart acks on start and exits on shutdown", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "heart pets watchdog when petted itself", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    Heart.pet(heart)
    assert_receive {:event, "pet(1)"}

    Heart.pet(heart)
    assert_receive {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "heart doesn't pet watchdog when not petted", context do
    # The default wdt_timeout is 120s and the VM timeout is 60s, so no
    # pet should happen. Wait for 6s to detect whether the default pet
    # timeout of 5 seconds was erroneously used.
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    refute_receive _, 6000

    graceful_shutdown(heart)
  end

  test "heart reboots when not petted", context do
    # Shortest timeout is 11 seconds
    start_supervised!({Heart, context.init_args ++ [heart_beat_timeout: 11]})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    Process.sleep(10000)
    refute_received _

    assert_receive {:event, "sync()"}, 1500
    assert_receive {:event, "reboot(0x01234567)"}
    assert_receive {:exit, 0}
  end
end
