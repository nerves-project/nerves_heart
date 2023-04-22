# nerves_heart

[![CircleCI](https://circleci.com/gh/nerves-project/nerves_heart.svg?style=svg)](https://circleci.com/gh/nerves-project/nerves_heart)
[![REUSE status](https://api.reuse.software/badge/github.com/nerves-project/nerves_heart)](https://api.reuse.software/info/github.com/nerves-project/nerves_heart)

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
6. Support for an application-level initialization handshake. This lets your
   application guard against conditions that cause Erlang and Nerves Heart to
   think that everything is ok when it's not. See discussion below.
7. Support for snoozing timeouts and assuring an uninterrupted amount of time at
   start. This makes heart friendlier to remote debug where it's nice when the
   watchdog doesn't get in your way of debugging. These timeouts are limited to
   avoid accidentally putting the device in a state where it can't recover.
8. Includes many regression tests make it easier to modify and maintain.

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
-heart
-env HEART_BEAT_TIMEOUT 30
```

The heart beat timeout has to be greater than 10 seconds per the Erlang
documentation.

If you need to change the watchdog path, you can do this through an environment variable.

```erlang
-env HEART_WATCHDOG_PATH /dev/watchdog1
```

The following table shows the environment variables that affect Nerves Heart:

| Variable                 | Description |
| ------------------------ | ----------- |
| `ERL_CRASH_DUMP_SECONDS` | Timeout in seconds to wait for Erlang to exit |
| `HEART_BEAT_TIMEOUT`     | Used by Erlang to start `heart`. Erlang promises to pet `heart` before this timeout. |
| `HEART_INIT_TIMEOUT`     | If set, require an init handshake message before the timeout |
| `HEART_KILL_SIGNAL`      | Set to "SIGABRT" to send `SIGABRT` rather than `SIGKILL` |
| `HEART_INIT_GRACE_TIME`  | Grace period for Erlang at the start. E.g., if set to 120, then `heart` will pet the hardware watchdog for the first two minutes even if Erlang isn't responsive. |
| `HEART_NO_KILL`          | If "TRUE", don't try to kill Erlang before exiting |
| `HEART_VERBOSE`          | "0" turns off logging, "1" is error logs only, "2" is everything |
| `HEART_WATCHDOG_PATH`    | Path to hardware watchdog. Defaults to `"/dev/watchdog0"` |

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

## Application-level initialization handshake

Erlang's `heart` module has a very useful feature of letting you specify a
callback to extend the heart beat protection beyond the VM. See
[:heart.set_callback/2](https://www.erlang.org/doc/man/heart.html#set_callback-2).
To use it, you specify a callback function that returns `:ok` if your
application seems is ok. If it's not ok, then Erlang won't send a heart beat
message to the heart process and the device will eventually reboot.

The problem with this mechanism is that if something causes the code that
registers the callback to not be run, the device could continue to run without
knowing that its in a bad state. As an aside, a non-Nerves way of handling this
would be to exit the VM with an error message. On devices, running in a degraded
state can be useful to keep important services going and allowing time for
remote troubleshooting.

Here's the flow for using the application-level initialization handshake:

1. Set the `HEART_INIT_TIMEOUT` environment variable to the number of seconds to
   wait for the initializing handshake.
2. Call `:heart.set_callback/2` in your program
3. Call `Nerves.Runtime.Heart.init_complete/0` or `:heart.setcmd('init_done')`.
   If you call the latter, be sure to do it in a different process than the
   your heart callback uses or you'll get a hang.

To set `HEART_INIT_TIMEOUT`, edit the `rel/vm.args.eex` and update the heart
section to look like this:

```sh
## Enable heartbeat monitoring of the Erlang runtime system
-heart
-env HEART_BEAT_TIMEOUT 30

## Require an initialization handshake within 15 minutes
-env HEART_INIT_TIMEOUT 900
```

With this configuration, Nerves Heart will operate as normal with petting the
hardware watchdog and expecting Erlang heart beat messages up. If `init_done` is
passed, Nerves Heart will continue working as normal. However, if `init_done` is
not sent at the 15 minute mark, Nerves Heart will pet the hardware WDT one last
time and exit. This will cause the Erlang VM to exit and if the VM decides to
not be happy about this, the hardware WDT will reset.

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
 'program_name=nerves_heart\nprogram_version=2.0.0\nheartbeat_timeout=60\nheartbeat_time_left=53\nwdt_pet_time_left=103\nwdt_identity=OMAP Watchdog\nwdt_firmware_version=0\nwdt_options=settimeout,magicclose,keepaliveping,\nwdt_time_left=116\nwdt_pre_timeout=0\nwdt_timeout=120\nwdt_last_boot=power_on\n'}
```

The format is "key=value\n". The keys are either from `nerves_heart` or from
Linux watchdog `ioctl` functions.

If you're using Nerves, `Nerves.Runtime.Heart` has a friendlier interface:

```elixir
iex> Nerves.Runtime.Heart.status!
%{
  program_name: "nerves_heart",
  program_version: %Version{major: 2, minor: 0, patch: 0},
  heartbeat_timeout: 60,
  heartbeat_time_left: 53,
  init_handshake_happened: true,
  init_handshake_timeout: 900,
  init_handshake_time_left: 0,
  wdt_identity: "OMAP Watchdog",
  wdt_firmware_version: 0,
  wdt_last_boot: :power_on,
  wdt_options: [:settimeout, :magicclose, :keepaliveping],
  wdt_pre_timeout: 0,
  wdt_time_left: 116,
  wdt_pet_time_left: 103,
  wdt_timeout: 120
}
```

The following table describes keys and their values:

| Key | Description or value  |
| --- | --------------------- |
| `:program_name` | `"nerves_heart"` |
| `:program_version` | Nerves heart's version number  |
| `:heartbeat_timeout` | Erlang's heartbeat timeout setting. Note that the hardware watchdog timeout supersedes this since it reboots. |
| `:heartbeat_time_left` | The amount of time left for Erlang to send a heartbeat message before heart times out. |
| `:init_handshake_happened` | `true` if the initialization handshake happened or isn't enabled |
| `:init_handshake_timeout` | The time to wait for the handshake message before timing out |
| `:init_handshake_time_left` | If waiting for an initialization handshake, this is the number of seconds left. |
| `:wdt_identity` | The hardware watchdog that's being used  |
| `:wdt_firmware_version` | An integer that represents the hardware watchdog's firmware revision  |
| `:wdt_last_boot` | What caused the most recent boot. Whether this is reliable depends on the watchdog. |
| `:wdt_options` | Hardware watchdog options as reported by Linux  |
| `:wdt_pre_timeout` | How many seconds before the watchdog expires that Linux will receive a pre-timeout notification  |
| `:wdt_time_left` | How many seconds are left before the hardware watchdog triggers a reboot (depends on the kernel driver) |
| `:wdt_timeout` | The hardware watchdog timeout. This is only changeable in the Linux configuration |
| `:wdt_pet_time_left` | The time left before Nerves heart will pet the hardware WDT should everything remain ok |

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

IMPORTANT: `nerves_runtime` v0.13.2 and later makes these calls for you when
running `Nerves.Runtime.reboot/0` and `Nerves.Runtime.poweroff/0` if it detects
that this feature is supported.

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

## Snoozing

If you're debugging a watchdog or Erlang heart issue, it can be really helpful
to disable reboots temporarily. This is called snoozing and it works by telling
`heart` to pet the hardware watchdog and keep the Erlang VM running even if it
should reboot. This is effective for the next 15 minutes every time you run it.

```elixir
iex> :heart.set_cmd("snooze")
```

The reason snoozing forever isn't supported is to avoid keeping a device on
forever in a bad state. For example, it would be unfortunate to start a debug
session on a remote device and accidentally mess it up in a way where you lose
connectivity until the next reboot.

## Debugging

Nerves Heart writes to the kernel's logger to aid debug if something unexpected
happens.  Only errors are logged by default. If you'd like informational
messages as well, set the `HEART_VERBOSE` environment variable in your
`vm.args.eex`:

```erlang
-env HEART_VERBOSE 2
```

If you want Nerves Heart to be completely stealth in its prints for some
reason, set `HEART_VERBOSE` to `0`:

```erlang
-env HEART_VERBOSE 0
```

## License

This production code in this project is Erlang/OTP's `heart.c` with custom
modifications for use with Nerves.  This file is licensed under Apache-2.0.
Without additional modification, no other license is used.

This project does contain source with other licenses including GPL-2.0-only.
Files containing these licenses are clearly marked as required by the [REUSE
recommendations](https://reuse.software).

Exceptions to Apache-2.0 licensing are:

* Configuration and data files are licensed under CC0-1.0
* Documentation is CC-BY-4.0
* Linux kernel headers for MacOS development are GPL-2.0-only with the Linux
  syscall note.

