#include <cstring>

#include <unity.h>

#include "pf_runtime/runtime_snapshot.hpp"
#include "pf_web/dashboard_serializer.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

pf_runtime::RuntimeSnapshot snapshot()
{
    return {
        .sequence = 7,
        .flash = pf_runtime::ServiceState::ready,
        .psram = pf_runtime::ServiceState::ready,
        .config = pf_runtime::ServiceState::ready,
        .webfs = pf_runtime::ServiceState::ready,
        .imagefs = pf_runtime::ServiceState::ready,
        .wifi = pf_runtime::WifiState::connected,
        .internet = pf_runtime::InternetState::reachable,
        .display = pf_runtime::DisplayState::deep_sleep,
        .active_display_request_id = 0,
        .queued_display_count = 1,
        .last_display_request_id = 6,
        .last_display_outcome =
            pf_runtime::DisplayOutcome::refreshed_and_slept,
        .last_display_stage = 4,
        .flash_bytes = 16U * 1024U * 1024U,
        .psram_bytes = 8U * 1024U * 1024U,
        .webfs_total_bytes = 0x100000U,
        .webfs_used_bytes = 4096U,
        .imagefs_total_bytes = 0x130000U,
        .imagefs_used_bytes = 8192U,
        .carousel_refresh_minutes = 30,
    };
}

void test_status_contains_runtime_and_null_future_modules()
{
    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        snapshot(),
        true,
        123456,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"sequence\":7"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"wifi\":\"connected\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"flash_bytes\":16777216"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"webfs_used_bytes\":4096"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"current_image\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"last_outcome\":\"refreshed_and_slept\""));
}

void test_device_is_safe_and_uses_snapshot_capacity()
{
    const pf_web::DeviceInfo device{
        .product = "PaperFrame",
        .model = "ESP32-S3-N16R8",
        .firmware = "test-version",
    };
    char output[768]{};
    const pf_web::SerializeResult result = pf_web::serialize_device(
        device,
        snapshot(),
        true,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"api_version\":\"v1\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"flash_bytes\":16777216"));
    TEST_ASSERT_NULL(std::strstr(output, "ssid"));
    TEST_ASSERT_NULL(std::strstr(output, "password"));
    TEST_ASSERT_NULL(std::strstr(output, "MAC"));
}

void test_masked_config_never_returns_secret_values()
{
    const pf_web::MaskedConfig config{
        .wifi_configured = true,
        .wifi_password_configured = true,
        .management_password_configured = true,
        .refresh_minutes = 30,
        .timezone = "Asia/Taipei",
        .weather_configured = true,
        .weather_api_key_set = true,
        .weather_latitude_e6 = 25033000,
        .weather_longitude_e6 = 121565000,
        .weather_interval_minutes = 10,
        .weather_location = "Taipei",
        .weather_units = "metric",
        .weather_language = "zh_tw",
        .weather_ntp_server = "pool.ntp.org",
    };
    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_masked_config(
        config,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"ssid_set\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"password_set\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"timezone\":\"Asia/Taipei\""));
    TEST_ASSERT_NULL(std::strstr(output, "secret"));
    TEST_ASSERT_NULL(std::strstr(output, "credential"));
}

void test_unknown_snapshot_has_null_capacities()
{
    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        pf_runtime::RuntimeSnapshot{},
        false,
        999,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"flash\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"flash_bytes\":null"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"refresh_minutes\":null"));
}

void test_unavailable_config_uses_null_for_unknown_refresh()
{
    const pf_web::MaskedConfig config{
        .wifi_configured = false,
        .wifi_password_configured = false,
        .management_password_configured = false,
        .refresh_minutes = 0,
        .timezone = "unknown",
        .weather_configured = false,
        .weather_api_key_set = false,
        .weather_latitude_e6 = 0,
        .weather_longitude_e6 = 0,
        .weather_interval_minutes = 0,
        .weather_location = "unknown",
        .weather_units = "unknown",
        .weather_language = "unknown",
        .weather_ntp_server = "unknown",
    };
    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_masked_config(
        config,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"refresh_minutes\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"password_set\":false"));
}

void test_masked_config_bounds_unterminated_text()
{
    char timezone[pf_config::kTimezoneCapacity];
    std::memset(timezone, 'x', sizeof(timezone));
    const pf_web::MaskedConfig config{
        .wifi_configured = false,
        .wifi_password_configured = false,
        .management_password_configured = false,
        .refresh_minutes = 0,
        .timezone = timezone,
        .weather_configured = false,
        .weather_api_key_set = false,
        .weather_latitude_e6 = 0,
        .weather_longitude_e6 = 0,
        .weather_interval_minutes = 0,
        .weather_location = "Taipei",
        .weather_units = "metric",
        .weather_language = "zh_tw",
        .weather_ntp_server = "pool.ntp.org",
    };
    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_masked_config(
        config,
        output,
        sizeof(output));
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"timezone\":\"unknown\""));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_contains_runtime_and_null_future_modules);
    RUN_TEST(test_device_is_safe_and_uses_snapshot_capacity);
    RUN_TEST(test_masked_config_never_returns_secret_values);
    RUN_TEST(test_unknown_snapshot_has_null_capacities);
    RUN_TEST(test_unavailable_config_uses_null_for_unknown_refresh);
    RUN_TEST(test_masked_config_bounds_unterminated_text);
    return UNITY_END();
}
