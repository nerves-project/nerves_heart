# SPDX-FileCopyrightText: 2022 Frank Hunleth
#
# SPDX-License-Identifier: Apache-2.0

defmodule DisableVmTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "sending disable_vm causes a heart timeout exit", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "disable_vm")

    assert_receive {:event, "sync()"}
    assert_receive {:event, "reboot(0x01234567)"}
    assert_receive {:exit, 0}
  end
end
