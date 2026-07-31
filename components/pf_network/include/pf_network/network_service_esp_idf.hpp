#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pf_config/network_credentials.hpp"
#include "pf_config/weather_settings.hpp"
#include "pf_network/scan_results.hpp"
#include "pf_network/state_machine.hpp"
#include "pf_network/time_sync_state.hpp"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_network {

inline constexpr std::size_t kAccessPointSsidCapacity = 32U;
inline constexpr std::size_t kAccessPointPasswordCapacity = 16U;

struct AccessPointInfo {
    char ssid[kAccessPointSsidCapacity]{};
    char password[kAccessPointPasswordCapacity]{};
    char device_suffix[5]{};
};

enum class ScanState : std::uint8_t {
    idle,
    scanning,
    ready,
    failed,
};

inline constexpr std::size_t kMaximumScanResults = 16U;

struct ScanSnapshot {
    ScanState state = ScanState::idle;
    esp_err_t error = ESP_OK;
    std::uint32_t generation = 0U;
    std::size_t count = 0U;
    ScanResult results[kMaximumScanResults]{};
};

using AccessPointPresenter = esp_err_t (*)(
    void* context,
    const AccessPointInfo& info);

class NetworkService {
public:
    esp_err_t start(
        pf_runtime::RuntimeCoordinator& runtime,
        const pf_config::NetworkCredentials& credentials,
        bool configured,
        AccessPointPresenter presenter = nullptr,
        void* presenter_context = nullptr);

    bool request_scan();
    bool scan_snapshot(ScanSnapshot& destination);
    // Reported by whichever component first makes an outbound network
    // call (currently WeatherWorker); drives the otherwise-unfed
    // NetworkEvent::internet_reachable/unreachable transitions.
    bool report_internet_state(bool reachable);

private:
    static constexpr UBaseType_t kEventQueueLength = 8U;
    static constexpr std::uint32_t kTaskStackWords = 6144U;
    static constexpr UBaseType_t kTaskPriority = 5U;
    static constexpr TickType_t kStaAttemptTimeoutTicks =
        pdMS_TO_TICKS(15000U);
    static constexpr TickType_t kActionRetryDelayTicks =
        pdMS_TO_TICKS(1000U);
    static void task_entry(void* context);
    static void event_handler(
        void* context,
        esp_event_base_t event_base,
        std::int32_t event_id,
        void* event_data);
    static void sntp_synced_callback(struct timeval* synced_time);

    void task_main();
    void perform_action_chain(NetworkAction action);
    esp_err_t initialize_wifi();
    esp_err_t apply(NetworkAction action);
    esp_err_t start_station();
    esp_err_t start_access_point();
    void begin_scan();
    void collect_scan_results();
    void fail_scan(esp_err_t error);
    void publish_state();
    bool enqueue_event(NetworkEvent event);
    bool build_access_point_info();
    void maybe_start_sntp();

    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    pf_config::NetworkCredentials credentials_{};
    bool configured_ = false;
    AccessPointInfo access_point_{};
    NetworkStateMachine state_machine_{};
    TimeSyncState time_sync_state_ = TimeSyncState::unsynced;
    bool sntp_started_ = false;
    char sntp_server_buffer_[pf_config::kWeatherNtpServerCapacity]{};
    QueueHandle_t event_queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    StaticQueue_t event_queue_control_{};
    std::uint8_t event_queue_storage_[
        kEventQueueLength * sizeof(NetworkEvent)]{};
    StaticTask_t task_control_{};
    StackType_t task_stack_[kTaskStackWords]{};
    StaticSemaphore_t scan_mutex_control_{};
    SemaphoreHandle_t scan_mutex_ = nullptr;
    esp_event_handler_instance_t wifi_event_instance_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    AccessPointPresenter presenter_ = nullptr;
    void* presenter_context_ = nullptr;
    ScanSnapshot scan_snapshot_{};
    bool scan_request_pending_ = false;
    bool presentation_confirmed_ = false;
};

NetworkService& network_service();

}  // namespace pf_network
