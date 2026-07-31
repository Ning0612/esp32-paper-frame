#include <cstdint>
#include <string>

#include <unity.h>

#include "pf_weather/weather.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

const char kResponse[] =
    R"json({"cod":200,"dt":1710000000,"name":"Taipei","main":{"temp":27.4,"humidity":80},"weather":[{"id":500,"description":"light rain","icon":"10d"}]})json";

void test_openweather_response_parses_required_fields()
{
    const pf_weather::ParseResult result =
        pf_weather::parse_current_weather(kResponse, sizeof(kResponse) - 1U);

    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_FLOAT(27.4F, result.observation.temperature);
    TEST_ASSERT_EQUAL_INT16(80, result.observation.humidity_percent);
    TEST_ASSERT_EQUAL_INT32(500, result.observation.weather_id);
    TEST_ASSERT_EQUAL_UINT64(1710000000U, result.observation.observed_at_epoch_s);
    TEST_ASSERT_EQUAL_STRING("light rain", result.observation.description);
    TEST_ASSERT_EQUAL_STRING("10d", result.observation.icon);
    TEST_ASSERT_EQUAL_STRING("Taipei", result.observation.location);
}

void test_api_rejection_and_malformed_payload_fail_closed()
{
    const std::string rejected =
        R"json({"cod":"401","message":"Invalid API key"})json";
    const pf_weather::ParseResult rejected_result =
        pf_weather::parse_current_weather(rejected.data(), rejected.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::ParseError::api_rejected),
        static_cast<int>(rejected_result.error));

    const std::string missing =
        R"json({"cod":200,"main":{"humidity":80},"weather":[]})json";
    const pf_weather::ParseResult missing_result =
        pf_weather::parse_current_weather(missing.data(), missing.size());
    TEST_ASSERT_FALSE(missing_result.ok());
    TEST_ASSERT_NOT_EQUAL(
        static_cast<int>(pf_weather::ParseError::none),
        static_cast<int>(missing_result.error));
}

void test_parser_rejects_truncated_strings_and_integer_overflow()
{
    const std::string truncated =
        R"json({"cod":200,"dt":1710000000,"main":{"temp":27.4,"humidity":80},"weather":[{"id":500,"description":"broken})json";
    const pf_weather::ParseResult truncated_result =
        pf_weather::parse_current_weather(
            truncated.data(),
            truncated.size());
    TEST_ASSERT_FALSE(truncated_result.ok());

    const std::string overflow =
        R"json({"cod":200,"dt":9223372036854775808,"main":{"temp":27.4,"humidity":80},"weather":[{"id":500,"description":"rain","icon":"10d"}]})json";
    const pf_weather::ParseResult overflow_result =
        pf_weather::parse_current_weather(overflow.data(), overflow.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::ParseError::invalid_value),
        static_cast<int>(overflow_result.error));
}

void test_parser_does_not_take_values_from_nested_objects()
{
    const std::string nested =
        R"json({"cod":200,"dt":1710000000,"main":{"nested":{"temp":27.4},"humidity":80},"weather":[{"id":500,"description":"rain","icon":"10d"}]})json";
    const pf_weather::ParseResult result =
        pf_weather::parse_current_weather(nested.data(), nested.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::ParseError::invalid_value),
        static_cast<int>(result.error));
}

void test_parser_rejects_number_prefixes_with_trailing_garbage()
{
    const std::string bad_temperature =
        R"json({"cod":200,"dt":1710000000,"main":{"temp":27.4junk,"humidity":80},"weather":[{"id":500,"description":"rain","icon":"10d"}]})json";
    const pf_weather::ParseResult temperature_result =
        pf_weather::parse_current_weather(
            bad_temperature.data(),
            bad_temperature.size());
    TEST_ASSERT_FALSE(temperature_result.ok());

    const std::string bad_timestamp =
        R"json({"cod":200,"dt":1710000000x,"main":{"temp":27.4,"humidity":80},"weather":[{"id":500,"description":"rain","icon":"10d"}]})json";
    const pf_weather::ParseResult timestamp_result =
        pf_weather::parse_current_weather(
            bad_timestamp.data(),
            bad_timestamp.size());
    TEST_ASSERT_FALSE(timestamp_result.ok());
}

void test_cache_preserves_last_success_and_backs_off_failures()
{
    const pf_weather::ParseResult parsed =
        pf_weather::parse_current_weather(kResponse, sizeof(kResponse) - 1U);
    TEST_ASSERT_TRUE(parsed.ok());

    pf_weather::Cache cache{};
    pf_weather::record_success(cache, parsed.observation, 1000U, 5000U);
    TEST_ASSERT_TRUE(cache.has_observation);
    TEST_ASSERT_EQUAL_UINT64(
        5000U + pf_weather::kUpdateIntervalMs,
        cache.next_attempt_ms);
    TEST_ASSERT_FALSE(pf_weather::retry_due(cache, 5000U));
    TEST_ASSERT_TRUE(pf_weather::retry_due(cache, cache.next_attempt_ms));
    TEST_ASSERT_FALSE(pf_weather::stale(cache, 1200U, 3600U));
    TEST_ASSERT_TRUE(pf_weather::stale(cache, 4601U, 3600U));

    pf_weather::record_failure(
        cache,
        pf_weather::Failure::network,
        10000U);
    TEST_ASSERT_EQUAL_UINT32(1U, cache.consecutive_failures);
    TEST_ASSERT_EQUAL_UINT64(
        10000U + pf_weather::kInitialRetryMs,
        cache.next_attempt_ms);
    TEST_ASSERT_TRUE(cache.has_observation);
    TEST_ASSERT_EQUAL_FLOAT(27.4F, cache.observation.temperature);

    pf_weather::record_failure(
        cache,
        pf_weather::Failure::http_error,
        cache.next_attempt_ms);
    TEST_ASSERT_EQUAL_UINT32(2U, cache.consecutive_failures);
    TEST_ASSERT_EQUAL_UINT64(
        10000U + pf_weather::kInitialRetryMs +
            (2U * pf_weather::kInitialRetryMs),
        cache.next_attempt_ms);
}

void test_record_success_honors_custom_interval()
{
    const pf_weather::ParseResult parsed =
        pf_weather::parse_current_weather(kResponse, sizeof(kResponse) - 1U);
    TEST_ASSERT_TRUE(parsed.ok());

    pf_weather::Cache cache{};
    pf_weather::record_success(
        cache, parsed.observation, 1000U, 5000U, 60U * 60U * 1000U);
    TEST_ASSERT_EQUAL_UINT64(5000U + 60U * 60U * 1000U, cache.next_attempt_ms);
}

void test_classify_http_status_maps_known_and_unknown_codes()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::none),
        static_cast<int>(pf_weather::classify_http_status(200)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::api_key_invalid),
        static_cast<int>(pf_weather::classify_http_status(401)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::http_error),
        static_cast<int>(pf_weather::classify_http_status(500)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::http_error),
        static_cast<int>(pf_weather::classify_http_status(404)));
}

}  // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_openweather_response_parses_required_fields);
    RUN_TEST(test_api_rejection_and_malformed_payload_fail_closed);
    RUN_TEST(test_parser_rejects_truncated_strings_and_integer_overflow);
    RUN_TEST(test_parser_does_not_take_values_from_nested_objects);
    RUN_TEST(test_parser_rejects_number_prefixes_with_trailing_garbage);
    RUN_TEST(test_cache_preserves_last_success_and_backs_off_failures);
    RUN_TEST(test_record_success_honors_custom_interval);
    RUN_TEST(test_classify_http_status_maps_known_and_unknown_codes);
    return UNITY_END();
}
