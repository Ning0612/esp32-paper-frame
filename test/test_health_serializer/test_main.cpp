#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pf_runtime/runtime_snapshot.hpp"
#include "pf_web/health_serializer.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

pf_runtime::RuntimeSnapshot ready_snapshot()
{
    return {
        .sequence = 12,
        .flash = pf_runtime::ServiceState::ready,
        .psram = pf_runtime::ServiceState::ready,
        .config = pf_runtime::ServiceState::ready,
        .webfs = pf_runtime::ServiceState::ready,
        .imagefs = pf_runtime::ServiceState::ready,
        .wifi = pf_runtime::WifiState::provisioning,
        .internet = pf_runtime::InternetState::unknown,
        .display = pf_runtime::DisplayState::unknown,
        .active_display_request_id = 0,
        .queued_display_count = 0,
        .last_display_request_id = 0,
        .last_display_outcome = pf_runtime::DisplayOutcome::none,
        .last_display_stage = 0,
    };
}

void test_ready_snapshot_serializes_without_sensitive_fields()
{
    char output[320]{};
    const pf_web::SerializeResult result =
        pf_web::serialize_health(
            ready_snapshot(),
            true,
            3456,
            output,
            sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_UINT(std::strlen(output), result.length);
    TEST_ASSERT_EQUAL_STRING(
        "{\"status\":\"ready\",\"sequence\":12,\"uptime_ms\":3456,"
        "\"services\":{\"flash\":\"ready\",\"psram\":\"ready\","
        "\"config\":\"ready\",\"webfs\":\"ready\",\"imagefs\":\"ready\"},"
        "\"network\":{\"wifi\":\"provisioning\","
        "\"internet\":\"unknown\"}}",
        output);
    TEST_ASSERT_NULL(std::strstr(output, "ssid"));
    TEST_ASSERT_NULL(std::strstr(output, "password"));
    TEST_ASSERT_NULL(std::strstr(output, "api_key"));
}

void test_degraded_service_sets_overall_status()
{
    pf_runtime::RuntimeSnapshot snapshot = ready_snapshot();
    snapshot.imagefs = pf_runtime::ServiceState::degraded;

    char output[320]{};
    const pf_web::SerializeResult result =
        pf_web::serialize_health(snapshot, true, 9, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"status\":\"degraded\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"imagefs\":\"degraded\""));
}

void test_unpublished_snapshot_sets_unknown_overall_status()
{
    const pf_runtime::RuntimeSnapshot snapshot{};
    char output[320]{};
    const pf_web::SerializeResult result =
        pf_web::serialize_health(
            snapshot,
            false,
            0,
            output,
            sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"status\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"flash\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"psram\":\"unknown\""));
}

void test_published_unknown_service_is_degraded()
{
    pf_runtime::RuntimeSnapshot snapshot = ready_snapshot();
    snapshot.config = pf_runtime::ServiceState::unknown;
    char output[320]{};
    const pf_web::SerializeResult result =
        pf_web::serialize_health(
            snapshot,
            true,
            1,
            output,
            sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"status\":\"degraded\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"config\":\"unknown\""));
}

void test_small_buffer_fails_without_partial_payload()
{
    char output[16]{};
    const pf_web::SerializeResult result =
        pf_web::serialize_health(
            ready_snapshot(),
            true,
            UINT64_MAX,
            output,
            sizeof(output));

    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_UINT(0, result.length);
    TEST_ASSERT_EQUAL_CHAR('\0', output[0]);
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_ready_snapshot_serializes_without_sensitive_fields);
    RUN_TEST(test_degraded_service_sets_overall_status);
    RUN_TEST(test_unpublished_snapshot_sets_unknown_overall_status);
    RUN_TEST(test_published_unknown_service_is_degraded);
    RUN_TEST(test_small_buffer_fails_without_partial_payload);
    return UNITY_END();
}
