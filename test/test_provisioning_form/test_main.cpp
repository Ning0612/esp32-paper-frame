#include <cstring>

#include <unity.h>

#include "pf_web/provisioning_form.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_form_decodes_hidden_ssid_and_password()
{
    constexpr char body[] =
        "ssid=Studio%20Wi-Fi&password=correct%2Bhorse";
    pf_web::ProvisioningForm form{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::ProvisioningParseStatus::ok),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                body,
                std::strlen(body),
                form)));
    TEST_ASSERT_EQUAL_STRING("Studio Wi-Fi", form.ssid);
    TEST_ASSERT_EQUAL_STRING("correct+horse", form.password);
}

void test_form_rejects_missing_duplicate_and_unknown_fields()
{
    pf_web::ProvisioningForm form{};
    constexpr char missing[] = "ssid=Only";
    constexpr char duplicate[] =
        "ssid=A&ssid=B&password=12345678";
    constexpr char unknown[] =
        "ssid=A&password=12345678&token=x";
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_web::ProvisioningParseStatus::missing_field),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                missing,
                std::strlen(missing),
                form)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_web::ProvisioningParseStatus::duplicate_field),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                duplicate,
                std::strlen(duplicate),
                form)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_web::ProvisioningParseStatus::unknown_field),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                unknown,
                std::strlen(unknown),
                form)));
}

void test_form_rejects_bad_encoding_controls_and_weak_password()
{
    pf_web::ProvisioningForm form{};
    constexpr char bad_encoding[] =
        "ssid=Bad%GG&password=12345678";
    constexpr char control[] =
        "ssid=Bad%00Name&password=12345678";
    constexpr char weak[] =
        "ssid=Valid&password=short";
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_web::ProvisioningParseStatus::bad_encoding),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                bad_encoding,
                std::strlen(bad_encoding),
                form)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_web::ProvisioningParseStatus::invalid_value),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                control,
                std::strlen(control),
                form)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_web::ProvisioningParseStatus::invalid_value),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                weak,
                std::strlen(weak),
                form)));
}

void test_open_network_is_allowed_with_empty_password()
{
    constexpr char body[] = "ssid=Guest&password=";
    pf_web::ProvisioningForm form{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::ProvisioningParseStatus::ok),
        static_cast<int>(
            pf_web::parse_provisioning_form(
                body,
                std::strlen(body),
                form)));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_form_decodes_hidden_ssid_and_password);
    RUN_TEST(test_form_rejects_missing_duplicate_and_unknown_fields);
    RUN_TEST(test_form_rejects_bad_encoding_controls_and_weak_password);
    RUN_TEST(test_open_network_is_allowed_with_empty_password);
    return UNITY_END();
}
