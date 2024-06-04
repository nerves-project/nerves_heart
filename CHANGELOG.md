# Changelog

## v2.4.0

* Changes
  * Support modifying the kernel's default watchdog timeout via the
    `WDIOF_SETTIMEOUT` ioctl. See the `HEART_KERNEL_TIMEOUT` option. Thanks to
    @ringlej.

## v2.3.0

* Changes
  * Add guarded immediate reboot and poweroff commands. These bypass all
    graceful shutdown code and should be used with care. Like the regular
    guarded reboot and power they stop petting the watchdog too.

## v2.2.0

* Changes
  * Added snooze support to turn off reboots for 15 minutes per snooze. This is
    super helpful for debugging issues that interact with device health checks.
    It's activated via either a `USR1` signal or the `"snooze"` command.
  * Support an initial grace period to avoid hardware watchdog reboots shortly
    after starting up. This lets you ensure that devices are up for a short
    period of time to make them debuggable. This helps with remote devices that
    may not connect to the network quickly enough to be easily debugged.
  * Support 2 second hardware watchdog timeouts. Previously the limit was 5
    seconds. 2 seconds is the shortest possible since the pet timer resolution
    is in seconds.

## v2.1.0

* Changes
  * Add `HEART_VERBOSE` environment variable for controlling logging output. The
    default (`1`) is to only log errors and important messages. `2` will output
    informational messages and `0` silences everything.

## v2.0.2

* Changes
  * Fix signal sent for guarded poweroffs
  * Support guarded halts (oversight from v2.0.0)

## v2.0.1

* Changes
  * Fix last wdt pet time statistic when the wdt isn't available. This only
    changes the value reported. Previously, the pet time was updated on all
    attempts even if they were unsuccessful.

## v2.0.0

This is a major update to Nerves Heart.

* Changes
  * BREAKING: the informational attribute names changed to clarify whether they
    came from the watchdog timer device driver or Nerves heart
  * Added an optional initialization handshake to protect against
    `:heart.set_callback/2` not being run and an issue going undetected.
  * Added support for guarded reboot and poweroff requests. These work similar
    to the `reboot` and `poweroff` shell commands, but stop petting the watchdog
    as well. This protects against rare reboot/poweroff hangs.
  * Pet the hardware watchdog before exiting to reduce the chance of it
    rebooting the system early due to unlucky timing from the previous pet.
  * Remove hardcoded hardware watchdog pet time and calculate based on actual
    timeout value.

## v1.2.0

This is a significant update since it adds a regression test framework.
Previously, we had so few changes that we trusted the OTP team's coverage,
visual inspection and spot checks. No major feature updates are in this release,
but this unblocks the addition of future updates.

* Changes
  * Add `disable_vm` command to enable testing of `heart` timing out the Erlang
    VM. For consistency, this adds `disable_hw` for disabling the petting of the
    hardware watchdog to verify that failure path. The `disable` command maps to
    `disable_hw` for backwards compatibility.
  * Call `sync(2)` before rebooting due to Erlang VM unresponsiveness to reduce
    data loss.
  * Open `/dev/watchdog0` on start. On systems that don't start the hardware
    watchdog on boot, this starts it as soon as possible to avoid the gap until
    the first "pet" or select timeout to start it.

## v1.1.0

* Changes
  * Several `:heart.get_cmd/0` status info updates:
    * Decode hardware watchdog options (no more hex number)
    * Add the configured heart timeout
    * If no hardware watchdog, fill out fields with default values

## v1.0.0

* New features
  * Add diagnostic information to `:heart.get_cmd/0`

## v0.3.1

* New features
  * Identify `nerves_heart` version on start to make it easier to debug watchdog
    issues due to running Erlang heart instead of `nerves_heart`
  * Sync with OTP-24 heart changes. All changes were minor and nothing that
    Nerves users should notice.

## v0.3.0

* New features
  * `HEART_WATCHDOG_PATH` environment variable for configurable watchdog device
    path
  * Test command for disabling the watch dog for test purposes is now `disable`

## v0.2.0

* New features
  * Log to the kernel log if available. On Nerves, this is routed to the Elixir
    logger so messages from heart should no longer be lost.

* Bug fixes
  * Pull in upstream message length fix

## v0.1.0

* Initial release
