#include <cstring>
#include <cstdio>

#include "unity.h"
#include "pf_web/weather_config_form.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_form_accepts_masked_key_update_fields()
{
    const char body[] =
        "latitude_e6=25033000&longitude_e6=121565000&"
        "interval_minutes=30&location=Taipei+City&units=metric&"
        "language=zh_tw&ntp_server=pool.ntp.org&api_key=newkey123";
    pf_web::WeatherConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::WeatherConfigParseStatus::ok,
        pf_web::parse_weather_config_form(body, std::strlen(body), form));
    TEST_ASSERT_TRUE(form.api_key_seen);
    TEST_ASSERT_EQUAL_STRING("newkey123", form.api_key);
    TEST_ASSERT_EQUAL_STRING("Taipei City", form.location);
}

void test_form_allows_clearing_optional_api_key()
{
    const char body[] =
        "latitude_e6=0&longitude_e6=0&interval_minutes=10&"
        "location=Lab&units=imperial&language=en&ntp_server=time.example";
    pf_web::WeatherConfigForm form{};
    TEST_ASSERT_EQUAL(
        pf_web::WeatherConfigParseStatus::ok,
        pf_web::parse_weather_config_form(body, std::strlen(body), form));
    TEST_ASSERT_FALSE(form.api_key_seen);
}

void test_form_rejects_unknown_duplicate_and_bad_encoding()
{
    const char common[] =
        "latitude_e6=0&longitude_e6=0&interval_minutes=10&"
        "location=Lab&units=metric&language=en&ntp_server=time.example";
    pf_web::WeatherConfigForm form{};
    char duplicate[256]{};
    std::snprintf(duplicate, sizeof(duplicate), "%s&units=metric", common);
    TEST_ASSERT_EQUAL(
        pf_web::WeatherConfigParseStatus::duplicate_field,
        pf_web::parse_weather_config_form(
            duplicate,
            std::strlen(duplicate),
            form));
    const char unknown[] = "unknown=x";
    TEST_ASSERT_EQUAL(
        pf_web::WeatherConfigParseStatus::unknown_field,
        pf_web::parse_weather_config_form(unknown, std::strlen(unknown), form));
    const char bad[] =
        "latitude_e6=%ZZ&longitude_e6=0&interval_minutes=10&"
        "location=Lab&units=metric&language=en&ntp_server=time.example";
    TEST_ASSERT_EQUAL(
        pf_web::WeatherConfigParseStatus::bad_encoding,
        pf_web::parse_weather_config_form(bad, std::strlen(bad), form));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_form_accepts_masked_key_update_fields);
    RUN_TEST(test_form_allows_clearing_optional_api_key);
    RUN_TEST(test_form_rejects_unknown_duplicate_and_bad_encoding);
    return UNITY_END();
}
