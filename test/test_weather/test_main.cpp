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

    // An explicit interval: the firmware itself passes UINT64_MAX (no
    // periodic timer since ADR-0014), but the scheduling arithmetic still has
    // to hold for a finite one, which is what this case pins down.
    constexpr std::uint64_t kIntervalMs = 10U * 60U * 1000U;
    pf_weather::Cache cache{};
    pf_weather::record_success(
        cache, parsed.observation, 1000U, 5000U, kIntervalMs);
    TEST_ASSERT_TRUE(cache.has_observation);
    TEST_ASSERT_EQUAL_UINT64(5000U + kIntervalMs, cache.next_attempt_ms);
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

// The path the firmware actually takes: WeatherWorker passes UINT64_MAX so no
// automatic follow-up is scheduled (ADR-0014 replaced the periodic timer with
// a fetch driven by the carousel's panel refresh). Without this case every
// assertion above covers only the finite-interval behaviour that ADR-0014
// retired, leaving the shipped scheduling untested.
void test_record_success_can_schedule_no_automatic_retry()
{
    const pf_weather::ParseResult parsed =
        pf_weather::parse_current_weather(kResponse, sizeof(kResponse) - 1U);
    TEST_ASSERT_TRUE(parsed.ok());

    pf_weather::Cache cache{};
    pf_weather::record_success(
        cache,
        parsed.observation,
        1000U,
        5000U,
        pf_weather::kNoAutomaticRetry);

    TEST_ASSERT_EQUAL_UINT64(
        pf_weather::kNoAutomaticRetry, cache.next_attempt_ms);
    // retry_due() must stay false for every reachable uptime, so the worker
    // waits on its semaphore until request_immediate_refresh() wakes it.
    TEST_ASSERT_FALSE(pf_weather::retry_due(cache, 5000U));
    TEST_ASSERT_FALSE(pf_weather::retry_due(cache, UINT64_MAX - 1U));
    // The observation itself is still cached and still ages normally.
    TEST_ASSERT_TRUE(cache.has_observation);
    TEST_ASSERT_EQUAL_UINT64(1000U, cache.last_success_epoch_s);
    TEST_ASSERT_FALSE(pf_weather::stale(cache, 1200U, 3600U));
    TEST_ASSERT_TRUE(pf_weather::stale(cache, 4601U, 3600U));
}

// The ESP-IDF worker's perform()-failure branch cannot be reached from the
// native suite, so its decision is factored into classify_perform_failure()
// and pinned down here instead.
void test_classify_perform_failure_separates_transport_from_http()
{
    // No status line at all: DNS/TCP/TLS never produced a response.
    const pf_weather::PerformFailure none =
        pf_weather::classify_perform_failure(0);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::network),
        static_cast<int>(none.failure));
    TEST_ASSERT_FALSE(none.reached_server);

    // 401 without WWW-Authenticate makes ESP-IDF abandon perform() before the
    // normal status handling runs; a rejected key must not read as a network
    // fault, and the server plainly answered.
    const pf_weather::PerformFailure unauthorized =
        pf_weather::classify_perform_failure(401);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::api_key_invalid),
        static_cast<int>(unauthorized.failure));
    TEST_ASSERT_TRUE(unauthorized.reached_server);

    const pf_weather::PerformFailure server_error =
        pf_weather::classify_perform_failure(500);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::http_error),
        static_cast<int>(server_error.failure));
    TEST_ASSERT_TRUE(server_error.reached_server);

    // A 2xx with a failed perform() means the body was cut short: an HTTP
    // problem, not a transport one. Reporting it as `network` would claim the
    // internet is down while the server is demonstrably answering.
    const pf_weather::PerformFailure truncated =
        pf_weather::classify_perform_failure(200);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::http_error),
        static_cast<int>(truncated.failure));
    TEST_ASSERT_TRUE(truncated.reached_server);

    // A 3xx can linger from a redirect ESP-IDF followed internally. It must
    // not be classified as an API-key or server error, but it still proves
    // the request reached a server.
    const pf_weather::PerformFailure redirect =
        pf_weather::classify_perform_failure(302);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::http_error),
        static_cast<int>(redirect.failure));
    TEST_ASSERT_TRUE(redirect.reached_server);

    // ESP-IDF's actual "nothing parsed" value is -1, not 0
    // (esp_http_client.c seeds it that way). Testing only 0 would leave the
    // real sentinel -- the one every connection-level failure produces --
    // unpinned.
    const pf_weather::PerformFailure sentinel =
        pf_weather::classify_perform_failure(-1);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_weather::Failure::network),
        static_cast<int>(sentinel.failure));
    TEST_ASSERT_FALSE(sentinel.reached_server);

    // 1xx and out-of-range codes must not fall through to `network`: a status
    // line of any kind still means the request reached a server.
    const int others[] = {100, 199, 399, 600, 999};
    for (const int code : others) {
        const pf_weather::PerformFailure other =
            pf_weather::classify_perform_failure(code);
        TEST_ASSERT_TRUE(other.reached_server);
        TEST_ASSERT_NOT_EQUAL(
            static_cast<int>(pf_weather::Failure::network),
            static_cast<int>(other.failure));
        TEST_ASSERT_NOT_EQUAL(
            static_cast<int>(pf_weather::Failure::none),
            static_cast<int>(other.failure));
    }
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
    RUN_TEST(test_record_success_can_schedule_no_automatic_retry);
    RUN_TEST(test_classify_perform_failure_separates_transport_from_http);
    RUN_TEST(test_classify_http_status_maps_known_and_unknown_codes);
    return UNITY_END();
}
