#include <cstring>

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
    RUN_TEST(test_form_rejects_missing_unknown_and_duplicate_fields);
    RUN_TEST(test_form_rejects_malformed_boolean);
    return UNITY_END();
}
