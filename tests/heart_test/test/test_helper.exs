# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

ExUnit.start()

# Set to 1 or 2 to get log messages from Nerves heart
System.put_env("HEART_VERBOSE", "0")

defmodule HeartTestCommon do
  use ExUnit.Case

  def common_setup() do
    # We need short temporary directory paths, so create them ourselves.
    path = "/tmp/heart_test/#{:rand.uniform(1_000_000)}"
    File.mkdir_p!(path)
    on_exit(fn -> File.rm_rf!(path) end)

    init_args = [tmp_dir: path, notifications: self()]
    [init_args: init_args]
  end

  def graceful_shutdown(heart) do
    # The event queue should be empty
    refute_received _

    # Request a graceful shutdown and then wait for the
    # final WDT pet and exit
    Heart.shutdown(heart)
    assert_receive {:event, "pet(1)"}
    assert_receive {:exit, 0}
  end
end
