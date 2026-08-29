#pragma once

#include <cstdint>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_config/sensor_settings.hpp"
#include "pf_sensors/daily_stats.hpp"
#include "pf_sensors/dht22_environment_sensor.hpp"
#include "pf_sensors/environment_sensor.hpp"
#include "pf_sensors/light_sensor.hpp"
#include "pf_sensors/presence.hpp"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_sensors {

// Owns the DHT22 (GPIO6) and both photoresistor ADC channels (GPIO5 /
// ADC1_CH4 and GPIO7 / ADC1_CH6) reads, runs the per-channel light
// filters, the combined threshold decision and the presence debounce each
// tick, and publishes results into RuntimeCoordinator. See
// docs/adr/0006-sensor-drivers-and-presence.md and
// docs/adr/0018-dual-photoresistor-channels.md.
class SensorTask {
public:
    SensorTask();

    esp_err_t start(pf_runtime::RuntimeCoordinator& runtime);

private:
    static constexpr std::uint32_t kTaskStackWords = 4096U;
    static constexpr UBaseType_t kTaskPriority = 3U;
    static constexpr TickType_t kLoopIntervalTicks = pdMS_TO_TICKS(2000U);
    static constexpr std::size_t kLightFilterCapacity = 8U;
    // Near-rail raw ADC readings (12-bit: 0-4095) still get a real reading
    // (low_clipped/high_clipped, ADR-0020) -- these are the boundaries past
    // which the ADC can no longer distinguish "very dark/bright" from
    // "even darker/brighter", not a stuck-or-disconnected marker.
    static constexpr int kSaturationLowRaw = 10;
    static constexpr int kSaturationHighRaw = 4085;

    static void task_entry(void* context);
    void task_main();
    void sample_light(const pf_config::SensorSettings& settings);
    void sample_environment(const pf_config::SensorSettings& settings);
    static std::uint64_t now_ms_since_boot();

    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    StaticTask_t task_control_{};
    StackType_t task_stack_[kTaskStackWords]{};

    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    // Per-channel, because one channel failing to configure must not take
    // the other one down with it.
    bool light_channel_ready_[kLightChannelCount]{};
    MovingAverageFilter<kLightFilterCapacity>
        light_filters_[kLightChannelCount]{};
    PresenceTracker presence_tracker_{};

    Dht22EnvironmentSensor environment_sensor_;
    EnvironmentCache environment_cache_{};
    SensorStatus environment_status_ = SensorStatus::disabled;
    DailyStats environment_daily_{};
};

SensorTask& sensor_task();

}  // namespace pf_sensors
