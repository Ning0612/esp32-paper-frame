#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_sensors {

// Mirrors Guild.md 4.9's light sensor state model, amended by
// docs/adr/0020-light-clip-is-diagnostic-not-presence-gate.md: the original
// `saturated` value is split into `low_clipped`/`high_clipped` so the ADC
// hitting a rail is reported precisely and no longer excludes the channel
// from presence (see light_status_is_decision_capable() below).
enum class LightSensorStatus : std::uint8_t {
    disabled,
    online,
    not_detected,
    low_clipped,
    high_clipped,
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
        case LightSensorStatus::low_clipped:
            return "low_clipped";
        case LightSensorStatus::high_clipped:
            return "high_clipped";
        case LightSensorStatus::error:
            return "error";
    }
    return "disabled";
}

// Whether a channel in this status carries a usable reading: `online` and
// both clipped states all still contribute to presence and to
// combine_light_channels()'s channel selection. `threshold` is a
// user-editable 0-4095 field with nothing pinning it inside the 11-4084
// band ADR-0018 only recommends for the *achievable raw range* -- comparing
// a clipped raw against an arbitrary threshold is not reliable (ADR-0020),
// which is why neither presence nor the reducer does that: both decide a
// clipped channel's direction from its status directly instead
// (light_channel_reads_present() below). Only a channel with no reading at
// all -- switched off, never came up, or a failed read -- is excluded.
constexpr bool light_status_is_decision_capable(const LightSensorStatus status)
{
    return status == LightSensorStatus::online ||
           status == LightSensorStatus::low_clipped ||
           status == LightSensorStatus::high_clipped;
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
// threshold, and presence reads "every channel is dark" as darkness --
// equivalently, any single lit channel keeps the device awake.
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
    // Only meaningful while light_status_is_decision_capable(status) is
    // true (online, or clipped against a rail -- ADR-0020); 0 otherwise, so
    // a reading from before the channel failed is never mistaken for a
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
    // channel is decision-capable and the other three fields carry no
    // reading.
    std::size_t channel_index = kLightChannelCount;
};

// Which side of the threshold comparison a decision-capable channel reads
// as, decided from status rather than a raw/threshold comparison for the
// two clipped states (ADR-0020): threshold is a user-editable 0-4095 field
// with nothing pinning it inside the achievable-raw band, so comparing a
// clipped raw against it can point the wrong way (e.g. threshold=4095
// would read a high_clipped/brightest-possible raw=4085 as below
// threshold). `online` is the only status where the raw/threshold
// comparison is meaningful. Caller must only pass a decision-capable
// channel (light_status_is_decision_capable(channel.status) is true).
constexpr bool light_channel_reads_present(const LightChannelState& channel)
{
    if (channel.status == LightSensorStatus::high_clipped) {
        return true;
    }
    if (channel.status == LightSensorStatus::low_clipped) {
        return false;
    }
    return channel.raw_filtered >= channel.threshold;
}

// Ranks the statuses with no usable reading (light_status_is_decision_capable()
// is false) so an aggregate reports the most actionable one when no channel
// has a reading at all: a read error outranks a channel the ADC never came
// up for, which outranks one the user switched off. online/low_clipped/
// high_clipped never reach this function -- combine_light_channels() only
// calls it for a channel that failed the decision-capable check -- but the
// switch still enumerates them so a future status addition is not silently
// misranked as 0.
constexpr std::uint8_t light_status_rank(const LightSensorStatus status)
{
    switch (status) {
        case LightSensorStatus::error:
            return 3U;
        case LightSensorStatus::not_detected:
            return 2U;
        case LightSensorStatus::disabled:
            return 1U;
        case LightSensorStatus::online:
        case LightSensorStatus::low_clipped:
        case LightSensorStatus::high_clipped:
            return 0U;
    }
    return 0U;
}

// Reduces per-channel state to what presence debouncing consumes.
//
// A channel with no usable reading (light_status_is_decision_capable() is
// false) is ignored outright as long as some other channel has one: an
// absent or broken second photoresistor must not disable a working first
// one (AGENTS.md -- sensors are optional and have to degrade rather than
// fail the feature).
//
// Among decision-capable channels (online, or clipped against a rail --
// ADR-0020), a channel that reads present (light_channel_reads_present())
// always wins over one that reads away, regardless of margin: presence is
// "any lit channel wakes the device", so a single present channel must
// decide the outcome even if its raw/threshold margin happens to be more
// negative than an away channel's (a clipped channel's margin is not a
// reliable presence-side indicator by itself -- ADR-0020, second fix
// 2026-08-29 after codex-cowork round 2 caught the reducer computing the
// same threshold-position-dependent margin the direction fix already
// removed from update_presence()). Within the same side, the *largest*
// signed margin (raw - threshold) wins -- purely a "which channel is more
// representative" tie-break, not what decides present vs away.
//
// Because presence is a single threshold comparison, that one choice
// fixes both directions: darkness needs every channel below its own
// threshold, and any single lit channel is enough to read as light. Put
// the other way round -- both sensors must agree it is dark before the
// panel sleeps, and either one seeing light wakes it.
//
// Two sensors mounted in different places see genuinely different light
// (measured 2026-08-23: one channel sat at ~42% of the other in the same
// room), so "this one spot is dark" is not the same question as "the room
// is dark". Requiring agreement also makes the wrong kind of error the
// rare one: blanking the panel while someone is sitting there is far more
// annoying than failing to blank it after they leave.
//
// The reported channel is therefore the brightest one -- the sensor
// currently keeping the device awake, which is the useful thing to show
// when someone is working out why it has not slept.
inline LightDecision combine_light_channels(
    const LightChannelState (&channels)[kLightChannelCount])
{
    LightDecision decision{};
    std::int32_t best_margin = 0;
    bool best_reads_present = false;
    std::uint8_t worst_rank = 0U;
    for (std::size_t index = 0U; index < kLightChannelCount; ++index) {
        const LightChannelState& channel = channels[index];
        if (!light_status_is_decision_capable(channel.status)) {
            const std::uint8_t rank = light_status_rank(channel.status);
            if (rank > worst_rank) {
                worst_rank = rank;
                decision.status = channel.status;
            }
            continue;
        }
        const bool channel_reads_present = light_channel_reads_present(channel);
        const std::int32_t margin =
            static_cast<std::int32_t>(channel.raw_filtered) -
            static_cast<std::int32_t>(channel.threshold);
        const bool no_channel_yet = decision.channel_index == kLightChannelCount;
        const bool present_beats_away =
            channel_reads_present && !best_reads_present;
        const bool same_side_better_margin =
            channel_reads_present == best_reads_present && margin > best_margin;
        if (no_channel_yet || present_beats_away || same_side_better_margin) {
            best_margin = margin;
            best_reads_present = channel_reads_present;
            decision.channel_index = index;
            decision.raw_filtered = channel.raw_filtered;
            decision.threshold = channel.threshold;
        }
    }
    if (decision.channel_index != kLightChannelCount) {
        // Carries the winning channel's own status through rather than
        // forcing `online`: a deciding channel pinned against a rail still
        // has a usable reading (ADR-0020), and callers/API consumers want to
        // see that it is clipped rather than a plain "online".
        decision.status = channels[decision.channel_index].status;
    }
    return decision;
}

}  // namespace pf_sensors
