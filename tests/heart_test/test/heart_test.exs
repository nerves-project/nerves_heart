defmodule HeartTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "retrying opening the hardware watchdog", context do
    # This tests the case when the hardware watchdog driver
    # comes up after heart does. This can happen if the driver
    # is compiled as a module.
    heart = start_supervised!({Heart, context.init_args ++ [open_tries: 1]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) failed"}

    Process.sleep(6000)

    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    graceful_shutdown(heart)
  end
end
