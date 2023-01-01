# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

defmodule CrashDumpTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "crash dump waits for notification", context do
    heart = start_supervised!({Heart, context.init_args ++ [crash_dump_seconds: 10]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    Heart.preparing_crash(heart)

    # Capture the WDT pet that's sent before the crash
    assert_receive {:event, "pet(1)"}

    # Nothing should happen now
    refute_receive _, 500

    # Any write to the socket will cause a reboot now.
    Heart.pet(heart)

    assert_receive {:event, "sync()"}
    assert_receive {:event, "reboot(0x01234567)"}
    assert_receive {:exit, 0}
  end

  test "crash dump crashes anyway after timeout", context do
    heart = start_supervised!({Heart, context.init_args ++ [crash_dump_seconds: 2]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    Heart.preparing_crash(heart)

    # Capture the WDT pet that's sent before the crash
    assert_receive {:event, "pet(1)"}

    # Nothing should happen for most of the 2 seconds
    refute_receive _, 1900

    # Timeout should trigger a crash in ~100 ms
    assert_receive {:event, "sync()"}, 200
    assert_receive {:event, "reboot(0x01234567)"}
    assert_receive {:exit, 0}
  end
end
