#include <cstring>

#include <unity.h>

#include "pf_config/weather_settings.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_defaults_are_valid_and_api_key_is_empty()
{
    pf_config::WeatherSettings settings{};
    TEST_ASSERT_TRUE(pf_config::weather_settings_valid(settings));
    TEST_ASSERT_EQUAL_STRING("metric", settings.units);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", settings.ntp_server);
    TEST_ASSERT_EQUAL_STRING("", settings.api_key);
}

void test_weather_settings_round_trip_and_crc_rejects_mutation()
{
    pf_config::WeatherSettings source{};
    std::strcpy(source.api_key, "abcdefgh12345678");
    source.latitude_e6 = 22'627'000;
    source.longitude_e6 = 120'301'000;
    std::strcpy(source.units, "imperial");

    pf_config::WeatherSettingsBlob blob{};
    TEST_ASSERT_TRUE(pf_config::encode_weather_settings(source, blob));
    pf_config::WeatherSettings decoded{};
    TEST_ASSERT_TRUE(pf_config::decode_weather_settings(blob, decoded));
    TEST_ASSERT_EQUAL_STRING(source.api_key, decoded.api_key);
    TEST_ASSERT_EQUAL_INT32(source.latitude_e6, decoded.latitude_e6);
    TEST_ASSERT_EQUAL_INT32(source.longitude_e6, decoded.longitude_e6);
    TEST_ASSERT_EQUAL_STRING(source.units, decoded.units);

    blob.units[0] ^= 0x01;
    TEST_ASSERT_FALSE(pf_config::decode_weather_settings(blob, decoded));
}

void test_invalid_ranges_and_weak_keys_are_rejected()
{
    pf_config::WeatherSettings settings{};
    settings.latitude_e6 = 90'000'001;
    TEST_ASSERT_FALSE(pf_config::weather_settings_valid(settings));

    settings = {};
    std::strcpy(settings.api_key, "short");
    TEST_ASSERT_FALSE(pf_config::weather_settings_valid(settings));

    settings = {};
    std::strcpy(settings.units, "kelvin");
    TEST_ASSERT_FALSE(pf_config::weather_settings_valid(settings));
}

void test_blob_requires_bounded_terminated_text_and_clears_key_tail()
{
    pf_config::WeatherSettings source{};
    std::strcpy(source.api_key, "abcdefgh12345678");
    pf_config::WeatherSettingsBlob blob{};
    std::memset(blob.api_key, 0xA5, sizeof(blob.api_key));
    TEST_ASSERT_TRUE(pf_config::encode_weather_settings(source, blob));
    TEST_ASSERT_EQUAL_UINT8(0U, blob.api_key[16]);
    for (std::size_t index = 17U; index < sizeof(blob.api_key); ++index) {
        TEST_ASSERT_EQUAL_UINT8(0U, blob.api_key[index]);
    }

    std::memset(blob.units, 'x', sizeof(blob.units));
    blob.crc32 = pf_config::weather_settings_crc32(blob);
    pf_config::WeatherSettings decoded{};
    TEST_ASSERT_FALSE(pf_config::decode_weather_settings(blob, decoded));
}

}  // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid_and_api_key_is_empty);
    RUN_TEST(test_weather_settings_round_trip_and_crc_rejects_mutation);
    RUN_TEST(test_invalid_ranges_and_weak_keys_are_rejected);
    RUN_TEST(test_blob_requires_bounded_terminated_text_and_clears_key_tail);
    return UNITY_END();
}
