#include "pf_weather/weather_worker.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pf_config/config_manager.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_network/network_service_esp_idf.hpp"
#include "pf_runtime/runtime_coordinator.hpp"

namespace pf_weather {
namespace {

constexpr char kTag[] = "pf_weather";

void bounded_copy(
    char* const destination,
    const std::size_t destination_capacity,
    const char* const source)
{
    if (destination == nullptr || destination_capacity == 0U) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    std::size_t index = 0U;
    while (index < destination_capacity - 1U && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

// True for the load_weather_settings() error codes that mean "the stored
// blob doesn't match the current WeatherSettingsBlob layout" (schema
// version bump, corrupted/truncated record) rather than "NVS itself is
// broken". Only these are safe to treat as "use defaults and keep trying"
// -- a genuine NVS driver/open failure should still count as a fetch
// failure instead of silently masking a real storage problem.
// config_manager.cpp's load_weather_settings() normalizes NVS-specific
// size errors (e.g. ESP_ERR_NVS_INVALID_LENGTH from a pre-upgrade blob
// that no longer fits the current struct) into ESP_ERR_INVALID_SIZE, so
// this component only needs to know these two plain esp_err.h codes.
bool is_recoverable_schema_mismatch(const esp_err_t error)
{
    return error == ESP_ERR_INVALID_CRC || error == ESP_ERR_INVALID_SIZE;
}

}  // namespace

esp_err_t WeatherWorker::start(
    pf_runtime::RuntimeCoordinator& runtime,
    pf_network::NetworkService& network)
{
    if (task_handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime_ = &runtime;
    network_ = &network;

    wake_semaphore_ =
        xSemaphoreCreateBinaryStatic(&wake_semaphore_control_);
    if (wake_semaphore_ == nullptr) {
        runtime_ = nullptr;
        network_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    task_handle_ = xTaskCreateStatic(
        &WeatherWorker::task_entry,
        "WeatherWorkerTask",
        kTaskStackWords,
        this,
        kTaskPriority,
        task_stack_,
        &task_control_);
    if (task_handle_ == nullptr) {
        wake_semaphore_ = nullptr;
        runtime_ = nullptr;
        network_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void WeatherWorker::request_immediate_refresh()
{
    force_refresh_.store(true, std::memory_order_relaxed);
    if (wake_semaphore_ != nullptr) {
        xSemaphoreGive(wake_semaphore_);
    }
}

void WeatherWorker::task_entry(void* const context)
{
    static_cast<WeatherWorker*>(context)->task_main();
}

std::uint64_t WeatherWorker::now_ms_since_boot()
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

void WeatherWorker::wait_for_time_sync()
{
    std::uint32_t waited_ms = 0U;
    while (waited_ms < kTimeSyncTimeoutMs) {
        pf_runtime::RuntimeSnapshot snapshot{};
        if (runtime_ != nullptr &&
            runtime_->read_snapshot(snapshot) &&
            snapshot.time_sync == pf_runtime::TimeSyncState::synced) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(kTimeSyncPollMs));
        waited_ms += kTimeSyncPollMs;
    }
    ESP_LOGW(
        kTag,
        "time_sync_wait_timeout=%lums; attempting fetch anyway",
        static_cast<unsigned long>(kTimeSyncTimeoutMs));
}

void WeatherWorker::task_main()
{
    wait_for_time_sync();

    while (true) {
        const std::uint64_t now_ms = now_ms_since_boot();
        const bool forced =
            force_refresh_.exchange(false, std::memory_order_relaxed);
        if (forced || pf_weather::retry_due(cache_, now_ms)) {
            fetch_once();
        }

        const std::uint64_t after_ms = now_ms_since_boot();
        // next_attempt_ms == UINT64_MAX means "no automatic retry
        // scheduled" (see the record_success() call above); computing
        // pdMS_TO_TICKS() on that magnitude would overflow, so wait
        // indefinitely for request_immediate_refresh() instead.
        TickType_t wait_ticks = portMAX_DELAY;
        if (cache_.next_attempt_ms != UINT64_MAX) {
            wait_ticks = cache_.next_attempt_ms > after_ms
                             ? pdMS_TO_TICKS(cache_.next_attempt_ms - after_ms)
                             : pdMS_TO_TICKS(1000U);
        }
        xSemaphoreTake(wake_semaphore_, wait_ticks);
    }
}

esp_err_t WeatherWorker::http_event_handler(
    esp_http_client_event_t* const event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == nullptr) {
        return ESP_OK;
    }
    // esp_http_client already de-chunks Transfer-Encoding: chunked bodies
    // before delivering HTTP_EVENT_ON_DATA, so chunked responses are
    // accumulated the same way as any other response.
    WeatherWorker& self = *static_cast<WeatherWorker*>(event->user_data);
    const std::size_t remaining =
        sizeof(self.response_buffer_) - 1U - self.response_length_;
    const std::size_t incoming =
        static_cast<std::size_t>(event->data_len);
    const std::size_t to_copy = std::min<std::size_t>(remaining, incoming);
    if (to_copy > 0U) {
        std::memcpy(
            self.response_buffer_ + self.response_length_,
            event->data,
            to_copy);
        self.response_length_ += to_copy;
        self.response_buffer_[self.response_length_] = '\0';
    }
    if (to_copy < incoming) {
        self.response_truncated_ = true;
    }
    return ESP_OK;
}

void WeatherWorker::apply_failure(const pf_weather::Failure failure)
{
    pf_weather::record_failure(cache_, failure, now_ms_since_boot());
    publish();
}

void WeatherWorker::publish()
{
    if (runtime_ != nullptr) {
        runtime_->update_weather(cache_, cached_units_);
    }
}

void WeatherWorker::report_internet(const bool reachable)
{
    if (network_ != nullptr) {
        network_->report_internet_state(reachable);
    }
}

void WeatherWorker::fetch_once()
{
    // A recoverable schema-mismatch load failure (e.g. a pre-upgrade blob
    // whose size no longer matches WeatherSettingsBlob after a schema
    // version bump) still yields safe Taipei/metric defaults in
    // settings_result.settings -- mirrors app_main.cpp's
    // "weather_settings_load_failed=...; using defaults" handling instead
    // of treating it as a fetch failure. A genuine NVS-level failure (e.g.
    // the namespace itself won't open) is not silently papered over the
    // same way: it still counts as Failure::network so a real storage
    // problem isn't misreported as (say) an invalid API key.
    const pf_config::WeatherSettingsLoadResult settings_result =
        pf_config::load_weather_settings();
    if (settings_result.error != ESP_OK &&
        !is_recoverable_schema_mismatch(settings_result.error)) {
        ESP_LOGW(
            kTag,
            "weather_settings_load_failed=%s",
            esp_err_to_name(settings_result.error));
        apply_failure(pf_weather::Failure::network);
        return;
    }
    if (settings_result.error != ESP_OK) {
        ESP_LOGW(
            kTag,
            "weather_settings_load_failed=%s; using defaults",
            esp_err_to_name(settings_result.error));
    }
    const pf_config::WeatherSettings& settings = settings_result.settings;

    if (settings.api_key[0] == '\0') {
        // No point spending a real HTTPS round trip: an empty key is
        // guaranteed to come back 401. Record the state so the dashboard
        // shows "not configured" and schedule no automatic retry --
        // saving a valid key already wakes the worker via
        // request_immediate_refresh(), so there is nothing to gain by
        // polling on a timer for a key that clearly isn't there yet.
        ESP_LOGW(kTag, "weather_api_key_not_configured; skipping fetch");
        cache_.last_failure = pf_weather::Failure::api_key_invalid;
        cache_.next_attempt_ms = UINT64_MAX;
        publish();
        return;
    }

    // Bounded, guaranteed-NUL-terminated local copies: WeatherSettings is
    // decoded from NVS via a separate path and this function has no way
    // to re-verify that contract, so it does not trust %s formatting
    // fixed-size fields directly.
    char api_key[sizeof(settings.api_key)]{};
    const pf_config::SecureZeroGuard api_key_guard(api_key);
    char units[sizeof(settings.units)]{};
    bounded_copy(api_key, sizeof(api_key), settings.api_key);
    bounded_copy(units, sizeof(units), settings.units);

    // url_buffer_ embeds the API key in its query string; zero it once
    // this function returns (any path) instead of leaving the key sitting
    // in this member until the next fetch overwrites it.
    const pf_config::SecureZeroGuard url_buffer_guard(url_buffer_);

    // Language is fixed to English (ADR-0014): it is not user-configurable
    // so the internal weather-condition/icon mapping only has one string
    // table to stay in sync with.
    const int url_written = std::snprintf(
        url_buffer_,
        sizeof(url_buffer_),
        "https://api.openweathermap.org/data/2.5/weather"
        "?lat=%.6f&lon=%.6f&appid=%s&units=%s&lang=en",
        static_cast<double>(settings.latitude_e6) / 1000000.0,
        static_cast<double>(settings.longitude_e6) / 1000000.0,
        api_key,
        units);
    if (url_written < 0 ||
        static_cast<std::size_t>(url_written) >= sizeof(url_buffer_)) {
        ESP_LOGW(kTag, "weather_url_build_failed_or_truncated");
        apply_failure(pf_weather::Failure::network);
        return;
    }

    response_length_ = 0U;
    response_truncated_ = false;
    response_buffer_[0] = '\0';

    esp_http_client_config_t config{};
    config.url = url_buffer_;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = static_cast<int>(kHttpTimeoutMs);
    config.crt_bundle_attach = &esp_crt_bundle_attach;
    config.event_handler = &WeatherWorker::http_event_handler;
    config.user_data = this;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        apply_failure(pf_weather::Failure::network);
        return;
    }

    const esp_err_t perform_result = esp_http_client_perform(client);
    if (perform_result != ESP_OK) {
        ESP_LOGW(
            kTag,
            "weather_fetch_failed=%s internal_free=%u internal_largest=%u "
            "dma_free=%u dma_largest=%u",
            esp_err_to_name(perform_result),
            static_cast<unsigned>(
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
        esp_http_client_cleanup(client);
        apply_failure(pf_weather::Failure::network);
        report_internet(false);
        return;
    }
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    const pf_weather::Failure http_failure =
        pf_weather::classify_http_status(status_code);
    if (http_failure != pf_weather::Failure::none) {
        ESP_LOGW(kTag, "weather_http_status=%d", status_code);
        apply_failure(http_failure);
        report_internet(true);
        return;
    }

    if (response_truncated_) {
        ESP_LOGW(
            kTag,
            "weather_response_truncated capacity=%u",
            static_cast<unsigned>(sizeof(response_buffer_) - 1U));
        apply_failure(pf_weather::Failure::http_error);
        report_internet(true);
        return;
    }

    const pf_weather::ParseResult parsed = pf_weather::parse_current_weather(
        response_buffer_,
        response_length_);
    if (!parsed.ok()) {
        ESP_LOGW(
            kTag,
            "weather_parse_failed=%s",
            pf_weather::to_string(parsed.error));
        apply_failure(pf_weather::Failure::parse_error);
        report_internet(true);
        return;
    }

    const std::time_t wall_clock_now = std::time(nullptr);
    if (wall_clock_now == static_cast<std::time_t>(-1)) {
        ESP_LOGW(kTag, "weather_wall_clock_unavailable");
        apply_failure(pf_weather::Failure::parse_error);
        report_internet(true);
        return;
    }

    bounded_copy(cached_units_, sizeof(cached_units_), units);
    // No periodic follow-up: the next fetch is woken externally by
    // request_immediate_refresh() right after the carousel's next panel
    // refresh is accepted (see app_main.cpp), not by a user-configured
    // interval.
    // UINT64_MAX saturates next_attempt_ms so retry_due() stays false
    // until something calls request_immediate_refresh() again.
    pf_weather::record_success(
        cache_,
        parsed.observation,
        static_cast<std::uint64_t>(wall_clock_now),
        now_ms_since_boot(),
        UINT64_MAX);
    report_internet(true);
    publish();
}

WeatherWorker& weather_worker()
{
    static WeatherWorker instance;
    return instance;
}

}  // namespace pf_weather
