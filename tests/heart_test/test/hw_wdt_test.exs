# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

defmodule HwWdtTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "hw watchdog gets pet when shorter than vm timeout", context do
    # The VM timeout defaults to 60. The wdt_timeout of 12 will make the
    # hw watchdog get pet every 6 seconds.
    heart = start_supervised!({Heart, context.init_args ++ [wdt_timeout: 12]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    refute_receive _, 5500

    assert_receive {:event, "pet(1)"}, 1000

    graceful_shutdown(heart)
  end

  test "hw watchdog gets pet every 5 seconds when bogus timeout read", context do
    # The VM timeout defaults to 60. The bogus wdt_timeout of 0 will make the
    # hw watchdog get pet every 5 seconds.
    heart = start_supervised!({Heart, context.init_args ++ [wdt_timeout: 0]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    Process.sleep(5000)
    assert_receive {:event, "pet(1)"}

    graceful_shutdown(heart)
  end

  test "non-default watchdog files", context do
    heart = start_supervised!({Heart, context.init_args ++ [watchdog_path: "/dev/watchdog1"]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog1) succeeded"}
    assert_receive {:event, "pet(1)"}

    graceful_shutdown(heart)
  end
end
