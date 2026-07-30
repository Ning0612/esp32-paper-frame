# Weather data contract

`pf_weather` currently owns the host-testable part of the weather flow. It
parses the bounded OpenWeather current-weather response used by the future
HTTPS worker and keeps the last successful observation when a later request
fails.

The parser accepts metric `main.temp`, `main.humidity`, the first
`weather[]` item (`id`, `description`, `icon`), `dt`, and the optional `name`.
An HTTP/API rejection is reported separately from malformed or incomplete
JSON. The API key and transport are intentionally not part of this component;
the next integration slice will provide the ESP-IDF HTTPS worker and NVS
configuration while preserving these result categories.

Cache updates happen only after a complete, validated observation. Failures
retain the previous observation, record a failure category, and schedule a
bounded exponential retry (10 seconds through 60 minutes). A successful fetch
resets the backoff and schedules the normal 10-minute refresh interval.

## Persisted settings

Weather settings are stored in the independent NVS namespace `pf_weather`.
The record includes latitude/longitude (microdegrees), update interval, API
key, display location, units, language, and NTP server. The record is versioned
and protected by CRC32; a missing record uses the safe Taipei/metric defaults.
The API key is never returned by the management API: callers receive only an
`api_key_set` boolean.
