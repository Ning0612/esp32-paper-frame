#include <cstring>

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
    TEST_ASSERT_FALSE(settings.light1_enabled);
    TEST_ASSERT_FALSE(settings.light2_enabled);
    TEST_ASSERT_EQUAL_UINT16(2000U, settings.light1_threshold);
    TEST_ASSERT_EQUAL_UINT16(2000U, settings.light2_threshold);
    TEST_ASSERT_EQUAL_UINT32(180U, settings.away_duration_s);
    TEST_ASSERT_EQUAL_UINT32(30U, settings.return_duration_s);
}

void test_sensor_settings_round_trip_and_crc_rejects_mutation()
{
    pf_config::SensorSettings source{};
    source.environment_enabled = true;
    source.light1_enabled = true;
    source.light2_enabled = true;
    source.light1_threshold = 1500U;
    source.light2_threshold = 900U;
    source.away_duration_s = 300U;
    source.return_duration_s = 45U;

    pf_config::SensorSettingsBlob blob{};
    TEST_ASSERT_TRUE(pf_config::encode_sensor_settings(source, blob));
    TEST_ASSERT_EQUAL_UINT16(2U, blob.version);
    pf_config::SensorSettings decoded{};
    TEST_ASSERT_TRUE(pf_config::decode_sensor_settings(blob, decoded));
    TEST_ASSERT_TRUE(decoded.environment_enabled);
    TEST_ASSERT_TRUE(decoded.light1_enabled);
    TEST_ASSERT_TRUE(decoded.light2_enabled);
    TEST_ASSERT_EQUAL_UINT16(1500U, decoded.light1_threshold);
    TEST_ASSERT_EQUAL_UINT16(900U, decoded.light2_threshold);
    TEST_ASSERT_EQUAL_UINT32(300U, decoded.away_duration_s);
    TEST_ASSERT_EQUAL_UINT32(45U, decoded.return_duration_s);

    blob.light2_threshold ^= 0x01U;
    TEST_ASSERT_FALSE(pf_config::decode_sensor_settings(blob, decoded));
}

void test_invalid_ranges_are_rejected()
{
    pf_config::SensorSettings settings{};
    settings.light1_threshold = pf_config::kMaxLightThreshold + 1U;
    TEST_ASSERT_FALSE(pf_config::sensor_settings_valid(settings));

    settings = {};
    settings.light2_threshold = pf_config::kMaxLightThreshold + 1U;
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

    TEST_ASSERT_TRUE(pf_config::encode_sensor_settings(source, blob));
    blob.light2_enabled = 2U;
    blob.crc32 = pf_config::sensor_settings_crc32(blob);
    TEST_ASSERT_FALSE(pf_config::decode_sensor_settings(blob, decoded));
}

pf_config::SensorSettingsBlobV1 make_v1_blob()
{
    pf_config::SensorSettingsBlobV1 blob{};
    blob.environment_enabled = 1U;
    blob.light_enabled = 1U;
    blob.light_threshold = 1234U;
    blob.away_duration_s = 600U;
    blob.return_duration_s = 20U;
    blob.crc32 = pf_config::sensor_settings_v1_crc32(blob);
    return blob;
}

// The upgrade must not silently reset an existing device to defaults, so
// a stored v1 record keeps its durations and its threshold, and only the
// channel that never existed comes up disabled.
void test_v1_record_migrates_into_channel_one()
{
    const pf_config::SensorSettingsBlobV1 blob = make_v1_blob();
    pf_config::SensorSettings decoded{};
    TEST_ASSERT_TRUE(pf_config::decode_sensor_settings_v1(blob, decoded));
    TEST_ASSERT_TRUE(decoded.environment_enabled);
    TEST_ASSERT_TRUE(decoded.light1_enabled);
    TEST_ASSERT_EQUAL_UINT16(1234U, decoded.light1_threshold);
    TEST_ASSERT_FALSE(decoded.light2_enabled);
    TEST_ASSERT_EQUAL_UINT16(2000U, decoded.light2_threshold);
    TEST_ASSERT_EQUAL_UINT32(600U, decoded.away_duration_s);
    TEST_ASSERT_EQUAL_UINT32(20U, decoded.return_duration_s);
}

void test_v1_decode_rejects_mutation_and_wrong_version()
{
    pf_config::SensorSettingsBlobV1 blob = make_v1_blob();
    blob.light_threshold ^= 0x01U;
    pf_config::SensorSettings decoded{};
    TEST_ASSERT_FALSE(pf_config::decode_sensor_settings_v1(blob, decoded));

    blob = make_v1_blob();
    blob.version = 2U;
    blob.crc32 = pf_config::sensor_settings_v1_crc32(blob);
    TEST_ASSERT_FALSE(pf_config::decode_sensor_settings_v1(blob, decoded));
}

// load_sensor_settings() picks the decoder purely from the stored length,
// so the two layouts must never come out the same size.
void test_blob_sizes_stay_distinguishable()
{
    TEST_ASSERT_EQUAL_UINT32(28U, sizeof(pf_config::SensorSettingsBlob));
    TEST_ASSERT_EQUAL_UINT32(24U, sizeof(pf_config::SensorSettingsBlobV1));
}

// A v2 record must never be accepted by the v1 decoder or vice versa,
// which is what keeps a length mismatch from being read as valid data.
void test_versions_do_not_cross_decode()
{
    pf_config::SensorSettings source{};
    source.light1_enabled = true;
    pf_config::SensorSettingsBlob blob{};
    TEST_ASSERT_TRUE(pf_config::encode_sensor_settings(source, blob));

    pf_config::SensorSettingsBlobV1 reinterpreted{};
    static_assert(
        sizeof(reinterpreted) <= sizeof(blob),
        "v1 blob must be the shorter of the two layouts");
    std::memcpy(&reinterpreted, &blob, sizeof(reinterpreted));
    pf_config::SensorSettings decoded{};
    TEST_ASSERT_FALSE(
        pf_config::decode_sensor_settings_v1(reinterpreted, decoded));
}

}  // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid_and_disabled);
    RUN_TEST(test_sensor_settings_round_trip_and_crc_rejects_mutation);
    RUN_TEST(test_invalid_ranges_are_rejected);
    RUN_TEST(test_decode_rejects_non_boolean_flag_bytes);
    RUN_TEST(test_v1_record_migrates_into_channel_one);
    RUN_TEST(test_v1_decode_rejects_mutation_and_wrong_version);
    RUN_TEST(test_blob_sizes_stay_distinguishable);
    RUN_TEST(test_versions_do_not_cross_decode);
    return UNITY_END();
}
