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

// A cache holding a reading taken moments ago, from a sensor that is
// answering normally.
EnvironmentCache fresh_cache(const std::uint64_t now_epoch_s)
{
    EnvironmentCache cache{};
    cache.reading.temperature_c = 26.8F;
    cache.reading.humidity_percent = 64.2F;
    cache.has_reading = true;
    cache.last_success_epoch_s = now_epoch_s;
    cache.consecutive_failures = 0U;
    return cache;
}

// Regression, measured on hardware 2026-08-23: unplugging a DHT22 that had
// just read successfully left the API reporting `online` with the
// pre-unplug 26.8C/64.2% on every tick that was not a retry tick, for the
// whole five-minute cache window. One failed read is already proof the
// sensor is not answering; from then on the cached value is history.
void test_a_failed_read_stops_the_cache_counting_as_current()
{
    const std::uint64_t now = 1700000000U;
    EnvironmentCache cache = fresh_cache(now);
    TEST_ASSERT_TRUE(pf_sensors::environment_reading_current(cache, now));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::SensorStatus::online),
        static_cast<int>(pf_sensors::environment_cached_status(cache, now)));

    pf_sensors::record_environment_failure(
        cache, pf_sensors::EnvironmentFailure::not_detected, 1000U);

    // Well inside the cache max age -- age alone would still say "fresh".
    TEST_ASSERT_FALSE(pf_sensors::environment_stale(cache, now + 1U));
    TEST_ASSERT_FALSE(
        pf_sensors::environment_reading_current(cache, now + 1U));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::SensorStatus::stale),
        static_cast<int>(
            pf_sensors::environment_cached_status(cache, now + 1U)));
}

// A recovered sensor must go back to online rather than being stuck stale.
void test_a_successful_read_restores_current()
{
    const std::uint64_t now = 1700000000U;
    EnvironmentCache cache = fresh_cache(now);
    pf_sensors::record_environment_failure(
        cache, pf_sensors::EnvironmentFailure::not_detected, 1000U);
    TEST_ASSERT_FALSE(pf_sensors::environment_reading_current(cache, now));

    pf_sensors::EnvironmentReading reading{};
    reading.temperature_c = 27.0F;
    reading.humidity_percent = 60.0F;
    pf_sensors::record_environment_success(cache, reading, now + 10U, 2000U);

    TEST_ASSERT_TRUE(
        pf_sensors::environment_reading_current(cache, now + 10U));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::SensorStatus::online),
        static_cast<int>(
            pf_sensors::environment_cached_status(cache, now + 10U)));
}

// Age still matters on its own: a sensor that answered long ago and has
// not been retried since is not current either.
void test_age_alone_still_makes_a_reading_not_current()
{
    const std::uint64_t now = 1700000000U;
    const EnvironmentCache cache = fresh_cache(now);
    const std::uint64_t expired =
        now + pf_sensors::kEnvironmentDefaultCacheMaxAgeSeconds + 1U;

    TEST_ASSERT_EQUAL_UINT32(0U, cache.consecutive_failures);
    TEST_ASSERT_FALSE(
        pf_sensors::environment_reading_current(cache, expired));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::SensorStatus::stale),
        static_cast<int>(
            pf_sensors::environment_cached_status(cache, expired)));
}

// Nothing ever read: probing, not stale -- there is no value to be stale.
void test_empty_cache_reports_probing()
{
    const EnvironmentCache cache{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::SensorStatus::probing),
        static_cast<int>(
            pf_sensors::environment_cached_status(cache, 1700000000U)));
}

// Every other test here references these constants symbolically, so a
// regression that silently reverted them (e.g. back to the old 60s poll /
// 300s staleness window) would still pass. Pin the actual values the
// 2026-09-03 indoor-staleness fix settled on.
void test_update_interval_and_stale_window_match_the_tuned_values()
{
    TEST_ASSERT_EQUAL_UINT64(
        120U * 1000U, pf_sensors::kEnvironmentUpdateIntervalMs);
    TEST_ASSERT_EQUAL_UINT64(
        3600U, pf_sensors::kEnvironmentDefaultCacheMaxAgeSeconds);
}

// EnvironmentCache::has_reading never resets to false once a read has ever
// succeeded (record_environment_success only ever sets it), so this
// decision must key off SensorStatus -- not has_reading alone -- to hide the
// indoor block once the sensor is disabled or was never detected, even with
// a sticky-true has_reading left over from an earlier success.
void test_display_decision_hides_on_disabled_or_never_read()
{
    const pf_sensors::EnvironmentDisplayDecision disabled_after_success =
        pf_sensors::environment_display_decision(
            true, SensorStatus::disabled);
    TEST_ASSERT_FALSE(disabled_after_success.available);
    TEST_ASSERT_FALSE(disabled_after_success.stale);

    const pf_sensors::EnvironmentDisplayDecision never_read =
        pf_sensors::environment_display_decision(
            false, SensorStatus::not_detected);
    TEST_ASSERT_FALSE(never_read.available);

    const pf_sensors::EnvironmentDisplayDecision probing =
        pf_sensors::environment_display_decision(
            false, SensorStatus::probing);
    TEST_ASSERT_FALSE(probing.available);
}

void test_display_decision_shows_fresh_without_a_stale_marker()
{
    const pf_sensors::EnvironmentDisplayDecision decision =
        pf_sensors::environment_display_decision(true, SensorStatus::online);
    TEST_ASSERT_TRUE(decision.available);
    TEST_ASSERT_FALSE(decision.stale);
}

void test_display_decision_shows_cached_reading_marked_stale()
{
    const pf_sensors::EnvironmentDisplayDecision decision =
        pf_sensors::environment_display_decision(true, SensorStatus::stale);
    TEST_ASSERT_TRUE(decision.available);
    TEST_ASSERT_TRUE(decision.stale);
}

// Every one of the six SensorStatus values, crossed with both has_reading
// states -- a codex-cowork review (2026-09-03, round 2) pointed out the
// three tests above only sample five status/has_reading combinations and
// skip `error` entirely, which would leave a widened allow-list or a
// dropped has_reading gate undetected. `online`/`stale` are the only two
// that should ever show, and only when has_reading is true; every other
// combination -- including ones the sensor task itself would never
// actually produce, like has_reading=false with status=online -- must
// still come out hidden, since this is a pure function with no right to
// assume its caller upholds that invariant for it.
void test_display_decision_only_shows_when_read_and_online_or_stale()
{
    const SensorStatus all_statuses[] = {
        SensorStatus::disabled,
        SensorStatus::probing,
        SensorStatus::online,
        SensorStatus::stale,
        SensorStatus::not_detected,
        SensorStatus::error,
    };
    for (const bool has_reading : {false, true}) {
        for (const SensorStatus status : all_statuses) {
            const pf_sensors::EnvironmentDisplayDecision decision =
                pf_sensors::environment_display_decision(
                    has_reading, status);
            const bool should_show = has_reading &&
                (status == SensorStatus::online ||
                 status == SensorStatus::stale);
            TEST_ASSERT_EQUAL_INT(
                should_show ? 1 : 0, decision.available ? 1 : 0);
            if (!should_show) {
                TEST_ASSERT_FALSE(decision.stale);
            } else {
                TEST_ASSERT_EQUAL_INT(
                    status == SensorStatus::stale ? 1 : 0,
                    decision.stale ? 1 : 0);
            }
        }
    }
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
    RUN_TEST(test_a_failed_read_stops_the_cache_counting_as_current);
    RUN_TEST(test_a_successful_read_restores_current);
    RUN_TEST(test_age_alone_still_makes_a_reading_not_current);
    RUN_TEST(test_empty_cache_reports_probing);
    RUN_TEST(test_update_interval_and_stale_window_match_the_tuned_values);
    RUN_TEST(test_display_decision_hides_on_disabled_or_never_read);
    RUN_TEST(test_display_decision_shows_fresh_without_a_stale_marker);
    RUN_TEST(test_display_decision_shows_cached_reading_marked_stale);
    RUN_TEST(test_display_decision_only_shows_when_read_and_online_or_stale);
    return UNITY_END();
}
