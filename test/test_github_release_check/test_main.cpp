#include <cstring>
#include <string>

#include <unity.h>

#include "pf_ota/github_release_check.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_ota::extract_tag_name;

pf_ota::GithubTagExtractResult extract(const char* const json)
{
    return extract_tag_name(json, std::strlen(json));
}

void test_extracts_tag_name_from_typical_response()
{
    const auto result = extract(
        "{\"url\":\"https://api.github.com/x\",\"tag_name\":\"v0.9.0\","
        "\"name\":\"Release 0.9.0\",\"draft\":false}");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("v0.9.0", result.tag_name);
}

void test_tag_name_field_order_does_not_matter()
{
    const auto result = extract("{\"tag_name\":\"v1.0.0\",\"name\":\"x\"}");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("v1.0.0", result.tag_name);
}

void test_missing_tag_name_field_is_not_ok()
{
    const auto result = extract("{\"name\":\"Release\",\"draft\":false}");
    TEST_ASSERT_FALSE(result.ok);
}

void test_nested_tag_name_inside_assets_array_is_ignored()
{
    // A tag_name-shaped key inside a nested object/array must not be
    // mistaken for the real top-level field.
    const auto result = extract(
        "{\"assets\":[{\"tag_name\":\"nested-should-be-ignored\"}],"
        "\"tag_name\":\"v2.0.0\"}");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("v2.0.0", result.tag_name);
}

void test_response_truncated_mid_tag_value_is_not_ok()
{
    // A response cut off mid-field (e.g. a dropped connection) must fail
    // closed. Note that trailing content *after* a fully-formed tag_name
    // field (including a missing final '}') is intentionally not
    // validated -- this extractor targets one field, the same
    // single-field-scan tradeoff pf_weather::weather.cpp's parser makes,
    // not a general well-formedness check.
    const auto result = extract("{\"tag_name\":\"v1.0");
    TEST_ASSERT_FALSE(result.ok);
}

void test_non_string_tag_name_value_is_not_ok()
{
    const auto result = extract("{\"tag_name\":123}");
    TEST_ASSERT_FALSE(result.ok);
}

void test_empty_tag_name_value_is_not_ok()
{
    const auto result = extract("{\"tag_name\":\"\"}");
    TEST_ASSERT_FALSE(result.ok);
}

void test_overlong_tag_name_is_rejected_not_truncated_silently()
{
    // A silently truncated tag name could compare unequal to the real
    // release tag forever; must fail closed instead.
    std::string overlong = "{\"tag_name\":\"v";
    for (int i = 0; i < 40; ++i) {
        overlong += "9";
    }
    overlong += "\"}";
    const auto result = extract_tag_name(overlong.c_str(), overlong.size());
    TEST_ASSERT_FALSE(result.ok);
}

void test_null_and_empty_input_are_not_ok()
{
    TEST_ASSERT_FALSE(extract_tag_name(nullptr, 0U).ok);
    TEST_ASSERT_FALSE(extract_tag_name("", 0U).ok);
}

void test_response_not_starting_with_object_is_not_ok()
{
    const auto result = extract("[\"tag_name\",\"v1.0.0\"]");
    TEST_ASSERT_FALSE(result.ok);
}

void test_value_matching_key_text_does_not_abort_the_scan()
{
    // A string *value* that happens to equal "tag_name" must not be
    // mistaken for the key and abort the scan before the real field.
    const auto result = extract("{\"name\":\"tag_name\",\"tag_name\":\"v3.0.0\"}");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("v3.0.0", result.tag_name);
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_extracts_tag_name_from_typical_response);
    RUN_TEST(test_tag_name_field_order_does_not_matter);
    RUN_TEST(test_missing_tag_name_field_is_not_ok);
    RUN_TEST(test_nested_tag_name_inside_assets_array_is_ignored);
    RUN_TEST(test_response_truncated_mid_tag_value_is_not_ok);
    RUN_TEST(test_non_string_tag_name_value_is_not_ok);
    RUN_TEST(test_empty_tag_name_value_is_not_ok);
    RUN_TEST(test_overlong_tag_name_is_rejected_not_truncated_silently);
    RUN_TEST(test_null_and_empty_input_are_not_ok);
    RUN_TEST(test_response_not_starting_with_object_is_not_ok);
    RUN_TEST(test_value_matching_key_text_does_not_abort_the_scan);
    return UNITY_END();
}
