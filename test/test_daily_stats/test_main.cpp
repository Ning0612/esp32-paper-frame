#include <cstdint>

#include <unity.h>

#include "pf_sensors/daily_stats.hpp"

using pf_sensors::DailyStats;
using pf_sensors::EnvironmentReading;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

constexpr std::uint64_t kDaySeconds = 86400U;

void test_tracks_min_max_avg_within_one_day()
{
    DailyStats stats{};
    pf_sensors::record_daily_reading(stats, {20.0F, 50.0F}, 1000U);
    pf_sensors::record_daily_reading(stats, {24.0F, 60.0F}, 2000U);
    pf_sensors::record_daily_reading(stats, {18.0F, 55.0F}, 3000U);

    TEST_ASSERT_EQUAL_FLOAT(18.0F, stats.temperature_c.min_value);
    TEST_ASSERT_EQUAL_FLOAT(24.0F, stats.temperature_c.max_value);
    TEST_ASSERT_EQUAL_FLOAT(
        (20.0F + 24.0F + 18.0F) / 3.0F,
        pf_sensors::daily_stat_average(stats.temperature_c));
    TEST_ASSERT_EQUAL_FLOAT(50.0F, stats.humidity_percent.min_value);
    TEST_ASSERT_EQUAL_FLOAT(60.0F, stats.humidity_percent.max_value);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.temperature_c.sample_count);
}

void test_resets_when_crossing_a_utc_day_boundary()
{
    DailyStats stats{};
    pf_sensors::record_daily_reading(stats, {20.0F, 50.0F}, 1000U);
    pf_sensors::record_daily_reading(
        stats, {99.0F, 90.0F}, 1000U + kDaySeconds);

    TEST_ASSERT_EQUAL_UINT32(1U, stats.temperature_c.sample_count);
    TEST_ASSERT_EQUAL_FLOAT(99.0F, stats.temperature_c.min_value);
    TEST_ASSERT_EQUAL_FLOAT(99.0F, stats.temperature_c.max_value);
}

void test_average_of_empty_stat_is_zero()
{
    const pf_sensors::DailyStat empty{};
    TEST_ASSERT_EQUAL_FLOAT(0.0F, pf_sensors::daily_stat_average(empty));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_tracks_min_max_avg_within_one_day);
    RUN_TEST(test_resets_when_crossing_a_utc_day_boundary);
    RUN_TEST(test_average_of_empty_stat_is_zero);
    return UNITY_END();
}
