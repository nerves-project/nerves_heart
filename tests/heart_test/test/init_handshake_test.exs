defmodule InitHandshakeTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "application init handshake times out if not handshaked", context do
    heart = start_supervised!({Heart, context.init_args ++ [init_timeout: 5]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, {:heart_cmd, cmd}} = Heart.get_cmd(heart)

    assert cmd["init_handshake_happened"] == "0"
    assert cmd["init_handshake_timeout"] == "5"
    assert cmd["init_handshake_time_left"] == "5"

    Process.sleep(1000)

    # Check that the time left changed. Don't be too picky here since there are
    # some random CI/local delays which sometimes add more delay.
    {:ok, {:heart_cmd, cmd}} = Heart.get_cmd(heart)
    assert cmd["init_handshake_time_left"] == "4" or cmd["init_handshake_time_left"] == "3"

    # No messages for 3 seconds
    refute_receive _, 3000

    # Capture reboot in about 1 second
    assert_receive {:event, "sync()"}, 1500
    assert_receive {:event, "reboot(0x01234567)"}
    assert_receive {:exit, 0}
  end

  test "application init handshake works", context do
    heart = start_supervised!({Heart, context.init_args ++ [init_timeout: 5]})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, {:heart_cmd, cmd}} = Heart.get_cmd(heart)
    assert cmd["init_handshake_happened"] == "0"
    assert cmd["init_handshake_timeout"] == "5"
    assert cmd["init_handshake_time_left"] == "5"

    assert {:ok, :heart_ack} == Heart.set_cmd(heart, "init_handshake")

    {:ok, {:heart_cmd, cmd}} = Heart.get_cmd(heart)
    assert cmd["init_handshake_happened"] == "1"
    assert cmd["init_handshake_timeout"] == "5"
    assert cmd["init_handshake_time_left"] == "0"

    # Init timer shouldn't expire so there should be no messages for 6 seconds
    refute_receive _, 6000

    graceful_shutdown(heart)
  end
end
