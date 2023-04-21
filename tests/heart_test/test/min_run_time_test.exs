# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

defmodule MinRunTimeTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "heart doesn't reboot when not petted before min_run_time", context do
    # Shortest timeout is 11 seconds
    start_supervised!(
      {Heart, context.init_args ++ [heart_beat_timeout: 11, wdt_timeout: 10, min_run_time: 12]}
    )

    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    # Wait for 2 pet (10 seconds total)
    refute_receive _, 4500
    assert_receive {:event, "pet(1)"}, 1000
    refute_receive _, 4500
    assert_receive {:event, "pet(1)"}, 1000
    refute_received _

    # At the 12 second mark, we're passed the min_run_time grace period. If it fails
    # on the next line, min_run_time is broke.
    refute_receive _, 4500
    assert_receive {:event, "pet(1)"}, 1000
    refute_receive _, 4500
    assert_receive {:event, "pet(1)"}, 1000
    refute_received _

    # Now we're 3 seconds from the 23 second mark (12s min_run_time + 11s hb_timeout) when the exit happens.
    refute_receive _, 2800
    assert_receive {:event, "sync()"}, 500
    assert_receive {:event, "reboot(0x01234567)"}
    assert_receive {:exit, 0}
  end
end
