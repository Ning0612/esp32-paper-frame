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

}  // namespace pf_sensors
