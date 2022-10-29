# nerves_heart

[![CircleCI](https://circleci.com/gh/nerves-project/nerves_heart.svg?style=svg)](https://circleci.com/gh/nerves-project/nerves_heart)

This is a replacement for Erlang's `heart` port process specifically for
Nerves-based devices. [Heart](http://erlang.org/doc/man/heart.html) monitors the
Erlang runtime using heartbeat messages. If the Erlang runtime becomes
unresponsive, `heart` reboots the system. This implementation of `heart` is
fully compatible with the default implementation and provides the following
changes:

1. Support for hardware watchdogs so that if both the Erlang runtime and
   `heart` become unresponsive, the hardware watchdog can reboot the system
2. Only monotonic time is used. The Erlang implementation of `heart` could use
   wall clock time and reboot the system for time changes larger than the
   heartbeat period. This is obviously not desirable on Nerves devices that run
   code on the Erlang VM that initializes the system clock.
3. Directly calls
   [reboot(2)](http://man7.org/linux/man-pages/man2/reboot.2.html). The reboot
   call is not configurable nor is it necessary to invoke another program.
4. Simplified code base. Code not needed for Nerves devices has been removed to
   make the program easier to audit.

## Using

If `nerves_heart` is part of your Nerves system, then all that you need to do is
enable it in your `rel/vm.args` file by adding the line:

```erlang
-heart
```

If `nerves_heart` is not part of your system, you'll need to compile it and copy
it over the Erlang-provided `heart` program in `/usr/lib/erlang/erts-x.y/bin`.

Once you have `heart` running, if you have a hardware watchdog on your board,
you'll see a message printed to the console on your device with the pet
interval:

```sh
heart: Kernel watchdog activated (interval 5s)
```

The interval that Erlang needs to communicate with `heart` can be different from
the interval that `heart` pets the hardware watchdog. The default for Erlang is
60 seconds. If this is too long, update your `rel/vm.args` like follows:

```erlang
-heart -env HEART_BEAT_TIMEOUT 30
```

The heart beat timeout has to be greater than 10 seconds per the Erlang
documentation.

If you need to change the watchdog path, you can do this through an environment variable.

```erlang
-heart -env HEART_WATCHDOG_PATH /dev/watchdog1
```

## Runtime diagnostic information

Both `nerves_heart` and the Linux watchdog subsystem can return useful
information via `:heart.get_cmd/0`. According to the Erlang/OTP documentation,
this function really should return the temporary reboot command, but it's
commented as unsupported in the Erlang version of `heart`. Given that there
isn't a better function call for returning this information, this one is used.

Here's an example run:

```elixir
iex> :heart.get_cmd
{:ok,
 'program_name=nerves_heart\nprogram_version=1.1.0\nheartbeat_timeout=30\nidentity=OMAP Watchdog\nfirmware_version=0\noptions=settimeout,magicclose,keepaliveping,\ntime_left=117\npre_timeout=0\ntimeout=120\nlast_boot=power_on\n'}
```

The format is "key=value\n". The keys are either from `nerves_heart` or from
Linux watchdog `ioctl` functions.

If you're using Nerves, `Nerves.Runtime.Heart` has a friendlier interface:

```elixir
iex> Nerves.Runtime.Heart.status!
%{
  firmware_version: 0,
  heartbeat_timeout: 30,
  identity: "OMAP Watchdog",
  last_boot: :power_on,
  options: [:settimeout, :magicclose, :keepaliveping],
  pre_timeout: 0,
  program_name: "nerves_heart",
  program_version: %Version{major: 1, minor: 1, patch: 0},
  time_left: 116,
  timeout: 120
}
```

The following table describes keys and their values:

| Key | Description or value  |
| --- | --------------------- |
| `:program_name` | `"nerves_heart"` |
| `:program_version` | Nerves heart's version number  |
| `:identity` | The hardware watchdog that's being used  |
| `:firmware_version` | An integer that represents the hardware watchdog's firmware revision  |
| `:options` | Hardware watchdog options as reported by Linux  |
| `:time_left` | How many seconds are left before the hardware watchdog triggers a reboot  |
| `:pre_timeout` | How many seconds before the watchdog expires that Linux will receive a pre-timeout notification  |
| `:timeout` | The hardware watchdog timeout. This is only changeable in the Linux configuration |
| `:last_boot` | What caused the most recent boot. Whether this is reliable depends on the watchdog. |
| `:heartbeat_timeout` | Erlang's heartbeat timeout setting. Note that the hardware watchdog timeout supersedes this since it reboots. |

## Testing

It's reassuring to know that `heart` does what it's supposed to do since it
should only rarely take action. If you'd like to verify that a hardware watchdog
works, you can instruct `heart` to stop petting the hardware watchdog by
running:

```elixir
iex> :heart.set_cmd("disable_hw")
```

If you'd like to verify how the Erlang VM handles the `heart` process detecting
an unresponsive VM, you can instruct `heart` to stop as it would if it timed out
on the VM by running:

```elixir
iex> :heart.set_cmd("disable_vm")
```
