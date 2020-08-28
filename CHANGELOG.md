# Changelog

## Unreleased
* New features
  * HEART_WATCHDOG_PATH environment variable for configurable watchdog device path

## v0.2.0

* New features
  * Log to the kernel log if available. On Nerves, this is routed to the Elixir
    logger so messages from heart should no longer be lost.

* Bug fixes
  * Pull in upstream message length fix

## v0.1.0

* Initial release
