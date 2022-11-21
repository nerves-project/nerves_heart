# Changelog

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
