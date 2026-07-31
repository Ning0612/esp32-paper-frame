#include <cstdint>

#include <unity.h>

#include "pf_sensors/environment_sensor.hpp"

using pf_sensors::EnvironmentCache;
using pf_sensors::EnvironmentFailure;
using pf_sensors::EnvironmentReading;
using pf_sensors::NullEnvironmentSensor;
using pf_sensors::SensorStatus;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_null_environment_sensor_reports_not_detected()
{
    NullEnvironmentSensor sensor;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorStatus::not_detected),
        static_cast<int>(sensor.probe()));
    EnvironmentReading reading{};
    TEST_ASSERT_FALSE(sensor.read(reading));
}

void test_range_validation_matches_dht22_datasheet()
{
    TEST_ASSERT_TRUE(pf_sensors::environment_reading_in_range(
        {24.4F, 62.5F}));
    TEST_ASSERT_TRUE(
        pf_sensors::environment_reading_in_range({-40.0F, 0.0F}));
    TEST_ASSERT_TRUE(
        pf_sensors::environment_reading_in_range({80.0F, 100.0F}));
    TEST_ASSERT_FALSE(
        pf_sensors::environment_reading_in_range({-40.1F, 50.0F}));
    TEST_ASSERT_FALSE(
        pf_sensors::environment_reading_in_range({80.1F, 50.0F}));
    TEST_ASSERT_FALSE(
        pf_sensors::environment_reading_in_range({20.0F, -0.1F}));
    TEST_ASSERT_FALSE(
        pf_sensors::environment_reading_in_range({20.0F, 100.1F}));
}

void test_cache_preserves_last_success_and_backs_off_failures()
{
    EnvironmentCache cache{};
    pf_sensors::record_environment_success(
        cache, {24.4F, 62.5F}, 1000U, 5000U);
    TEST_ASSERT_TRUE(cache.has_reading);
    TEST_ASSERT_EQUAL_UINT64(
        5000U + pf_sensors::kEnvironmentUpdateIntervalMs,
        cache.next_attempt_ms);
    TEST_ASSERT_FALSE(pf_sensors::environment_retry_due(cache, 5000U));
    TEST_ASSERT_TRUE(
        pf_sensors::environment_retry_due(cache, cache.next_attempt_ms));
    TEST_ASSERT_FALSE(pf_sensors::environment_stale(cache, 1200U, 300U));
    TEST_ASSERT_TRUE(pf_sensors::environment_stale(cache, 1301U, 300U));

    pf_sensors::record_environment_failure(
        cache, EnvironmentFailure::read_error, 10000U);
    TEST_ASSERT_EQUAL_UINT32(1U, cache.consecutive_failures);
    TEST_ASSERT_EQUAL_UINT64(
        10000U + pf_sensors::kEnvironmentInitialRetryMs,
        cache.next_attempt_ms);
    TEST_ASSERT_TRUE(cache.has_reading);
    TEST_ASSERT_EQUAL_FLOAT(24.4F, cache.reading.temperature_c);

    pf_sensors::record_environment_failure(
        cache, EnvironmentFailure::out_of_range, cache.next_attempt_ms);
    TEST_ASSERT_EQUAL_UINT32(2U, cache.consecutive_failures);
    TEST_ASSERT_EQUAL_UINT64(
        10000U + pf_sensors::kEnvironmentInitialRetryMs +
            (2U * pf_sensors::kEnvironmentInitialRetryMs),
        cache.next_attempt_ms);
}

void test_classify_environment_read_maps_driver_outcomes()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorStatus::not_detected),
        static_cast<int>(pf_sensors::classify_environment_read(
            false, {})));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorStatus::online),
        static_cast<int>(pf_sensors::classify_environment_read(
            true, {24.4F, 62.5F})));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorStatus::error),
        static_cast<int>(pf_sensors::classify_environment_read(
            true, {200.0F, 62.5F})));
}

void test_unavailable_cache_is_always_stale()
{
    const EnvironmentCache cache{};
    TEST_ASSERT_TRUE(pf_sensors::environment_stale(cache, 999999U));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_null_environment_sensor_reports_not_detected);
    RUN_TEST(test_range_validation_matches_dht22_datasheet);
    RUN_TEST(test_cache_preserves_last_success_and_backs_off_failures);
    RUN_TEST(test_classify_environment_read_maps_driver_outcomes);
    RUN_TEST(test_unavailable_cache_is_always_stale);
    return UNITY_END();
}
