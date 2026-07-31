#include <cstring>
#include <cstdio>

#include "unity.h"
#include "pf_web/sensor_config_form.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_form_accepts_all_fields_including_checkboxes()
{
    const char body[] =
        "light_threshold=2000&away_duration_s=180&return_duration_s=30&"
        "environment_enabled=on&light_enabled=on";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::ok,
        pf_web::parse_sensor_config_form(body, std::strlen(body), form));
    TEST_ASSERT_TRUE(form.environment_enabled);
    TEST_ASSERT_TRUE(form.light_enabled);
    TEST_ASSERT_EQUAL_STRING("2000", form.light_threshold);
    TEST_ASSERT_EQUAL_STRING("180", form.away_duration_s);
    TEST_ASSERT_EQUAL_STRING("30", form.return_duration_s);
}

void test_form_treats_absent_checkboxes_as_disabled()
{
    const char body[] =
        "light_threshold=2000&away_duration_s=180&return_duration_s=30";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::ok,
        pf_web::parse_sensor_config_form(body, std::strlen(body), form));
    TEST_ASSERT_FALSE(form.environment_enabled);
    TEST_ASSERT_FALSE(form.light_enabled);
}

void test_form_rejects_missing_unknown_and_duplicate_fields()
{
    const char missing[] = "light_threshold=2000";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::missing_field,
        pf_web::parse_sensor_config_form(
            missing, std::strlen(missing), form));

    const char unknown[] =
        "light_threshold=2000&away_duration_s=180&return_duration_s=30&"
        "bogus=1";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::unknown_field,
        pf_web::parse_sensor_config_form(
            unknown, std::strlen(unknown), form));

    const char duplicate[] =
        "light_threshold=2000&away_duration_s=180&return_duration_s=30&"
        "light_threshold=1000";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::duplicate_field,
        pf_web::parse_sensor_config_form(
            duplicate, std::strlen(duplicate), form));
}

void test_form_rejects_malformed_checkbox_values()
{
    // Regression: a checkbox field must submit exactly "on" (what
    // browsers actually send); any other present value must not be
    // silently treated as "checked".
    const char empty_value[] =
        "light_threshold=2000&away_duration_s=180&return_duration_s=30&"
        "environment_enabled=";
    pf_web::SensorConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::invalid_value,
        pf_web::parse_sensor_config_form(
            empty_value, std::strlen(empty_value), form));

    const char zero_value[] =
        "light_threshold=2000&away_duration_s=180&return_duration_s=30&"
        "light_enabled=0";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::invalid_value,
        pf_web::parse_sensor_config_form(
            zero_value, std::strlen(zero_value), form));

    const char garbage_value[] =
        "light_threshold=2000&away_duration_s=180&return_duration_s=30&"
        "environment_enabled=garbage";
    TEST_ASSERT_EQUAL(
        pf_web::SensorConfigParseStatus::invalid_value,
        pf_web::parse_sensor_config_form(
            garbage_value, std::strlen(garbage_value), form));
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
    RUN_TEST(test_form_treats_absent_checkboxes_as_disabled);
    RUN_TEST(test_form_rejects_missing_unknown_and_duplicate_fields);
    RUN_TEST(test_form_rejects_malformed_checkbox_values);
    RUN_TEST(test_parse_sensor_u32_rejects_non_numeric_and_overflow);
    return UNITY_END();
}
