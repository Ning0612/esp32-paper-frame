#pragma once

#include <cstdint>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_config/sensor_settings.hpp"
#include "pf_dht22/dht22_environment_sensor.hpp"
#include "pf_sensors/daily_stats.hpp"
#include "pf_sensors/environment_sensor.hpp"
#include "pf_sensors/light_sensor.hpp"
#include "pf_sensors/presence.hpp"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_sensor_task {

// Owns the DHT22 (GPIO6) and photoresistor ADC (GPIO5 / ADC1_CH4) reads,
// runs the light filter/threshold/presence debounce each tick, and
// publishes results into RuntimeCoordinator. See
// docs/adr/0006-sensor-drivers-and-presence.md.
class SensorTask {
public:
    SensorTask();

    esp_err_t start(pf_runtime::RuntimeCoordinator& runtime);

private:
    static constexpr std::uint32_t kTaskStackWords = 4096U;
    static constexpr UBaseType_t kTaskPriority = 3U;
    static constexpr TickType_t kLoopIntervalTicks = pdMS_TO_TICKS(2000U);
    static constexpr std::size_t kLightFilterCapacity = 8U;
    // Near-rail raw ADC readings (12-bit: 0-4095) are treated as a stuck
    // or disconnected photoresistor rather than genuine darkness/glare.
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
    pf_sensors::MovingAverageFilter<kLightFilterCapacity> light_filter_{};
    pf_sensors::PresenceTracker presence_tracker_{};

    pf_dht22::Dht22EnvironmentSensor environment_sensor_;
    pf_sensors::EnvironmentCache environment_cache_{};
    pf_sensors::SensorStatus environment_status_ =
        pf_sensors::SensorStatus::disabled;
    pf_sensors::DailyStats environment_daily_{};
};

SensorTask& sensor_task();

}  // namespace pf_sensor_task
