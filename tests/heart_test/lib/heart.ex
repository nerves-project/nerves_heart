# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

defmodule Heart do
  @moduledoc """
  Fixture for testing the heart binary

  This lets tests send messages, get replies and check that the right
  syscalls were made to the OS.
  """
  use GenServer

  @heart_ack 1
  @heart_beat 2
  @shut_down 3
  @set_cmd 4
  @clear_cmd 5
  @get_cmd 6
  @heart_cmd 7
  @preparing_crash 8

  @type event() :: {:heart, atom()} | {:event, String.t()} | {:exit, non_neg_integer()}

  @spec start_link(keyword()) :: GenServer.on_start()
  def start_link(init_args) do
    GenServer.start_link(__MODULE__, init_args)
  end

  @spec send_message(GenServer.server(), iodata()) :: :ok
  def send_message(server, data) do
    GenServer.call(server, {:send_message, data})
  end

  @spec request(GenServer.server(), iodata()) :: {:ok, any()}
  def request(server, data) do
    GenServer.call(server, {:request, data})
  end

  @spec pet(GenServer.server()) :: :ok
  def pet(server) do
    send_message(server, <<@heart_beat>>)
  end

  @spec shutdown(GenServer.server()) :: :ok
  def shutdown(server) do
    send_message(server, <<@shut_down>>)
  end

  @spec set_cmd(GenServer.server(), String.t()) :: {:ok, :heart_ack}
  def set_cmd(server, cmd) do
    request(server, <<@set_cmd, cmd::binary>>)
  end

  @spec clear_cmd(GenServer.server()) :: {:ok, :heart_ack}
  def clear_cmd(server) do
    request(server, <<@clear_cmd>>)
  end

  @spec get_cmd(GenServer.server()) :: {:ok, {:heart_cmd, binary()}}
  def get_cmd(server) do
    request(server, <<@get_cmd>>)
  end

  @spec preparing_crash(GenServer.server()) :: :ok
  def preparing_crash(server) do
    send_message(server, <<@preparing_crash>>)
  end

  @impl GenServer
  def init(init_args) do
    shim = Application.app_dir(:heart_test, ["priv", "heart_fixture.so"]) |> Path.expand()
    heart = Path.expand("../../heart")
    tmp_dir = init_args[:tmp_dir] || "/tmp"
    reports = Path.join(tmp_dir, "reports.sock")
    heart_beat_timeout = init_args[:heart_beat_timeout] || 60
    open_tries = init_args[:open_tries] || 0
    watchdog_path = init_args[:watchdog_path]
    wdt_timeout = init_args[:wdt_timeout] || 120
    crash_dump_seconds = init_args[:crash_dump_seconds]
    init_timeout = init_args[:init_timeout]
    init_grace_time = init_args[:init_grace_time]

    File.exists?(shim) || raise "Can't find heart_fixture.so"
    File.exists?(heart) || raise "Can't find heart"
    File.exists?(tmp_dir) || raise "Can't find #{inspect(tmp_dir)}"

    c_shim = shim |> to_charlist()

    env =
      [
        if watchdog_path do
          {~c"HEART_WATCHDOG_PATH", to_charlist(watchdog_path)}
        end,
        # WDT_TIMEOUT is handled by the test fixture for what to return from the simulated hardware wdt
        if wdt_timeout do
          {~c"WDT_TIMEOUT", ~c"#{wdt_timeout}"}
        end,
        if crash_dump_seconds do
          {~c"ERL_CRASH_DUMP_SECONDS", ~c"#{crash_dump_seconds}"}
        end,
        if init_timeout do
          {~c"HEART_INIT_TIMEOUT", ~c"#{init_timeout}"}
        end,
        if init_timeout do
          {~c"HEART_INIT_TIMEOUT", ~c"#{init_timeout}"}
        end,
        if init_grace_time do
          {~c"HEART_INIT_GRACE_TIME", ~c"#{init_grace_time}"}
        end,
        {~c"LD_PRELOAD", c_shim},
        {~c"DYLD_INSERT_LIBRARIES", c_shim},
        {~c"HEART_REPORT_PATH", to_charlist(reports)},
        {~c"HEART_WATCHDOG_OPEN_TRIES", to_charlist(open_tries)}
      ]
      |> Enum.filter(&Function.identity/1)

    backend_socket = open_backend_socket(reports)

    heart_port =
      Port.open(
        {:spawn_executable, heart},
        [
          {:args, ["-ht", "#{heart_beat_timeout}"]},
          {:packet, 2},
          {:env, env},
          :exit_status
        ]
      )

    {:ok,
     %{
       heart: heart_port,
       backend: backend_socket,
       requests: :queue.new(),
       notifications: init_args[:notifications]
     }}
  end

  @impl GenServer
  def handle_call({:send_message, data}, _from, state) do
    Port.command(state.heart, data)

    {:reply, :ok, state}
  end

  def handle_call({:request, data}, from, state) do
    Port.command(state.heart, data)

    {:noreply, %{state | requests: :queue.in(from, state.requests)}}
  end

  @impl GenServer
  def handle_info({heart, {:data, data}}, %{heart: heart} = state) do
    result = data |> IO.iodata_to_binary() |> decode_response()

    case :queue.out(state.requests) do
      {{:value, client}, new_requests} ->
        GenServer.reply(client, {:ok, result})
        {:noreply, %{state | requests: new_requests}}

      {:empty, _requests} ->
        {:noreply, process_event(state, {:heart, result})}
    end
  end

  def handle_info({heart, {:exit_status, value}}, %{heart: heart} = state) do
    {:noreply, process_event(state, {:exit, value})}
  end

  def handle_info({:udp, backend, _, 0, data}, %{backend: backend} = state) do
    {:noreply, process_event(state, {:event, data})}
  end

  def handle_info(message, state) do
    IO.puts("Got unexpected data #{inspect(message)}")
    {:noreply, state}
  end

  defp process_event(state, event) do
    send(state.notifications, event)
    state
  end

  defp decode_response(<<@heart_ack>>), do: :heart_ack

  defp decode_response(<<@heart_cmd, data::binary>>) do
    stats =
      data
      |> String.split("\n", trim: true)
      |> Enum.map(&String.split(&1, "=", parts: 2))
      |> Enum.map(fn [x, y] -> {x, y} end)
      |> Map.new()

    {:heart_cmd, stats}
  end

  defp open_backend_socket(socket_path) do
    # Blindly try to remove an old file just in case it exists from a previous run
    _ = File.rm(socket_path)

    {:ok, socket} =
      :gen_udp.open(0, [
        :local,
        :binary,
        {:active, true},
        {:ip, {:local, socket_path}},
        {:recbuf, 1024}
      ])

    socket
  end
end
