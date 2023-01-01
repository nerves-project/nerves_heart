# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

defmodule GuardedRebootTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "guarded reboot", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "guarded_reboot")

    # Final WDT pet
    assert_receive {:event, "pet(1)"}

    # Tell PID 1 to reboot
    assert_receive {:event, "kill(1, SIGTERM)"}

    # Proactive sync
    assert_receive {:event, "sync()"}

    Process.sleep(6)

    # Run normal shutdown and check that there aren't any more WDT pets
    Heart.shutdown(heart)
    assert_receive {:exit, 0}
  end

  test "guarded poweroff", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "guarded_poweroff")

    # Final WDT pet
    assert_receive {:event, "pet(1)"}

    # Tell PID 1 to poweroff
    assert_receive {:event, "kill(1, SIGUSR2)"}

    # Proactive sync
    assert_receive {:event, "sync()"}

    Process.sleep(6)

    # Run normal shutdown and check that there aren't any more WDT pets
    Heart.shutdown(heart)
    assert_receive {:exit, 0}
  end

  test "guarded halt", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, :heart_ack} = Heart.set_cmd(heart, "guarded_halt")

    # Final WDT pet
    assert_receive {:event, "pet(1)"}

    # Tell PID 1 to halt
    assert_receive {:event, "kill(1, SIGUSR1)"}

    # Proactive sync
    assert_receive {:event, "sync()"}

    Process.sleep(6)

    # Run normal shutdown and check that there aren't any more WDT pets
    Heart.shutdown(heart)
    assert_receive {:exit, 0}
  end
end
