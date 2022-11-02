# nerves_heart

[![CircleCI](https://circleci.com/gh/nerves-project/nerves_heart.svg?style=svg)](https://circleci.com/gh/nerves-project/nerves_heart)

This is a replacement for Erlang's `heart` port process specifically for
Nerves-based devices. It is installed by default on all Nerves devices.

[Heart](http://erlang.org/doc/man/heart.html) monitors the Erlang runtime using
heart beat messages. If the Erlang runtime becomes unresponsive, `heart` reboots
the system. This implementation of `heart` is fully compatible with the default
implementation and provides the following changes:

1. Supports a hardware watchdog timer (WDT) so that if both the Erlang runtime
   and `heart` become unresponsive, the hardware watchdog can reboot the system
2. Simplifies the code base. Code not needed for Nerves devices has been removed
   to make the program easier to audit.
3. Provides diagnostic information. See
   [Nerves.Runtime.Heart](https://hexdocs.pm/nerves_runtime/Nerves.Runtime.Heart.html#status/0)
   for an easier interface to get this.
4. Directly calls [sync(2)](https://man7.org/linux/man-pages/man2/sync.2.html)
   and [reboot(2)](http://man7.org/linux/man-pages/man2/reboot.2.html) when
   Erlang is unresponsive. The reboot call is not configurable nor is it
   necessary to invoke another program.
5. Trigger watchdog-protected reboots and shutdowns

## Timeouts and semantics

The following timeouts are important to `nerves_heart`:

* Erlang VM heart beat timeout - the time interval for Erlang heart beat messages
* Watchdog Timer (WDT) timeout - the time interval that before the WDT resets the device
* WDT pet timeout - the time interval that `nerves_heart` pets the WDT

The WDT pet timeout is always shorter than the WDT timeout. The Erlang VM heart
beat timer may be longer or shorter than the WDT timeout. Heart beat messages
from Erlang reset the heart beat timer and pet the WDT.  In the case that Erlang
sends heart beat messages frequent enough to satisfy the heart beat timeout, but
at a shorter interval than the WDT pet timeout, `nerves_heart` will
automatically pet the WDT.

In the event that Erlang is unresponsive and if `nerves_heart` is still able to
run, it will reboot the system by calling `sync(2)` and then `reboot(2)`. If
even `nerves_heart` is unresponsive, the WDT will reset the system. This will
also happen if `sync(2)` or `reboot(2)` hang `nerves_heart`.

If you stop the Erlang VM gracefully (such as by calling `:init.stop/0`), Erlang
will tell `nerves_heart` to exit without rebooting.  This, however, will stop
petting the WDT which will also lead to a system reset if the Linux kernel has
`CONFIG_WATCHDOG_NOWAYOUT=y` in its configuration. All official Nerves systems
specify this option.

WDT pet decisions made by `nerves_heart`:

1. Pet the WDT on start. This is important since it's unknown
   how much time has passed since the WDT has been started and when
   `nerves_heart` is started. This prevents a reboot due to a slow boot time.
2. Pet the WDT on Erlang heart beat messages AND pet timeout events. This makes
   the Erlang VM heart beat timeout the critical one in deciding when reboots
   happen.
3. Pet the WDT once on graceful shutdown and requested crash dumps. The
   intention is to give other shutdown code or crash dump preparation code the
   full WDT timeout interval.

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

## Linux kernel configuration

All official Nerves systems have Linux configured of Nerves Heart.

Nerves Heart expects the kernel watchdog device driver to be enabled with the no
way out option:

```text
CONFIG_WATCHDOG=y
CONFIG_WATCHDOG_NOWAYOUT=y
```

Linux provides several hardware watchdog driver so select the option that
matches your device. You may need to add a section to your device tree to
configure the driver. For example, if using an external watchdog and petting it
via a GPIO, you will need to specify which GPIO in the device tree.

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

## Reboot and power off

The `:heart.set_cmd/1` function can be used to trigger reboots and shutdowns.

To understand why, it's good to review the alternatives. The other ways of
rebooting with Nerves are either by exiting the Erlang VM via `:init.stop/0` or
by running the `reboot(8)` or `poweroff(8)` shell commands. The latter two work
by sending a signal to PID 1 (`erlinit`) that kills all OS processes and does
the reboot and power off. These work well in practice, but have had failures
blamed on them that can be further reduced by using Nerves Heart. For example,
one failure was found where OS processes couldn't be started and therefore
`reboot(8)` wasn't being called. This ended up causing a hang that wasn't
resolved for a long time.  Since using Nerves Heart to reboot does not create OS
processes, it would not have this issue and if communication problems between Nerves
Heart and Erlang arise, that will be detected by the heart beat timers. There's
lots to say about this topic. Suffice it to say that the heart process has
advantages in simplicity and control of the hardware WDT to make this process
more reliable.

To use, run either:

```elixir
iex> :heart.set_cmd("guarded_reboot")
```

or

```elixir
iex> :heart.set_cmd("guarded_poweroff")
```

Nerves Heart will notify PID 1 of the intention to reboot or poweroff. However,
the WDT will be pet for the last time. You should then call either
`:init.stop/0` (graceful) or `:erlang.halt/0` (ungraceful) to exit the Erlang
VM. Both `erlinit` and the WDT will prevent shutdown from not completing.

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
