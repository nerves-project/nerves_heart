# SPDX-FileCopyrightText: 2022 Frank Hunleth
#
# SPDX-License-Identifier: Apache-2.0

defmodule DisableHwTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "sending disable_hw stops petting the hardware watchdog", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}, 500
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "disable_hw")

    refute_receive _, 11000

    # NOTE: even graceful shutdown doesn't do a final pet of the WDT
    Heart.shutdown(heart)
    assert_receive {:exit, 0}
  end
end
