#include <cstdint>

#include <unity.h>

#include "pf_sensors/light_sensor.hpp"

using pf_sensors::MovingAverageFilter;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_moving_average_ramps_up_while_buffer_fills()
{
    MovingAverageFilter<4> filter;
    TEST_ASSERT_EQUAL_UINT16(100U, filter.push(100U));
    TEST_ASSERT_EQUAL_UINT16(150U, filter.push(200U));
    TEST_ASSERT_EQUAL_UINT16(200U, filter.push(300U));
    TEST_ASSERT_EQUAL_UINT(3U, filter.sample_count());
}

void test_moving_average_drops_oldest_sample_once_full()
{
    MovingAverageFilter<3> filter;
    filter.push(100U);
    filter.push(100U);
    filter.push(100U);
    // Buffer full at [100,100,100]; pushing 400 evicts the oldest 100.
    TEST_ASSERT_EQUAL_UINT16(200U, filter.push(400U));
    TEST_ASSERT_EQUAL_UINT16(3U, filter.sample_count());
}

void test_reset_clears_accumulated_samples()
{
    MovingAverageFilter<3> filter;
    filter.push(1000U);
    filter.push(2000U);
    filter.reset();
    TEST_ASSERT_EQUAL_UINT(0U, filter.sample_count());
    TEST_ASSERT_EQUAL_UINT16(50U, filter.push(50U));
}

using pf_sensors::combine_light_channels;
using pf_sensors::kLightChannelCount;
using pf_sensors::LightChannelState;
using pf_sensors::LightDecision;
using pf_sensors::LightSensorStatus;

// The brightest channel decides, regardless of which slot it sits in: it
// is the one keeping the device awake, and reporting it answers "why has
// this not slept yet".
void test_the_channel_furthest_above_its_threshold_decides()
{
    const LightChannelState brighter_first[kLightChannelCount] = {
        {LightSensorStatus::online, 3000U, 2000U},
        {LightSensorStatus::online, 500U, 2500U},
    };
    LightDecision decision = combine_light_channels(brighter_first);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::online),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(0U, decision.channel_index);
    TEST_ASSERT_EQUAL_UINT16(3000U, decision.raw_filtered);
    TEST_ASSERT_EQUAL_UINT16(2000U, decision.threshold);

    const LightChannelState brighter_second[kLightChannelCount] = {
        {LightSensorStatus::online, 100U, 2000U},
        {LightSensorStatus::online, 4000U, 2500U},
    };
    decision = combine_light_channels(brighter_second);
    TEST_ASSERT_EQUAL_UINT(1U, decision.channel_index);
    TEST_ASSERT_EQUAL_UINT16(4000U, decision.raw_filtered);
}

// One lit channel keeps the device awake even while the other is well
// below its own threshold: darkness needs both sensors to agree. This is
// the case that made the shipped behaviour wrong in the room -- a sensor
// mounted in a shadier spot was dragging the panel to sleep with the
// lights on.
void test_one_lit_channel_keeps_the_result_light()
{
    const LightChannelState channels[kLightChannelCount] = {
        // Bright: 1042 above its threshold.
        {LightSensorStatus::online, 2442U, 1400U},
        // Dark: 378 below its own.
        {LightSensorStatus::online, 1022U, 1400U},
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_UINT(0U, decision.channel_index);
    TEST_ASSERT_TRUE(decision.raw_filtered >= decision.threshold);
}

// Darkness requires every channel below its own threshold.
void test_all_channels_dark_reads_as_dark()
{
    const LightChannelState channels[kLightChannelCount] = {
        {LightSensorStatus::online, 23U, 500U},
        {LightSensorStatus::online, 180U, 500U},
    };
    const LightDecision decision = combine_light_channels(channels);
    // The brightest of the two still sits below its threshold, so the
    // single comparison presence performs comes out dark.
    TEST_ASSERT_EQUAL_UINT(1U, decision.channel_index);
    TEST_ASSERT_TRUE(decision.raw_filtered < decision.threshold);
}

// Each channel is compared against its own threshold, never a shared one:
// a channel with a higher raw value can still be the dark one if its
// threshold is higher too.
void test_each_channel_is_compared_against_its_own_threshold()
{
    const LightChannelState channels[kLightChannelCount] = {
        // 1200 with a 1000 threshold: 200 above, still light.
        {LightSensorStatus::online, 1200U, 1000U},
        // 1800 with a 2000 threshold: 200 below, already dark.
        {LightSensorStatus::online, 1800U, 2000U},
    };
    const LightDecision decision = combine_light_channels(channels);
    // Higher raw, but it is the one below its own threshold, so the lit
    // channel 0 decides even though it reads lower.
    TEST_ASSERT_EQUAL_UINT(0U, decision.channel_index);
    TEST_ASSERT_TRUE(decision.raw_filtered >= decision.threshold);
}

// A channel that is off, absent or faulty is ignored while the other one
// still reports, so one dead photoresistor cannot disable presence.
void test_a_working_channel_survives_a_dead_one()
{
    const LightChannelState statuses[] = {
        {LightSensorStatus::disabled, 0U, 2000U},
        {LightSensorStatus::not_detected, 0U, 2000U},
        {LightSensorStatus::error, 0U, 2000U},
    };
    for (const LightChannelState& dead : statuses) {
        const LightChannelState channels[kLightChannelCount] = {
            dead,
            {LightSensorStatus::online, 900U, 2000U},
        };
        const LightDecision decision = combine_light_channels(channels);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(LightSensorStatus::online),
            static_cast<int>(decision.status));
        TEST_ASSERT_EQUAL_UINT(1U, decision.channel_index);
        TEST_ASSERT_EQUAL_UINT16(900U, decision.raw_filtered);
    }
}

// A channel pinned against a rail still carries a usable reading
// (ADR-0020): it decides against a disabled peer just like `online` would,
// and the winning status is reported as the clipped value rather than
// forced to `online`.
void test_a_clipped_channel_decides_like_an_online_one()
{
    const LightChannelState channels[kLightChannelCount] = {
        {LightSensorStatus::low_clipped, 5U, 1400U},
        {LightSensorStatus::disabled, 0U, 1400U},
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::low_clipped),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(0U, decision.channel_index);
    TEST_ASSERT_EQUAL_UINT16(5U, decision.raw_filtered);
    TEST_ASSERT_TRUE(decision.raw_filtered < decision.threshold);
}

// Regression, measured on hardware 2026-08-27 (see HARDWARE.md): both
// channels clipping dark at once used to report `saturated` and drop out of
// the decision entirely, collapsing presence to `unknown` mid-away-countdown.
// Now both stay decision-capable, the brighter (less clipped) one still
// wins the margin comparison, and the result reads unambiguously dark.
void test_both_channels_clipped_dark_still_decides()
{
    const LightChannelState channels[kLightChannelCount] = {
        {LightSensorStatus::low_clipped, 8U, 1400U},
        {LightSensorStatus::low_clipped, 3U, 1400U},
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::low_clipped),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(0U, decision.channel_index);
    TEST_ASSERT_TRUE(decision.raw_filtered < decision.threshold);
}

// Regression, caught by codex-cowork round 2 (2026-08-29): the reducer used
// to pick the winner purely by signed margin (raw - threshold), which is
// not a reliable present/away indicator for a clipped channel once
// threshold sits at or beyond a rail (SensorSettings/WebUI allow the full
// 0-4095 range). A high_clipped(4085, threshold=4095) channel is
// unambiguously present but had margin -10; a genuinely away
// online(0, threshold=1) channel had the less-negative margin -1 and used
// to win, flipping the combined decision to away even though a channel was
// definitely lit. A channel that reads present must always beat one that
// reads away, regardless of margin.
void test_a_present_channel_always_beats_an_away_one_regardless_of_margin()
{
    const LightChannelState channels[kLightChannelCount] = {
        // high_clipped: definitely present, but the most negative margin.
        {LightSensorStatus::high_clipped, 4085U, 4095U},
        // online, genuinely away, but a less-negative margin than above.
        {LightSensorStatus::online, 0U, 1U},
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::high_clipped),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(0U, decision.channel_index);
}

// Mirror case with low_clipped: its margin can be *higher* than a
// genuinely present online channel's when its threshold happens to sit
// near 0 (still a legal SensorSettings value) -- raw=10 against
// threshold=0 gives margin +10, versus an online channel sitting right at
// its own threshold (margin 0). The old margin-only reducer would have
// picked the low_clipped (away) channel and reported the combined result
// as away even though the online channel is genuinely lit.
void test_an_online_present_channel_beats_a_low_clipped_away_one()
{
    const LightChannelState channels[kLightChannelCount] = {
        // low_clipped: definitely away regardless of margin.
        {LightSensorStatus::low_clipped, 10U, 0U},
        // online, genuinely present (raw >= threshold), smaller margin.
        {LightSensorStatus::online, 100U, 100U},
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::online),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(1U, decision.channel_index);
}

// When both channels are on the present side, the tie-break still falls
// back to the largest margin -- the "most representative" one -- exactly
// as it did before the present/away split was introduced.
void test_two_present_channels_break_ties_by_margin()
{
    const LightChannelState channels[kLightChannelCount] = {
        {LightSensorStatus::online, 3000U, 2000U},  // present, margin 1000
        {LightSensorStatus::high_clipped, 4085U, 500U},  // present, margin 3585
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::high_clipped),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(1U, decision.channel_index);
}

// With nothing online the aggregate reports the most actionable fault --
// presence collapses to unknown either way, but the user needs to see
// "error" rather than "disabled" when a channel is actually broken.
void test_no_online_channel_reports_the_worst_status_and_no_reading()
{
    const LightChannelState channels[kLightChannelCount] = {
        {LightSensorStatus::disabled, 0U, 2000U},
        {LightSensorStatus::error, 0U, 2500U},
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::error),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(kLightChannelCount, decision.channel_index);
    TEST_ASSERT_EQUAL_UINT16(0U, decision.raw_filtered);
    TEST_ASSERT_EQUAL_UINT16(0U, decision.threshold);

    const LightChannelState both_off[kLightChannelCount] = {
        {LightSensorStatus::disabled, 0U, 2000U},
        {LightSensorStatus::disabled, 0U, 2000U},
    };
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::disabled),
        static_cast<int>(combine_light_channels(both_off).status));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_moving_average_ramps_up_while_buffer_fills);
    RUN_TEST(test_moving_average_drops_oldest_sample_once_full);
    RUN_TEST(test_reset_clears_accumulated_samples);
    RUN_TEST(test_the_channel_furthest_above_its_threshold_decides);
    RUN_TEST(test_one_lit_channel_keeps_the_result_light);
    RUN_TEST(test_all_channels_dark_reads_as_dark);
    RUN_TEST(test_each_channel_is_compared_against_its_own_threshold);
    RUN_TEST(test_a_working_channel_survives_a_dead_one);
    RUN_TEST(test_a_clipped_channel_decides_like_an_online_one);
    RUN_TEST(test_both_channels_clipped_dark_still_decides);
    RUN_TEST(test_a_present_channel_always_beats_an_away_one_regardless_of_margin);
    RUN_TEST(test_an_online_present_channel_beats_a_low_clipped_away_one);
    RUN_TEST(test_two_present_channels_break_ties_by_margin);
    RUN_TEST(test_no_online_channel_reports_the_worst_status_and_no_reading);
    return UNITY_END();
}
