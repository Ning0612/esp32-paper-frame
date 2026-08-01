# ADR-0005：WeatherWorker HTTPS 契約與狀態列渲染基線

- Status: accepted
- Date: 2026-07-31
- Supersedes: none

## Context

`docs/IMPLEMENTATION_PLAN.md` 的 Gate G7（"OpenWeatherMap endpoint/version、
TLS trust、cache schema 與 rate limit"）與狀態列 renderer 的字型/圖示來源
必須在 Phase 6 開工前固定，不能沿用草案「建議」直接實作。掃描確認：全專案
目前沒有任何 `esp_http_client`/`esp_tls`/`esp_crt_bundle` 用例、SNTP/mDNS
完全未整合、狀態列目前是整片填白（無文字/圖示繪製能力）。本 ADR 記錄
WeatherWorker（`pf_weather_worker` component）與狀態列渲染（`pf_display`
的 bitmap font/icon 原語）實作前必須固定的決策。

## Decision

### G7：OpenWeatherMap endpoint / TLS trust / cache schema / rate limit

- **Endpoint**：`GET https://api.openweathermap.org/data/2.5/weather`，
  查詢參數 `lat`、`lon`（由 `WeatherSettings.latitude_e6`/`longitude_e6`
  換算為十進位度）、`appid`、`units`、`lang`。對應既有
  `pf_weather::parse_current_weather` 已假設的回應欄位
  （`main.temp`、`main.humidity`、`weather[0].id/description/icon`、
  `dt`、`name`），不需要改動 parser 契約。
- **TLS trust**：使用 `esp_crt_bundle_attach`（`esp_http_client_config_t`
  的 `crt_bundle_attach` 欄位）。`sdkconfig.paperframe-s3` 已啟用
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` 與
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y`，不釘選、不內嵌自訂
  憑證。TLS 憑證有效期驗證需要正確的系統時間，因此 WeatherWorker 在首次
  請求前必須等待 `RuntimeSnapshot.time_sync == synced`（見下方 SNTP 決策）。
- **Cache schema**：直接複用既有 `pf_weather::Observation`/`Cache`
  （`components/pf_weather/include/pf_weather/weather.hpp`），不重新定義。
  `RuntimeSnapshot` 內嵌一份精簡欄位（`weather_observation`、
  `weather_units`、`weather_has_observation`、
  `weather_last_success_epoch_s`、`weather_consecutive_failures`、
  `weather_last_failure`），值語意複製，不使用指標，維持
  `RuntimeSnapshot` 的 trivially-copyable 契約。
- **Rate limit / 更新頻率**：改採 `WeatherSettings.update_interval_minutes`
  （使用者可設 10–1440 分鐘）取代 `pf_weather::kUpdateIntervalMs` 目前寫死
  的 10 分鐘；`pf_weather::record_success` 擴充一個 `interval_ms` 參數，
  預設值等於原常數以維持既有呼叫端相容。OpenWeatherMap 免費額度為
  60 requests/min，在 10–1440 分鐘區間內完全無虞，不需要額外的用戶端節流。

### SNTP：最小可用的時間同步

- NetworkServiceTask（既有 `pf_network::NetworkService`，
  `docs/IMPLEMENTATION_PLAN.md` 第 60 行已將其列為 SNTP owner）在 STA
  首次取得 IP 後啟動 SNTP：`esp_netif_sntp_init` +
  `ESP_NETIF_SNTP_DEFAULT_CONFIG`，伺服器名稱來自
  `WeatherSettings.ntp_server`（預設 `pool.ntp.org`），透過
  `sntp_set_time_sync_notification_cb` 註冊回呼。
- 新增 `pf_network::TimeSyncState { unsynced, syncing, synced }`
  （純邏輯，`components/pf_network/include/pf_network/time_sync_state.hpp`）。
  刻意不設 `failed` 狀態：ESP-IDF 的 SNTP 客戶端沒有終止性失敗的概念，
  啟動後會持續在背景重試，沒有事件可以驅動這樣的狀態。
- `RuntimeSnapshot` 鏡射一份同名 `TimeSyncState`（避免 `pf_runtime`
  反向依賴 `pf_network`，沿用既有 `WifiState`/`InternetState` 的鏡射＋
  轉譯慣例），`RuntimeCoordinator::update_time_sync(...)` 是唯一寫入口。
- 本次只處理「STA 連上後啟動 SNTP、追蹤 synced 狀態」，不處理 AP/
  Recovery AP 模式下的時間行為，也不做未同步狀態的完整診斷分類；
  WeatherWorker 與狀態列渲染只需要一個布林等級的「時間是否可信」訊號。

### Internet 可達性訊號

- `NetworkStateMachine` 已定義 `internet_reachable`/`internet_unreachable`
  事件，但全專案沒有任何呼叫端觸發，導致 `internet` 狀態永遠停在
  `unknown`。WeatherWorker 是第一個會發出 HTTPS 請求的元件，因此由它
  透過 `NetworkService::report_internet_state(bool reachable)`
  （比照 `request_scan()` 的直接呼叫模式）回報：
  - `pf_weather::Cache::Failure::network`（DNS 解析、連線建立、TLS
    handshake 失敗）→ `unreachable`。
  - 其餘結果（成功、`api_key_invalid`、`http_error`、`parse_error`——
    皆代表已收到 HTTP 回應，代表連線本身是通的）→ `reachable`。
- 對應 `docs/IMPLEMENTATION_PLAN.md` 第 203 行的既有原則：
  「DNS、weather 或 Internet 錯誤只改變 Internet 狀態，不進 AP」。

### 字型與圖示授權

- 狀態列渲染（日期、星期、weather icon、temperature、stale 標記）採用
  **自製最小點陣字型**（僅涵蓋數字、`:`、`/`、`-`、`°`、`%`、星期縮寫
  所需的拉丁字母）與 **9 組簡化天氣圖示分類**（對映 OpenWeatherMap
  icon code 的天氣狀況群組），皆為本專案原創點陣資料，不引入任何
  第三方字型或圖示檔案，避免授權查證與再散布限制的負擔。

## Consequences

- `pf_weather_worker` 是本專案第一個使用 `esp_http_client`/`esp-tls`/
  `mbedtls` 的 component；CMakeLists 需新增對應 `REQUIRES`。
- WeatherWorker 讀取設定一律呼叫 `pf_config::load_weather_settings()`
  直接讀 NVS，不與 `health_server.cpp` 內的 `server_access_config` 共用
  記憶體狀態，避免新增跨 component 耦合；使用者存檔後由
  `process_weather_config()` 呼叫 WeatherWorker 的
  `request_immediate_refresh()` 提前喚醒，不必等到下一個排程週期。
- 狀態列內容更新沿用既有整頁 refresh 節奏（不新增 `CommandKind`、不做
  局部刷新驅動）；天氣本身 10–1440 分鐘的更新頻率與既有 5–30 分鐘
  carousel refresh 週期已經匹配，此設計為刻意簡化，非遺漏。
- 未來若要改變 endpoint、TLS 策略或字型/圖示來源，需要新的 superseding
  ADR，而不是直接修改程式碼繞過本決策。

## Verification

- `pio run` 與 `pio test -e native` 全綠是本 ADR 所涵蓋所有變更的最低
  驗證門檻；HTTPS/SNTP 的實際網路行為無法 host test，須在
  `docs/hardware/VALIDATION.md` 記錄實機驗證（API key invalid、
  DNS/TLS/timeout 各自診斷狀態、stale 顯示、未同步時間 fallback）。
- `esp_crt_bundle_attach` 驗證 `api.openweathermap.org` 憑證成功，是
  WeatherWorker 第一次成功抓取的前提條件，須在實機記錄。

## Update (2026-08)

反過度設計整併：`pf_weather_worker` component 已併入 `pf_weather`，
namespace 由 `pf_weather_worker` 改為 `pf_weather`（`WeatherWorker` 類別
本體與本 ADR 記錄的所有決策不變，純粹是元件邊界調整，不是 superseding
ADR 的範疇）。本文其餘內容提到的 `pf_weather_worker` 是撰寫當下的元件
名稱，保留作歷史紀錄。
