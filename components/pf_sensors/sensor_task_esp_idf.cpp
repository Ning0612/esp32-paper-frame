#include "pf_sensors/sensor_task.hpp"

#include <ctime>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pf_config/config_manager.hpp"
#include "pf_runtime/runtime_coordinator.hpp"

namespace pf_sensors {
namespace {

constexpr char kTag[] = "pf_sensors";
// ADR-0003: DHT22 data = GPIO6, photoresistor channel 1 = GPIO5
// (ADC1_CH4). ADR-0018 adds channel 2 = GPIO7 (ADC1_CH6). Both channels
// are on ADC1 because ADC2 is claimed by the Wi-Fi driver and reads back
// ESP_ERR_TIMEOUT while the radio is up.
//
// Must stay in step with pf_sensors::kLightChannelGpios (light_sensor.hpp),
// which is what the API reports: these are two views of the same pins
// (GPIO5 = ADC1_CH4, GPIO7 = ADC1_CH6). Moving a channel means changing both.
constexpr adc_channel_t kLightAdcChannels[kLightChannelCount] = {
    ADC_CHANNEL_4,
    ADC_CHANNEL_6,
};
constexpr gpio_num_t kDhtPin = GPIO_NUM_6;

}  // namespace

SensorTask::SensorTask() : environment_sensor_(kDhtPin)
{
}

esp_err_t SensorTask::start(pf_runtime::RuntimeCoordinator& runtime)
{
    if (task_handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime_ = &runtime;

    const adc_oneshot_unit_init_cfg_t init_config{
        .unit_id = ADC_UNIT_1,
        .clk_src = static_cast<adc_oneshot_clk_src_t>(0),
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t result = adc_oneshot_new_unit(&init_config, &adc_handle_);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "adc_unit_init_failed=%s; light sensing disabled",
            esp_err_to_name(result));
        adc_handle_ = nullptr;
    } else {
        const adc_oneshot_chan_cfg_t channel_config{
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        // A channel that fails to configure is left not-ready on its own;
        // the other one keeps working, matching the rule that sensors are
        // optional and degrade individually.
        for (std::size_t index = 0U; index < kLightChannelCount; ++index) {
            const esp_err_t channel_result = adc_oneshot_config_channel(
                adc_handle_, kLightAdcChannels[index], &channel_config);
            light_channel_ready_[index] = channel_result == ESP_OK;
            if (channel_result != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "adc_channel_config_failed=%s channel=%u; "
                    "light channel disabled",
                    esp_err_to_name(channel_result),
                    static_cast<unsigned>(index + 1U));
            }
        }
    }

    task_handle_ = xTaskCreateStatic(
        &SensorTask::task_entry,
        "SensorTask",
        kTaskStackWords,
        this,
        kTaskPriority,
        task_stack_,
        &task_control_);
    if (task_handle_ == nullptr) {
        if (adc_handle_ != nullptr) {
            adc_oneshot_del_unit(adc_handle_);
            adc_handle_ = nullptr;
        }
        runtime_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void SensorTask::task_entry(void* const context)
{
    static_cast<SensorTask*>(context)->task_main();
}

std::uint64_t SensorTask::now_ms_since_boot()
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

void SensorTask::task_main()
{
    while (true) {
        const pf_config::SensorSettingsLoadResult settings_result =
            pf_config::load_sensor_settings();
        const pf_config::SensorSettings settings =
            settings_result.error == ESP_OK ? settings_result.settings
                                             : pf_config::SensorSettings{};

        sample_light(settings);
        sample_environment(settings);

        vTaskDelay(kLoopIntervalTicks);
    }
}

void SensorTask::sample_light(const pf_config::SensorSettings& settings)
{
    const bool channel_enabled[kLightChannelCount] = {
        settings.light1_enabled,
        settings.light2_enabled,
    };
    const std::uint16_t channel_threshold[kLightChannelCount] = {
        settings.light1_threshold,
        settings.light2_threshold,
    };

    pf_sensors::LightChannelState channels[kLightChannelCount]{};
    for (std::size_t index = 0U; index < kLightChannelCount; ++index) {
        pf_sensors::LightChannelState& channel = channels[index];
        // The threshold is reported even for a channel that is off or
        // faulty: it is the configured value, and the WebUI shows it while
        // the user is still calibrating.
        channel.threshold = channel_threshold[index];
        if (!channel_enabled[index]) {
            channel.status = pf_sensors::LightSensorStatus::disabled;
        } else if (adc_handle_ == nullptr || !light_channel_ready_[index]) {
            channel.status = pf_sensors::LightSensorStatus::not_detected;
        } else {
            int raw = 0;
            const esp_err_t result = adc_oneshot_read(
                adc_handle_, kLightAdcChannels[index], &raw);
            if (result != ESP_OK) {
                channel.status = pf_sensors::LightSensorStatus::error;
            } else if (
                raw <= kSaturationLowRaw || raw >= kSaturationHighRaw) {
                channel.status = pf_sensors::LightSensorStatus::saturated;
            } else {
                // Only an `online` sample feeds the filter; raw_filtered
                // stays 0 otherwise so a bad reading never pollutes the
                // moving average with a spurious value.
                channel.status = pf_sensors::LightSensorStatus::online;
                channel.raw_filtered = light_filters_[index].push(
                    static_cast<std::uint16_t>(raw));
            }
        }
        if (channel.status != pf_sensors::LightSensorStatus::online) {
            // Drop the accumulated history too. Keeping it would average
            // the first sample after recovery together with readings from
            // before the channel was switched off, unplugged or faulty --
            // exactly the "reuse a historical value" the optional-sensor
            // contract forbids. The cost is a few seconds of ramp-up on
            // recovery, well inside the presence debounce window.
            light_filters_[index].reset();
        }
    }

    const pf_sensors::LightDecision decision =
        pf_sensors::combine_light_channels(channels);
    const pf_sensors::PresenceUpdateResult presence_result =
        pf_sensors::update_presence(
            presence_tracker_,
            decision.status,
            decision.raw_filtered,
            decision.threshold,
            now_ms_since_boot(),
            static_cast<std::uint64_t>(settings.away_duration_s) * 1000U,
            static_cast<std::uint64_t>(settings.return_duration_s) *
                1000U);

    if (runtime_ != nullptr) {
        runtime_->update_light_and_presence(
            channels, decision, presence_result.state);
    }
}

void SensorTask::sample_environment(
    const pf_config::SensorSettings& settings)
{
    if (!settings.environment_enabled) {
        environment_status_ = pf_sensors::SensorStatus::disabled;
        if (runtime_ != nullptr) {
            runtime_->update_environment(
                environment_cache_, environment_status_, environment_daily_);
        }
        return;
    }

    const std::uint64_t now_ms = now_ms_since_boot();
    const std::uint64_t now_epoch_s =
        static_cast<std::uint64_t>(std::time(nullptr));

    if (pf_sensors::environment_retry_due(environment_cache_, now_ms)) {
        pf_sensors::EnvironmentReading reading{};
        const bool ok = environment_sensor_.read(reading);
        if (ok) {
            pf_sensors::record_environment_success(
                environment_cache_, reading, now_epoch_s, now_ms);
            pf_sensors::record_daily_reading(
                environment_daily_, reading, now_epoch_s);
            environment_status_ = pf_sensors::SensorStatus::online;
        } else {
            pf_sensors::record_environment_failure(
                environment_cache_,
                pf_sensors::EnvironmentFailure::not_detected,
                now_ms);
            environment_status_ = environment_cache_.has_reading
                                       ? pf_sensors::SensorStatus::stale
                                       : pf_sensors::SensorStatus::not_detected;
        }
    } else {
        environment_status_ =
            environment_cache_.has_reading
                ? (pf_sensors::environment_stale(
                       environment_cache_, now_epoch_s)
                       ? pf_sensors::SensorStatus::stale
                       : pf_sensors::SensorStatus::online)
                : pf_sensors::SensorStatus::probing;
    }

    if (runtime_ != nullptr) {
        runtime_->update_environment(
            environment_cache_, environment_status_, environment_daily_);
    }
}

SensorTask& sensor_task()
{
    static SensorTask instance;
    return instance;
}

}  // namespace pf_sensors
