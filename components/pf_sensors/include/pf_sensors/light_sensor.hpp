#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_sensors {

// Mirrors Guild.md 4.9's light sensor state model.
enum class LightSensorStatus : std::uint8_t {
    disabled,
    online,
    not_detected,
    saturated,
    error,
};

constexpr const char* to_string(const LightSensorStatus status)
{
    switch (status) {
        case LightSensorStatus::disabled:
            return "disabled";
        case LightSensorStatus::online:
            return "online";
        case LightSensorStatus::not_detected:
            return "not_detected";
        case LightSensorStatus::saturated:
            return "saturated";
        case LightSensorStatus::error:
            return "error";
    }
    return "disabled";
}

// Fixed-size moving average over raw ADC samples (see
// docs/adr/0006-sensor-drivers-and-presence.md for why moving average was
// picked over a median filter). Capacity is a compile-time constant so
// the whole filter lives inline with no dynamic allocation.
template <std::size_t Capacity>
class MovingAverageFilter {
    static_assert(Capacity > 0U, "MovingAverageFilter capacity must be > 0");

public:
    std::uint16_t push(const std::uint16_t sample)
    {
        samples_[next_index_] = sample;
        next_index_ = (next_index_ + 1U) % Capacity;
        if (filled_count_ < Capacity) {
            ++filled_count_;
        }
        std::uint32_t sum = 0U;
        for (std::size_t i = 0U; i < filled_count_; ++i) {
            sum += samples_[i];
        }
        return static_cast<std::uint16_t>(sum / filled_count_);
    }

    void reset()
    {
        next_index_ = 0U;
        filled_count_ = 0U;
    }

    std::size_t sample_count() const
    {
        return filled_count_;
    }

private:
    std::uint16_t samples_[Capacity]{};
    std::size_t next_index_ = 0U;
    std::size_t filled_count_ = 0U;
};


// Two photoresistor channels, each its own voltage-divider branch with
// nothing shared but 3V3 and ground (ADR-0018): channel 0 on GPIO5/ADC1_CH4,
// channel 1 on GPIO7/ADC1_CH6. Each has its own enable flag and its own
// threshold, and presence reads "either channel is dark" as darkness.
inline constexpr std::size_t kLightChannelCount = 2U;

// The GPIO each channel is wired to, reported through GET /api/v1/sensors so
// the WebUI can name the pin it is showing readings for. This must stay in
// step with kLightAdcChannels in sensor_task_esp_idf.cpp: the ADC channel and
// the GPIO are two views of the same pin and the mapping is fixed by silicon
// (GPIO5 = ADC1_CH4, GPIO7 = ADC1_CH6). Moving a channel to another pin means
// changing both arrays.
inline constexpr std::uint8_t kLightChannelGpios[kLightChannelCount] = {
    5U,
    7U,
};

struct LightChannelState {
    LightSensorStatus status = LightSensorStatus::disabled;
    // Only meaningful while `status` is `online`; 0 otherwise, so a
    // reading from before the channel failed is never mistaken for a
    // current one.
    std::uint16_t raw_filtered = 0U;
    // The configured threshold. Populated for a disabled or failed channel
    // too -- the WebUI shows it while calibrating, when the channel is by
    // definition not yet reporting usable samples. Only meaningful once
    // SensorTask has published at all; see RuntimeSnapshot::light_published,
    // which is what stops this 0 from being served as a real threshold.
    std::uint16_t threshold = 0U;
};

// The single (status, raw, threshold) triple presence debouncing acts on,
// reduced from every channel.
struct LightDecision {
    LightSensorStatus status = LightSensorStatus::disabled;
    std::uint16_t raw_filtered = 0U;
    std::uint16_t threshold = 0U;
    // Which channel the triple came from, or kLightChannelCount when no
    // channel is online and the other three fields carry no reading.
    std::size_t channel_index = kLightChannelCount;
};

// Ranks the non-online statuses so an aggregate reports the most
// actionable one: a read error outranks a saturated (stuck or unwired)
// channel, which outranks one the ADC never came up for, which outranks
// a channel the user switched off.
constexpr std::uint8_t light_status_rank(const LightSensorStatus status)
{
    switch (status) {
        case LightSensorStatus::error:
            return 4U;
        case LightSensorStatus::saturated:
            return 3U;
        case LightSensorStatus::not_detected:
            return 2U;
        case LightSensorStatus::disabled:
            return 1U;
        case LightSensorStatus::online:
            return 0U;
    }
    return 0U;
}

// Reduces per-channel state to what presence debouncing consumes.
//
// A channel that is not `online` is ignored outright as long as some
// other channel is online: an absent or broken second photoresistor must
// not disable a working first one (AGENTS.md -- sensors are optional and
// have to degrade rather than fail the feature). Among online channels
// the smallest signed margin (raw - threshold) wins, which makes "any
// channel reads dark" fall out of one comparison, because the minimum
// margin is negative exactly when at least one channel sits below its own
// threshold. Reporting that channel's raw/threshold also tells the user
// which sensor is driving the decision.
inline LightDecision combine_light_channels(
    const LightChannelState (&channels)[kLightChannelCount])
{
    LightDecision decision{};
    std::int32_t best_margin = 0;
    std::uint8_t worst_rank = 0U;
    for (std::size_t index = 0U; index < kLightChannelCount; ++index) {
        const LightChannelState& channel = channels[index];
        if (channel.status != LightSensorStatus::online) {
            const std::uint8_t rank = light_status_rank(channel.status);
            if (rank > worst_rank) {
                worst_rank = rank;
                decision.status = channel.status;
            }
            continue;
        }
        const std::int32_t margin =
            static_cast<std::int32_t>(channel.raw_filtered) -
            static_cast<std::int32_t>(channel.threshold);
        if (decision.channel_index == kLightChannelCount ||
            margin < best_margin) {
            best_margin = margin;
            decision.channel_index = index;
            decision.raw_filtered = channel.raw_filtered;
            decision.threshold = channel.threshold;
        }
    }
    if (decision.channel_index != kLightChannelCount) {
        decision.status = LightSensorStatus::online;
    }
    return decision;
}

}  // namespace pf_sensors
