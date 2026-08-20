# Weather data contract

`pf_weather` owns both the host-testable weather logic and the ESP-IDF HTTPS
worker that fetches from OpenWeather. It parses the bounded current-weather
response and keeps the last successful observation when a later request
fails.

The parser accepts metric `main.temp`, `main.humidity`, the first
`weather[]` item (`id`, `description`, `icon`), `dt`, and the optional `name`.
An HTTP/API rejection is reported separately from malformed or incomplete
JSON.

## 失敗分類與 internet 可達性

`Failure` 有四種，判定散在兩處：`classify_http_status()` 處理正常收到回應的
狀態碼，`classify_perform_failure()` 處理 `esp_http_client_perform()` 失敗但
client 仍記錄到狀態碼的情況。後者存在的原因是 ESP-IDF 對「401 且回應未帶
`WWW-Authenticate`」會直接回 `ESP_ERR_NOT_SUPPORTED`（`esp_http_client.c` 中
`auth_header == NULL` 的分支），而 OpenWeather 的無效 key 回應正是這種——
若不在該分支分類，打錯 key 會被報成網路故障。

| 情況 | `Failure` | internet |
| --- | --- | --- |
| 無狀態碼（DNS／TCP／TLS 失敗；ESP-IDF 回 `-1`） | `network` | unreachable |
| 401 | `api_key_invalid` | reachable |
| 其他 4xx／5xx | `http_error` | reachable |
| 2xx 或殘留 3xx 但 perform 失敗（body 中斷） | `http_error` | reachable |
| 200 但 JSON 缺欄位或語法錯誤 | `parse_error` | reachable |

**收到任何 HTTP 狀態碼就代表請求離開了 LAN**，因此只有「完全沒有回應」才回報
internet unreachable。把伺服器已回應的情況標成 `network` 會同時污染錯誤訊息與
Dashboard 的可達性顯示。四種分類都有實機驗證，見
[`docs/hardware/VALIDATION.md`](hardware/VALIDATION.md) 2026-08-20 段落。

天氣抓取沒有週期計時器（ADR-0014）：worker 成功後把 `next_attempt_ms` 設為
`kNoAutomaticRetry`，只有面板刷新被接受時 app_main 呼叫的
`request_immediate_refresh()` 會喚醒它；失敗則走既有的指數退避。

API 金鑰存放於 NVS（`WeatherSettings`），只以 `api_key_set` 布林值對外回報，
不出現在任何 API 回應或 log 中。

Cache updates happen only after a complete, validated observation. Failures
retain the previous observation, record a failure category, and schedule a
bounded exponential retry (10 seconds through 60 minutes), unchanged by
ADR-0014. A successful fetch resets the backoff and schedules no further
automatic attempt (see ADR-0014): the next fetch only happens when something
calls `WeatherWorker::request_immediate_refresh()`.

## Persisted settings

Weather settings are stored in the independent NVS namespace `pf_weather`.
The record includes latitude/longitude (microdegrees), API key, units, and
NTP server. The record is versioned and protected by CRC32; a missing record
uses the safe Taipei/metric defaults. The API key is never returned by the
management API: callers receive only an `api_key_set` boolean.

There is no user-configurable update interval or display language/location
(see [ADR-0014](adr/0014-weather-panel-refresh-cadence-and-map-picker.md)):
the API request language is fixed to English, and refreshes are triggered by
`WeatherWorker::request_immediate_refresh()` right after the carousel accepts
a real panel refresh submission, instead of running on a periodic timer. The
WebUI's latitude/
longitude fields are set either by typing microdegrees directly or through a
map coordinate picker (online OpenStreetMap tiles with a fixed center pin,
falling back to a canvas-drawn graticule when offline).
