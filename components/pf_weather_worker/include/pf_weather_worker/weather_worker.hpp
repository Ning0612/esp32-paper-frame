#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pf_weather/weather.hpp"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_network {
class NetworkService;
}

namespace pf_weather_worker {

// Fetches OpenWeatherMap current-weather data over HTTPS on its own task,
// publishes the result into RuntimeCoordinator, and reports whether the
// attempt reached a server at all (used to drive NetworkService's
// otherwise-unfed internet reachability signal). See
// docs/adr/0005-weather-worker-and-status-bar.md for the endpoint, TLS
// trust, cache schema, and rate-limit decisions this implements.
class WeatherWorker {
public:
    esp_err_t start(
        pf_runtime::RuntimeCoordinator& runtime,
        pf_network::NetworkService& network);

    // Wakes the worker immediately instead of waiting for the next
    // scheduled attempt; called after a settings save so a changed
    // location/API key takes effect without waiting out the old interval.
    void request_immediate_refresh();

private:
    static constexpr std::uint32_t kTaskStackWords = 6144U;
    static constexpr UBaseType_t kTaskPriority = 3U;
    static constexpr std::uint32_t kTimeSyncPollMs = 2000U;
    static constexpr std::uint32_t kTimeSyncTimeoutMs = 5U * 60U * 1000U;
    static constexpr std::uint32_t kHttpTimeoutMs = 10000U;
    static constexpr std::size_t kResponseBufferCapacity = 4096U;
    static constexpr std::size_t kUrlCapacity = 320U;

    static void task_entry(void* context);
    static esp_err_t http_event_handler(esp_http_client_event_t* event);

    void task_main();
    void wait_for_time_sync();
    void fetch_once();
    void apply_failure(pf_weather::Failure failure);
    void publish();
    void report_internet(bool reachable);
    static std::uint64_t now_ms_since_boot();

    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    pf_network::NetworkService* network_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    StaticTask_t task_control_{};
    StackType_t task_stack_[kTaskStackWords]{};
    SemaphoreHandle_t wake_semaphore_ = nullptr;
    StaticSemaphore_t wake_semaphore_control_{};

    pf_weather::Cache cache_{};
    char cached_units_[pf_weather::kUnitsCapacity]{};
    char response_buffer_[kResponseBufferCapacity]{};
    std::size_t response_length_ = 0U;
    bool response_truncated_ = false;
    char url_buffer_[kUrlCapacity]{};
    // Set by request_immediate_refresh() (a different task) and consumed
    // by task_main(); an atomic keeps that cross-task signal race-free
    // without needing a mutex just for one flag.
    std::atomic<bool> force_refresh_{false};
};

WeatherWorker& weather_worker();

}  // namespace pf_weather_worker
