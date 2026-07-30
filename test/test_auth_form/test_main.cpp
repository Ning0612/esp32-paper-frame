#include <cstring>

#include <unity.h>

#include "pf_web/auth_form.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_admin_login_form_decodes_utf8_password()
{
    constexpr char kBody[] =
        "username=admin&password=%E6%B8%AC%E8%A9%A61234";
    pf_web::AuthForm form{};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_web::AuthParseStatus::ok),
        static_cast<std::uint8_t>(pf_web::parse_auth_form(
            kBody,
            sizeof(kBody) - 1U,
            form)));
    TEST_ASSERT_EQUAL_STRING("admin", form.username);
    TEST_ASSERT_EQUAL_STRING("測試1234", form.password);
}

void test_short_password_and_non_admin_user_are_rejected()
{
    constexpr char kShort[] =
        "username=admin&password=1234567";
    pf_web::AuthForm form{};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(
            pf_web::AuthParseStatus::invalid_value),
        static_cast<std::uint8_t>(pf_web::parse_auth_form(
            kShort,
            sizeof(kShort) - 1U,
            form)));

    constexpr char kWrongUser[] =
        "username=root&password=12345678";
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(
            pf_web::AuthParseStatus::invalid_value),
        static_cast<std::uint8_t>(pf_web::parse_auth_form(
            kWrongUser,
            sizeof(kWrongUser) - 1U,
            form)));
}

void test_duplicate_unknown_and_bad_encoding_are_rejected()
{
    pf_web::AuthForm form{};
    constexpr char kDuplicate[] =
        "username=admin&password=12345678&password=abcdefgh";
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(
            pf_web::AuthParseStatus::duplicate_field),
        static_cast<std::uint8_t>(pf_web::parse_auth_form(
            kDuplicate,
            sizeof(kDuplicate) - 1U,
            form)));

    constexpr char kUnknown[] =
        "username=admin&password=12345678&redirect=/";
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(
            pf_web::AuthParseStatus::unknown_field),
        static_cast<std::uint8_t>(pf_web::parse_auth_form(
            kUnknown,
            sizeof(kUnknown) - 1U,
            form)));

    constexpr char kBadEncoding[] =
        "username=admin&password=1234567%";
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(
            pf_web::AuthParseStatus::bad_encoding),
        static_cast<std::uint8_t>(pf_web::parse_auth_form(
            kBadEncoding,
            sizeof(kBadEncoding) - 1U,
            form)));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_admin_login_form_decodes_utf8_password);
    RUN_TEST(test_short_password_and_non_admin_user_are_rejected);
    RUN_TEST(test_duplicate_unknown_and_bad_encoding_are_rejected);
    return UNITY_END();
}
