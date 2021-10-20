# Changelog

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
