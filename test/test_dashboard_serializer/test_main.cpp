#include <cstring>

#include <unity.h>

#include "pf_runtime/diagnostics_event.hpp"
#include "pf_runtime/runtime_snapshot.hpp"
#include "pf_weather/weather.hpp"
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
        .imagefs_total_bytes = 0x130000U,
        .imagefs_used_bytes = 8192U,
        .carousel_refresh_minutes = 30,
    };
}

// Fills in both photoresistor channels and the reduction the serializers
// read, so a test never has to hand-compute the combined decision.
void set_light(
    pf_runtime::RuntimeSnapshot& data,
    const pf_sensors::LightChannelState& channel_one,
    const pf_sensors::LightChannelState& channel_two)
{
    data.light_channels[0] = channel_one;
    data.light_channels[1] = channel_two;
    data.light_decision =
        pf_sensors::combine_light_channels(data.light_channels);
    data.light_published = true;
}

void test_status_contains_runtime_fields_and_null_carousel_when_unset()
{
    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        snapshot(),
        true,
        123456,
        1700000000U,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"sequence\":7"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"wifi\":\"connected\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"flash_bytes\":16777216"));
    // The webfs partition is retired and its capacity must not
    // reappear in the payload
    // (docs/adr/0016-embed-webui-assets-in-firmware.md).
    TEST_ASSERT_NULL(std::strstr(output, "webfs"));
    // Null-safety regression: snapshot() leaves current_image_id/
    // next_carousel_due_ms at their default 0 ("never set yet" / welcome
    // frame), which must render as null, not a fabricated 0.
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"current_image\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"next_refresh_ms\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"last_outcome\":\"refreshed_and_slept\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"weather\":{\"state\":\"unavailable\""));
}

void test_status_reports_current_image_and_next_refresh_when_set()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.current_image_id = 7U;
    data.next_carousel_due_ms = 200000U;

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data, true, 123456U, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"current_image\":7"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"next_refresh_ms\":76544"));
}

void test_status_clamps_overdue_next_refresh_to_zero_not_negative()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.current_image_id = 3U;
    // due in the past relative to uptime_ms -- must clamp to 0 ("due now"),
    // never wrap into a huge unsigned value from an unsigned subtraction.
    data.next_carousel_due_ms = 100U;

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data, true, 999999U, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"next_refresh_ms\":0"));
}

void test_weather_reports_available_when_fresh()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.weather.has_observation = true;
    data.weather.observation.temperature = 27.4F;
    data.weather.observation.humidity_percent = 80;
    data.weather.observation.weather_id = 500;
    std::strncpy(
        data.weather.observation.description,
        "light rain",
        sizeof(data.weather.observation.description) - 1U);
    std::strncpy(
        data.weather.observation.icon,
        "10d",
        sizeof(data.weather.observation.icon) - 1U);
    data.weather.last_success_epoch_s = 1700000000U;
    data.weather.last_failure = pf_weather::Failure::none;
    std::strncpy(
        data.weather_units,
        "metric",
        sizeof(data.weather_units) - 1U);

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data,
        true,
        123456,
        1700000100U,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"weather\":{\"state\":\"available\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"temperature\":27.4"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"units\":\"metric\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"icon\":\"10d\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"description\":\"light rain\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"humidity_percent\":80"));
}

void test_weather_reports_stale_past_max_age()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.weather.has_observation = true;
    data.weather.observation.temperature = 10.0F;
    data.weather.last_success_epoch_s = 1000U;

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data,
        true,
        123456,
        1000U + pf_weather::kDefaultCacheMaxAgeSeconds + 1U,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"weather\":{\"state\":\"stale\""));
}

void test_weather_reports_last_failure_reason_when_unavailable()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.weather.has_observation = false;
    data.weather.last_failure = pf_weather::Failure::api_key_invalid;

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data,
        true,
        123456,
        1700000000U,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"weather\":{\"state\":\"unavailable\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"temperature\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"last_failure\":\"api_key_invalid\""));
}

void test_sensors_report_readings_when_online()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.environment.has_reading = true;
    data.environment.reading.temperature_c = 24.4F;
    data.environment.reading.humidity_percent = 62.5F;
    data.environment_status = pf_sensors::SensorStatus::online;
    set_light(
        data,
        {pf_sensors::LightSensorStatus::online, 1234U, 2000U},
        {pf_sensors::LightSensorStatus::disabled, 0U, 2000U});
    data.presence = pf_sensors::PresenceState::present;

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data, true, 123456, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"environment_status\":\"online\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"temperature_c\":24.4"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"humidity_percent\":62.5"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"light_status\":\"online\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"light_adc\":1234"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"presence\":\"present\""));
}

void test_sensors_report_null_readings_when_disabled_or_not_online()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.environment_status = pf_sensors::SensorStatus::disabled;
    // A saturated channel outranks a disabled one, so the combined status
    // reports the fault rather than hiding it behind "disabled".
    set_light(
        data,
        {pf_sensors::LightSensorStatus::saturated, 0U, 2000U},
        {pf_sensors::LightSensorStatus::disabled, 0U, 2000U});
    data.presence = pf_sensors::PresenceState::unknown;

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data, true, 123456, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"environment_status\":\"disabled\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"humidity_percent\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"light_status\":\"saturated\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"light_adc\":null"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"presence\":\"unknown\""));
}

void test_serialize_sensors_reports_readings_and_daily_stats()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.environment.has_reading = true;
    data.environment.reading.temperature_c = 24.4F;
    data.environment.reading.humidity_percent = 62.5F;
    data.environment.last_success_epoch_s = 1700000000U;
    data.environment_status = pf_sensors::SensorStatus::online;
    pf_sensors::record_daily_reading(
        data.environment_daily, data.environment.reading, 1700000000U);
    set_light(
        data,
        {pf_sensors::LightSensorStatus::online, 1234U, 2000U},
        {pf_sensors::LightSensorStatus::online, 3000U, 2500U});
    data.presence = pf_sensors::PresenceState::present;

    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_sensors(
        data, true, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"status\":\"online\",\"gpio\":6"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"temperature_c\":24.4"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"stale\":false"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"temperature_min_c\":24.4"));
    // Channel 2 is the brighter of the two (+500 vs -766 against its own
    // threshold), so it is the one keeping the device awake and the one
    // presence acted on.
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "\"light\":{\"status\":\"online\",\"raw\":3000,"
        "\"threshold\":2500,\"saturated\":false,"
        "\"deciding_channel\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"channel\":1,\"gpio\":5,\"status\":\"online\","
        "\"raw\":1234,\"threshold\":2000}"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"channel\":2,\"gpio\":7,\"status\":\"online\","
        "\"raw\":3000,\"threshold\":2500}"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"presence\":\"present\""));
}

// Darkness needs both sensors to agree, so one lit channel wins over a
// second one that has gone dark -- and the API names the lit channel, the
// one actually keeping the device awake.
void test_serialize_sensors_lets_one_lit_channel_win()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    set_light(
        data,
        {pf_sensors::LightSensorStatus::online, 3000U, 2000U},
        {pf_sensors::LightSensorStatus::online, 500U, 2500U});

    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_sensors(
        data, true, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "\"light\":{\"status\":\"online\",\"raw\":3000,"
        "\"threshold\":2000,\"saturated\":false,"
        "\"deciding_channel\":1"));
}

// A readable snapshot whose sensor task has not published yet (still
// starting, or it failed to start at all) must report the thresholds as
// null. 0 is a legal configured threshold, so emitting the zero-initialised
// placeholder would be a fabricated value.
void test_serialize_sensors_reports_null_threshold_before_first_sample()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    TEST_ASSERT_FALSE(data.light_published);

    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_sensors(
        data, true, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"channel\":1,\"gpio\":5,\"status\":\"disabled\","
        "\"raw\":null,\"threshold\":null}"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"channel\":2,\"gpio\":7,\"status\":\"disabled\","
        "\"raw\":null,\"threshold\":null}"));
    TEST_ASSERT_NULL(std::strstr(output, "\"threshold\":0"));
}

// An unwired or switched-off second channel must not take the working
// first one down with it: sensors are optional and degrade individually.
void test_serialize_sensors_ignores_a_missing_second_channel()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    set_light(
        data,
        {pf_sensors::LightSensorStatus::online, 900U, 2000U},
        {pf_sensors::LightSensorStatus::not_detected, 0U, 2000U});

    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_sensors(
        data, true, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "\"light\":{\"status\":\"online\",\"raw\":900,"
        "\"threshold\":2000,\"saturated\":false,"
        "\"deciding_channel\":1"));
    // The dead channel still reports its own state and its configured
    // threshold, which is what the user calibrates against.
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"channel\":2,\"gpio\":7,\"status\":\"not_detected\","
        "\"raw\":null,\"threshold\":2000}"));
}

void test_serialize_sensors_reports_null_when_never_read()
{
    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_sensors(
        pf_runtime::RuntimeSnapshot{}, false, 0U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"status\":\"disabled\",\"gpio\":6"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"temperature_min_c\":null"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"raw\":null"));
    // Regression: an unavailable snapshot must not leak threshold=0,
    // which would look like a genuinely configured value instead of
    // "we don't know". That holds for the combined view and for both
    // per-channel entries.
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "\"light\":{\"status\":\"disabled\",\"raw\":null,"
        "\"threshold\":null,\"saturated\":false,"
        "\"deciding_channel\":null"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"channel\":1,\"gpio\":5,\"status\":\"disabled\","
        "\"raw\":null,\"threshold\":null}"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"channel\":2,\"gpio\":7,\"status\":\"disabled\","
        "\"raw\":null,\"threshold\":null}"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"presence\":\"unknown\""));
}

// Regression, measured on hardware 2026-08-23: after the DHT22 was
// unplugged the API served `"status":"stale"` alongside `"stale":false` in
// the same object, because the flag was gated on the status being online --
// exactly when it cannot be stale. A reading that is being served while no
// longer current must say so.
void test_serialize_sensors_marks_a_failed_sensors_reading_stale()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.environment.has_reading = true;
    data.environment.reading.temperature_c = 26.8F;
    data.environment.reading.humidity_percent = 64.2F;
    data.environment.last_success_epoch_s = 1700000000U;
    // One failed read: the sensor has stopped answering, but the cache is
    // still well inside its max age.
    data.environment.consecutive_failures = 1U;
    data.environment_status = pf_sensors::SensorStatus::stale;

    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_sensors(
        data, true, 1700000001U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"status\":\"stale\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"stale\":true"));
    TEST_ASSERT_NULL(std::strstr(output, "\"stale\":false"));
    // The value itself is still served -- flagged, not fabricated away.
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"temperature_c\":26.8"));

    // The dashboard summary has no room for a qualifier, so there it is
    // dropped rather than rendered as a bare "26.8 °C" that reads as the
    // current temperature.
    char status_output[2048]{};
    const pf_web::SerializeResult status_result = pf_web::serialize_status(
        data, true, 123456, 1700000001U, status_output,
        sizeof(status_output));
    TEST_ASSERT_TRUE(status_result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(status_output, "\"environment_status\":\"stale\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(status_output, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(status_output, "\"humidity_percent\":null"));
}

void test_sensors_hide_stale_reading_after_environment_disabled()
{
    // Regression: disabling the sensor after it had a valid reading must
    // not leave the last cached temperature/humidity/daily stats visible
    // through the API -- only the status flips to "disabled", the cache
    // itself is intentionally left alone so a re-enable can resume.
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.environment.has_reading = true;
    data.environment.reading.temperature_c = 24.4F;
    data.environment.reading.humidity_percent = 62.5F;
    data.environment.last_success_epoch_s = 1700000000U;
    pf_sensors::record_daily_reading(
        data.environment_daily, data.environment.reading, 1700000000U);
    data.environment_status = pf_sensors::SensorStatus::disabled;

    char status_output[2048]{};
    const pf_web::SerializeResult status_result = pf_web::serialize_status(
        data, true, 123456, 1700000000U, status_output, sizeof(status_output));
    TEST_ASSERT_TRUE(status_result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(status_output, "\"environment_status\":\"disabled\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(status_output, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(status_output, "\"humidity_percent\":null"));

    char sensors_output[1024]{};
    const pf_web::SerializeResult sensors_result = pf_web::serialize_sensors(
        data, true, 1700000000U, sensors_output, sizeof(sensors_output));
    TEST_ASSERT_TRUE(sensors_result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(sensors_output, "\"status\":\"disabled\",\"gpio\":6"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(sensors_output, "\"temperature_c\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(sensors_output, "\"humidity_percent\":null"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(sensors_output, "\"temperature_min_c\":null"));
    TEST_ASSERT_NOT_NULL(std::strstr(sensors_output, "\"stale\":false"));
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
        .carousel_random = true,
        .timezone = "Asia/Taipei",
        .weather_configured = true,
        .weather_api_key_set = true,
        .weather_latitude_e6 = 25033000,
        .weather_longitude_e6 = 121565000,
        .weather_units = "metric",
        .weather_ntp_server = "pool.ntp.org",
        .environment_enabled = true,
        .light1_enabled = true,
        .light2_enabled = true,
        .light1_threshold = 2000U,
        .light2_threshold = 1500U,
        .away_duration_s = 180U,
        .return_duration_s = 30U,
    };
    char output[1024]{};
    const pf_web::SerializeResult result = pf_web::serialize_masked_config(
        config,
        output,
        sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"ssid_set\":true"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"environment_enabled\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"light1_enabled\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"light1_threshold\":2000"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"light2_enabled\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"light2_threshold\":1500"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"away_duration_s\":180"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"password_set\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"random\":true"));
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
        0,
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
        .weather_units = "unknown",
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
        .weather_units = "metric",
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

void test_status_exposes_reboot_reason_and_queue_lock_counters()
{
    // Regression: these RuntimeSnapshot fields exist purely to be
    // observable through the API; a coordinator that tracks them but never
    // serializes them defeats the entire point of Milestone 1.
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.reboot_reason = pf_runtime::RebootReason::panic_or_watchdog;
    data.diagnostics_latest_sequence_id = 42U;
    data.command_queue_rejected_count = 3U;
    data.terminal_result_exhausted_count = 1U;
    data.flash_display_lock_timeout_count = 2U;

    char output[2048]{};
    const pf_web::SerializeResult result = pf_web::serialize_status(
        data, true, 123456, 1700000000U, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"reboot_reason\":\"panic_or_watchdog\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"latest_sequence_id\":42"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"command_queue_rejected_count\":3"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"terminal_result_exhausted_count\":1"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"flash_display_lock_timeout_count\":2"));
}

void test_serialize_ota_status_reports_check_and_update_state()
{
    pf_runtime::RuntimeSnapshot data = snapshot();
    data.ota_check_state = pf_runtime::OtaCheckState::update_available;
    std::strncpy(
        data.ota_latest_version, "v0.9.0",
        sizeof(data.ota_latest_version) - 1U);
    data.ota_last_check_epoch_s = 1700000000U;
    data.ota_update_state = pf_runtime::OtaUpdateState::writing;
    data.ota_update_progress_percent = 42U;

    char output[512]{};
    const pf_web::SerializeResult result =
        pf_web::serialize_ota_status(data, true, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"check_state\":\"update_available\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"latest_version\":\"v0.9.0\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"update_state\":\"writing\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"progress_percent\":42"));
}

void test_serialize_ota_status_reports_unknown_when_never_checked()
{
    char output[512]{};
    const pf_web::SerializeResult result = pf_web::serialize_ota_status(
        pf_runtime::RuntimeSnapshot{}, false, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"check_state\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"latest_version\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"update_state\":\"idle\""));
}

void test_serialize_events_reports_empty_array_when_none()
{
    char output[256]{};
    const pf_web::SerializeResult result =
        pf_web::serialize_events(nullptr, 0U, 0U, false, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"events\":[]"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"highest_sequence_id\":0"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"more_available\":false"));
}

void test_serialize_events_reports_events_oldest_first()
{
    pf_runtime::DiagnosticEvent events[2]{};
    events[0].sequence_id = 3U;
    events[0].uptime_ms = 111U;
    events[0].category = pf_runtime::DiagnosticCategory::queue;
    events[0].severity = pf_runtime::DiagnosticSeverity::warning;
    std::strncpy(
        events[0].message, "command_queue_full",
        sizeof(events[0].message) - 1U);
    events[1].sequence_id = 4U;
    events[1].uptime_ms = 222U;
    events[1].category = pf_runtime::DiagnosticCategory::lock;
    events[1].severity = pf_runtime::DiagnosticSeverity::error;
    std::strncpy(
        events[1].message, "flash_display_lock_timeout",
        sizeof(events[1].message) - 1U);

    char output[512]{};
    const pf_web::SerializeResult result = pf_web::serialize_events(
        events, 2U, 4U, true, output, sizeof(output));

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"highest_sequence_id\":4"));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"more_available\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"sequence_id\":3,\"uptime_ms\":111,\"category\":\"queue\","
        "\"severity\":\"warning\",\"message\":\"command_queue_full\"}"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        output,
        "{\"sequence_id\":4,\"uptime_ms\":222,\"category\":\"lock\","
        "\"severity\":\"error\",\"message\":\"flash_display_lock_timeout\"}"));
    // Oldest-first ordering: sequence_id 3 must appear before 4.
    TEST_ASSERT_TRUE(
        std::strstr(output, "\"sequence_id\":3") <
        std::strstr(output, "\"sequence_id\":4"));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_contains_runtime_fields_and_null_carousel_when_unset);
    RUN_TEST(test_status_reports_current_image_and_next_refresh_when_set);
    RUN_TEST(test_status_clamps_overdue_next_refresh_to_zero_not_negative);
    RUN_TEST(test_weather_reports_available_when_fresh);
    RUN_TEST(test_weather_reports_stale_past_max_age);
    RUN_TEST(test_weather_reports_last_failure_reason_when_unavailable);
    RUN_TEST(test_sensors_report_readings_when_online);
    RUN_TEST(test_sensors_report_null_readings_when_disabled_or_not_online);
    RUN_TEST(test_serialize_sensors_reports_readings_and_daily_stats);
    RUN_TEST(test_serialize_sensors_lets_one_lit_channel_win);
    RUN_TEST(test_serialize_sensors_reports_null_threshold_before_first_sample);
    RUN_TEST(test_serialize_sensors_ignores_a_missing_second_channel);
    RUN_TEST(test_serialize_sensors_reports_null_when_never_read);
    RUN_TEST(test_serialize_sensors_marks_a_failed_sensors_reading_stale);
    RUN_TEST(test_sensors_hide_stale_reading_after_environment_disabled);
    RUN_TEST(test_device_is_safe_and_uses_snapshot_capacity);
    RUN_TEST(test_masked_config_never_returns_secret_values);
    RUN_TEST(test_unknown_snapshot_has_null_capacities);
    RUN_TEST(test_unavailable_config_uses_null_for_unknown_refresh);
    RUN_TEST(test_masked_config_bounds_unterminated_text);
    RUN_TEST(test_status_exposes_reboot_reason_and_queue_lock_counters);
    RUN_TEST(test_serialize_ota_status_reports_check_and_update_state);
    RUN_TEST(test_serialize_ota_status_reports_unknown_when_never_checked);
    RUN_TEST(test_serialize_events_reports_empty_array_when_none);
    RUN_TEST(test_serialize_events_reports_events_oldest_first);
    return UNITY_END();
}
