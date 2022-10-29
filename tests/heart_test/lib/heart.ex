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

  @doc """
  Wait timeout milliseconds for the next event

  This is written trivial and only one caller process is supported at a time.
  """
  @spec next_event(GenServer.server(), non_neg_integer()) :: event() | :timeout
  def next_event(server, timeout \\ 1000) do
    GenServer.call(server, {:next_event, timeout})
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

    watchdog_path_env =
      if watchdog_path do
        [{~c"HEART_WATCHDOG_PATH", to_charlist(watchdog_path)}]
      else
        []
      end

    crash_dump_seconds = init_args[:crash_dump_seconds]

    crash_dump_seconds_env =
      if crash_dump_seconds do
        [{~c"ERL_CRASH_DUMP_SECONDS", ~c"#{crash_dump_seconds}"}]
      else
        []
      end

    File.exists?(shim) || raise "Can't find heart_fixture.so"
    File.exists?(heart) || raise "Can't find heart"
    File.exists?(tmp_dir) || raise "Can't find #{inspect(tmp_dir)}"

    c_shim = shim |> to_charlist()

    backend_socket = open_backend_socket(reports)

    heart_port =
      Port.open(
        {:spawn_executable, heart},
        [
          {:args, ["-ht", "#{heart_beat_timeout}"]},
          {:packet, 2},
          {:env,
           [
             {~c"LD_PRELOAD", c_shim},
             {~c"DYLD_INSERT_LIBRARIES", c_shim},
             {~c"HEART_REPORT_PATH", to_charlist(reports)},
             {~c"HEART_WATCHDOG_OPEN_TRIES", to_charlist(open_tries)}
           ] ++ watchdog_path_env ++ crash_dump_seconds_env},
          :exit_status
        ]
      )

    {:ok,
     %{
       heart: heart_port,
       backend: backend_socket,
       requests: :queue.new(),
       events: :queue.new(),
       waiter: nil
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

  def handle_call({:next_event, timeout}, from, state) do
    case :queue.out(state.events) do
      {{:value, event}, new_events} ->
        {:reply, event, %{state | events: new_events}}

      {:empty, _calls} ->
        timer_ref = Process.send_after(self(), {:timeout, from}, timeout)
        {:noreply, %{state | waiter: {from, timer_ref}}}
    end
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

  def handle_info({:timeout, client}, %{waiter: {client, _timer_ref}} = state) do
    GenServer.reply(client, :timeout)
    {:noreply, %{state | waiter: nil}}
  end

  def handle_info({:timeout, _client}, state) do
    # Ignore stale timeout
    {:noreply, state}
  end

  def handle_info(message, state) do
    IO.puts("Got unexpected data #{inspect(message)}")
    {:noreply, state}
  end

  defp process_event(state, event) do
    if state.waiter do
      {client, timer_ref} = state.waiter

      GenServer.reply(client, event)
      _ = Process.cancel_timer(timer_ref)

      %{state | waiter: nil}
    else
      %{state | events: :queue.in(event, state.events)}
    end
  end

  defp decode_response(<<@heart_ack>>), do: :heart_ack
  defp decode_response(<<@heart_cmd, data::binary>>), do: {:heart_cmd, data}

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
