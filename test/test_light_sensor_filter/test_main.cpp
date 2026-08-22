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

// "Either channel reads dark" is the whole point of the second sensor, so
// the darker channel has to win regardless of which slot it sits in.
void test_the_channel_furthest_below_its_threshold_decides()
{
    const LightChannelState darker_second[kLightChannelCount] = {
        {LightSensorStatus::online, 3000U, 2000U},
        {LightSensorStatus::online, 500U, 2500U},
    };
    LightDecision decision = combine_light_channels(darker_second);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LightSensorStatus::online),
        static_cast<int>(decision.status));
    TEST_ASSERT_EQUAL_UINT(1U, decision.channel_index);
    TEST_ASSERT_EQUAL_UINT16(500U, decision.raw_filtered);
    TEST_ASSERT_EQUAL_UINT16(2500U, decision.threshold);

    const LightChannelState darker_first[kLightChannelCount] = {
        {LightSensorStatus::online, 100U, 2000U},
        {LightSensorStatus::online, 4000U, 2500U},
    };
    decision = combine_light_channels(darker_first);
    TEST_ASSERT_EQUAL_UINT(0U, decision.channel_index);
    TEST_ASSERT_EQUAL_UINT16(100U, decision.raw_filtered);
}

// Each channel is compared against its own threshold, never a shared one:
// a channel with a raw value lower than the other can still be the bright
// one if its threshold is lower too.
void test_each_channel_is_compared_against_its_own_threshold()
{
    const LightChannelState channels[kLightChannelCount] = {
        // 1200 with a 1000 threshold: 200 above, still light.
        {LightSensorStatus::online, 1200U, 1000U},
        // 1800 with a 2000 threshold: 200 below, already dark.
        {LightSensorStatus::online, 1800U, 2000U},
    };
    const LightDecision decision = combine_light_channels(channels);
    TEST_ASSERT_EQUAL_UINT(1U, decision.channel_index);
    TEST_ASSERT_TRUE(decision.raw_filtered < decision.threshold);
}

// A channel that is off, absent or faulty is ignored while the other one
// still reports, so one dead photoresistor cannot disable presence.
void test_a_working_channel_survives_a_dead_one()
{
    const LightChannelState statuses[] = {
        {LightSensorStatus::disabled, 0U, 2000U},
        {LightSensorStatus::not_detected, 0U, 2000U},
        {LightSensorStatus::saturated, 0U, 2000U},
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
    RUN_TEST(test_the_channel_furthest_below_its_threshold_decides);
    RUN_TEST(test_each_channel_is_compared_against_its_own_threshold);
    RUN_TEST(test_a_working_channel_survives_a_dead_one);
    RUN_TEST(test_no_online_channel_reports_the_worst_status_and_no_reading);
    return UNITY_END();
}
