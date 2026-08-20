#include <cstring>
#include <initializer_list>

#include <unity.h>

#include "pf_web/carousel_config_form.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_form_accepts_true_and_false_values()
{
    pf_web::CarouselConfigForm form{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=true", std::strlen("random=true"), form)));
    TEST_ASSERT_TRUE(form.random);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=false", std::strlen("random=false"), form)));
    TEST_ASSERT_FALSE(form.random);
    TEST_ASSERT_FALSE(form.refresh_minutes_seen);
}

void test_form_accepts_refresh_interval_bounds()
{
    pf_web::CarouselConfigForm form{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=true&refresh_minutes=10",
            std::strlen("random=true&refresh_minutes=10"),
            form)));
    TEST_ASSERT_TRUE(form.random);
    TEST_ASSERT_TRUE(form.refresh_minutes_seen);
    TEST_ASSERT_EQUAL_UINT32(10U, form.refresh_minutes);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=false&refresh_minutes=1440",
            std::strlen("random=false&refresh_minutes=1440"),
            form)));
    TEST_ASSERT_FALSE(form.random);
    TEST_ASSERT_EQUAL_UINT32(1440U, form.refresh_minutes);
}

void test_form_rejects_refresh_interval_outside_bounds()
{
    pf_web::CarouselConfigForm form{};
    for (const char* body : {
             "random=false&refresh_minutes=9",
             "random=false&refresh_minutes=1441",
             "random=false&refresh_minutes=abc",
         }) {
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(pf_web::CarouselConfigParseStatus::invalid_value),
            static_cast<int>(pf_web::parse_carousel_config_form(
                body,
                std::strlen(body),
                form)));
    }
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::duplicate_field),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=false&refresh_minutes=10&refresh_minutes=20",
            std::strlen("random=false&refresh_minutes=10&refresh_minutes=20"),
            form)));
}

void test_form_rejects_missing_unknown_and_duplicate_fields()
{
    pf_web::CarouselConfigForm form{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::missing_field),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "", 0U, form)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::unknown_field),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "mode=true", std::strlen("mode=true"), form)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::duplicate_field),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=true&random=false",
            std::strlen("random=true&random=false"),
            form)));
}

void test_form_accepts_timezone_field()
{
    pf_web::CarouselConfigForm form{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=true&timezone=%2B5.5",
            std::strlen("random=true&timezone=%2B5.5"),
            form)));
    TEST_ASSERT_TRUE(form.timezone_seen);
    TEST_ASSERT_EQUAL_STRING("+5.5", form.timezone);

    pf_web::CarouselConfigForm form_without_timezone{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=false", std::strlen("random=false"), form_without_timezone)));
    TEST_ASSERT_FALSE(form_without_timezone.timezone_seen);
}

void test_form_rejects_invalid_timezone_field()
{
    pf_web::CarouselConfigForm form{};
    for (const char* body : {
             "random=true&timezone=Asia%2FTaipei",
             "random=true&timezone=%2B15",
             "random=true&timezone=",
         }) {
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(pf_web::CarouselConfigParseStatus::invalid_value),
            static_cast<int>(pf_web::parse_carousel_config_form(
                body, std::strlen(body), form)));
    }
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::duplicate_field),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=true&timezone=%2B8&timezone=%2B9",
            std::strlen("random=true&timezone=%2B8&timezone=%2B9"),
            form)));
}

// The WebUI's timezone-only save posts just `timezone=...` -- random must
// stay optional (mirroring refresh_minutes_seen/timezone_seen) so the caller
// can preserve the carousel's existing mode instead of being forced to
// resend a possibly-stale snapshot of it.
void test_form_random_field_is_optional()
{
    pf_web::CarouselConfigForm form{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "timezone=%2B8", std::strlen("timezone=%2B8"), form)));
    TEST_ASSERT_FALSE(form.random_seen);
    TEST_ASSERT_TRUE(form.timezone_seen);
    TEST_ASSERT_EQUAL_STRING("+8", form.timezone);

    pf_web::CarouselConfigForm form_with_random{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::ok),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=true", std::strlen("random=true"), form_with_random)));
    TEST_ASSERT_TRUE(form_with_random.random_seen);
    TEST_ASSERT_TRUE(form_with_random.random);
}

void test_form_rejects_malformed_boolean()
{
    pf_web::CarouselConfigForm form{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::invalid_value),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random=1", std::strlen("random=1"), form)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::CarouselConfigParseStatus::invalid_value),
        static_cast<int>(pf_web::parse_carousel_config_form(
            "random", std::strlen("random"), form)));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_form_accepts_true_and_false_values);
    RUN_TEST(test_form_accepts_refresh_interval_bounds);
    RUN_TEST(test_form_rejects_refresh_interval_outside_bounds);
    RUN_TEST(test_form_rejects_missing_unknown_and_duplicate_fields);
    RUN_TEST(test_form_accepts_timezone_field);
    RUN_TEST(test_form_rejects_invalid_timezone_field);
    RUN_TEST(test_form_random_field_is_optional);
    RUN_TEST(test_form_rejects_malformed_boolean);
    return UNITY_END();
}
