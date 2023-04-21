# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

defmodule SnoozeTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "snooze avoids reboot", context do
    # Shortest timeout is 11 seconds
    heart =
      start_supervised!({Heart, context.init_args ++ [heart_beat_timeout: 11, wdt_timeout: 2]})

    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "snooze")
    # Check for immediate petting of the watchdog
    assert_receive {:event, "pet(1)"}

    # Check that the watchdog is pet automatically
    assert_receive {:event, "pet(1)"}, 2500
    assert_receive {:event, "pet(1)"}, 2500
    assert_receive {:event, "pet(1)"}, 2500
    assert_receive {:event, "pet(1)"}, 2500
    assert_receive {:event, "pet(1)"}, 2500

    # This will get us past the Erlang heart beat timeout of 11 seconds
    assert_receive {:event, "pet(1)"}, 2500

    refute_receive {:exit, 0}, 100
  end
end
