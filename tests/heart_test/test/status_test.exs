defmodule StatusTest do
  use ExUnit.Case, async: true

  import HeartTestCommon

  setup do
    common_setup()
  end

  test "getting heart status", context do
    heart = start_supervised!({Heart, context.init_args})
    assert_receive {:heart, :heart_ack}
    assert_receive {:event, "open(/dev/watchdog0) succeeded"}
    assert_receive {:event, "pet(1)"}

    {:ok, {:heart_cmd, cmd}} = Heart.get_cmd(heart)

    assert cmd == %{
             "heartbeat_time_left" => "60",
             "heartbeat_timeout" => "60",
             "init_handshake_happened" => "1",
             "init_handshake_time_left" => "0",
             "init_handshake_timeout" => "0",
             "program_name" => "nerves_heart",
             "program_version" => "2.0.0",
             "wdt_firmware_version" => "0",
             "wdt_identity" => "OMAP Watchdog",
             "wdt_last_boot" => "power_on",
             "wdt_options" => "settimeout,magicclose,keepaliveping,",
             "wdt_pet_time_left" => "110",
             "wdt_pre_timeout" => "0",
             "wdt_time_left" => "116",
             "wdt_timeout" => "120"
           }

    graceful_shutdown(heart)
  end
end
