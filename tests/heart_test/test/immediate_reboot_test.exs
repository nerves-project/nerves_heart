# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

defmodule ImmediateRebootTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "guarded reboot", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    assert Heart.set_cmd(heart, "guarded_immediate_reboot") == {:error, :exit}

    # Kernel reboot
    assert_receive {:event, "reboot(0x01234567)"}
    refute_receive _
  end

  test "guarded immediate poweroff", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    assert Heart.set_cmd(heart, "guarded_immediate_poweroff") == {:error, :exit}

    # Kernel reboot
    assert_receive {:event, "reboot(0x4321fedc)"}
    refute_receive _
  end
end
