#include <cstring>
#include <cstdio>

#include "unity.h"
#include "pf_web/sensor_config_form.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

// The four required numeric fields, in the order data/web/ui.js submits
// them. Reused so each case only has to spell out what it is changing.
constexpr char kRequired[] =
    "light1_threshold=2000&light2_threshold=1500&"
    "away_duration_s=180&return_duration_s=30";

void test_form_accepts_all_fields_including_checkboxes()
{
    const char body[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&"
        "environment_enabled=on&light1_enabled=on&light2_enabled=on";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::ok,
        pf_web::parse_sensor_config_form(body, std::strlen(body), form));
    TEST_ASSERT_TRUE(form.environment_enabled);
    TEST_ASSERT_TRUE(form.light1_enabled);
    TEST_ASSERT_TRUE(form.light2_enabled);
    TEST_ASSERT_EQUAL_STRING("2000", form.light1_threshold);
    TEST_ASSERT_EQUAL_STRING("1500", form.light2_threshold);
    TEST_ASSERT_EQUAL_STRING("180", form.away_duration_s);
    TEST_ASSERT_EQUAL_STRING("30", form.return_duration_s);
}

// Each light channel is switched on independently: enabling only the
// second one must not implicitly enable the first.
void test_form_enables_each_light_channel_independently()
{
    const char body[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&light2_enabled=on";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::ok,
        pf_web::parse_sensor_config_form(body, std::strlen(body), form));
    TEST_ASSERT_FALSE(form.light1_enabled);
    TEST_ASSERT_TRUE(form.light2_enabled);
}

void test_form_treats_absent_checkboxes_as_disabled()
{
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::ok,
        pf_web::parse_sensor_config_form(
            kRequired, std::strlen(kRequired), form));
    TEST_ASSERT_FALSE(form.environment_enabled);
    TEST_ASSERT_FALSE(form.light1_enabled);
    TEST_ASSERT_FALSE(form.light2_enabled);
}

void test_form_rejects_missing_unknown_and_duplicate_fields()
{
    const char missing[] = "light1_threshold=2000";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::missing_field,
        pf_web::parse_sensor_config_form(
            missing, std::strlen(missing), form));

    // The second threshold is required too: a body written against the
    // pre-ADR-0018 schema must be rejected rather than silently leaving
    // channel 2 at whatever the field defaulted to.
    const char single_channel[] =
        "light1_threshold=2000&away_duration_s=180&return_duration_s=30";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::missing_field,
        pf_web::parse_sensor_config_form(
            single_channel, std::strlen(single_channel), form));

    const char unknown[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&bogus=1";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::unknown_field,
        pf_web::parse_sensor_config_form(
            unknown, std::strlen(unknown), form));

    // The retired single-channel names must not quietly land in a
    // channel; they are unknown fields now.
    const char legacy_name[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&light_enabled=on";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::unknown_field,
        pf_web::parse_sensor_config_form(
            legacy_name, std::strlen(legacy_name), form));

    const char duplicate[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&light2_threshold=1000";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::duplicate_field,
        pf_web::parse_sensor_config_form(
            duplicate, std::strlen(duplicate), form));

    const char duplicate_checkbox[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&"
        "light2_enabled=on&light2_enabled=on";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::duplicate_field,
        pf_web::parse_sensor_config_form(
            duplicate_checkbox, std::strlen(duplicate_checkbox), form));
}

void test_form_rejects_malformed_checkbox_values()
{
    // Regression: a checkbox field must submit exactly "on" (what
    // browsers actually send); any other present value must not be
    // silently treated as "checked".
    const char empty_value[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&environment_enabled=";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::invalid_value,
        pf_web::parse_sensor_config_form(
            empty_value, std::strlen(empty_value), form));

    const char zero_value[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&light1_enabled=0";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::invalid_value,
        pf_web::parse_sensor_config_form(
            zero_value, std::strlen(zero_value), form));

    const char garbage_value[] =
        "light1_threshold=2000&light2_threshold=1500&"
        "away_duration_s=180&return_duration_s=30&light2_enabled=garbage";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::invalid_value,
        pf_web::parse_sensor_config_form(
            garbage_value, std::strlen(garbage_value), form));
}

// The threshold fields hold 7 digits plus a terminator; anything longer
// has to be rejected rather than truncated into a plausible number.
void test_form_rejects_oversized_numeric_values()
{
    const char oversized[] =
        "light1_threshold=2000&light2_threshold=12345678&"
        "away_duration_s=180&return_duration_s=30";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::invalid_value,
        pf_web::parse_sensor_config_form(
            oversized, std::strlen(oversized), form));
}

void test_parse_sensor_u32_rejects_non_numeric_and_overflow()
{
    std::uint32_t value = 0U;
    TEST_ASSERT_TRUE(pf_web::parse_sensor_u32("180", value));
    TEST_ASSERT_EQUAL_UINT32(180U, value);
    TEST_ASSERT_FALSE(pf_web::parse_sensor_u32("", value));
    TEST_ASSERT_FALSE(pf_web::parse_sensor_u32("18a", value));
    TEST_ASSERT_FALSE(pf_web::parse_sensor_u32("99999999999", value));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_form_accepts_all_fields_including_checkboxes);
    RUN_TEST(test_form_enables_each_light_channel_independently);
    RUN_TEST(test_form_treats_absent_checkboxes_as_disabled);
    RUN_TEST(test_form_rejects_missing_unknown_and_duplicate_fields);
    RUN_TEST(test_form_rejects_malformed_checkbox_values);
    RUN_TEST(test_form_rejects_oversized_numeric_values);
    RUN_TEST(test_parse_sensor_u32_rejects_non_numeric_and_overflow);
    return UNITY_END();
}
