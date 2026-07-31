#include <unity.h>

#include "pf_config/sensor_settings.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_defaults_are_valid_and_disabled()
{
    pf_config::SensorSettings settings{};
    TEST_ASSERT_TRUE(pf_config::sensor_settings_valid(settings));
    TEST_ASSERT_FALSE(settings.environment_enabled);
    TEST_ASSERT_FALSE(settings.light_enabled);
    TEST_ASSERT_EQUAL_UINT16(2000U, settings.light_threshold);
    TEST_ASSERT_EQUAL_UINT32(180U, settings.away_duration_s);
    TEST_ASSERT_EQUAL_UINT32(30U, settings.return_duration_s);
}

void test_sensor_settings_round_trip_and_crc_rejects_mutation()
{
    pf_config::SensorSettings source{};
    source.environment_enabled = true;
    source.light_enabled = true;
    source.light_threshold = 1500U;
    source.away_duration_s = 300U;
    source.return_duration_s = 45U;

    pf_config::SensorSettingsBlob blob{};
    TEST_ASSERT_TRUE(pf_config::encode_sensor_settings(source, blob));
    pf_config::SensorSettings decoded{};
    TEST_ASSERT_TRUE(pf_config::decode_sensor_settings(blob, decoded));
    TEST_ASSERT_TRUE(decoded.environment_enabled);
    TEST_ASSERT_TRUE(decoded.light_enabled);
    TEST_ASSERT_EQUAL_UINT16(1500U, decoded.light_threshold);
    TEST_ASSERT_EQUAL_UINT32(300U, decoded.away_duration_s);
    TEST_ASSERT_EQUAL_UINT32(45U, decoded.return_duration_s);

    blob.light_threshold ^= 0x01U;
    TEST_ASSERT_FALSE(pf_config::decode_sensor_settings(blob, decoded));
}

void test_invalid_ranges_are_rejected()
{
    pf_config::SensorSettings settings{};
    settings.light_threshold = pf_config::kMaxLightThreshold + 1U;
    TEST_ASSERT_FALSE(pf_config::sensor_settings_valid(settings));

    settings = {};
    settings.away_duration_s = pf_config::kMinAwayDurationSeconds - 1U;
    TEST_ASSERT_FALSE(pf_config::sensor_settings_valid(settings));

    settings = {};
    settings.return_duration_s = pf_config::kMaxReturnDurationSeconds + 1U;
    TEST_ASSERT_FALSE(pf_config::sensor_settings_valid(settings));
}

void test_decode_rejects_non_boolean_flag_bytes()
{
    pf_config::SensorSettings source{};
    pf_config::SensorSettingsBlob blob{};
    TEST_ASSERT_TRUE(pf_config::encode_sensor_settings(source, blob));
    blob.environment_enabled = 7U;
    blob.crc32 = pf_config::sensor_settings_crc32(blob);
    pf_config::SensorSettings decoded{};
    TEST_ASSERT_FALSE(pf_config::decode_sensor_settings(blob, decoded));
}

}  // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid_and_disabled);
    RUN_TEST(test_sensor_settings_round_trip_and_crc_rejects_mutation);
    RUN_TEST(test_invalid_ranges_are_rejected);
    RUN_TEST(test_decode_rejects_non_boolean_flag_bytes);
    return UNITY_END();
}
