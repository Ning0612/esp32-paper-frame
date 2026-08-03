#include <cstring>
#include <limits>

#include <unity.h>

#include "pf_network/ap_screen.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_ap_screen_payload_has_stable_golden_values()
{
    pf_network::AccessPointScreenPayload payload{};
    TEST_ASSERT_TRUE(
        pf_network::build_access_point_screen_payload(
            "PaperFrame-Setup-A1B2",
            "PF-ABCDEFGHJKMN",
            "A1B2",
            payload));

    TEST_ASSERT_EQUAL_STRING(
        "PaperFrame-Setup-A1B2",
        payload.ssid);
    TEST_ASSERT_EQUAL_STRING("PF-ABCDEFGHJKMN", payload.password);
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", payload.ip_address);
    TEST_ASSERT_EQUAL_STRING(
        "WIFI:T:WPA;S:PaperFrame-Setup-A1B2;P:PF-ABCDEFGHJKMN;;",
        payload.wifi_qr);
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.4.1/",
        payload.web_qr);
    TEST_ASSERT_EQUAL_STRING("A1B2", payload.device_suffix);
}

void test_qr_payload_escapes_wifi_reserved_characters()
{
    pf_network::AccessPointScreenPayload payload{};
    TEST_ASSERT_TRUE(
        pf_network::build_access_point_screen_payload(
            "Lab;2",
            "pass:word",
            "00AF",
            payload));
    TEST_ASSERT_EQUAL_STRING(
        "WIFI:T:WPA;S:Lab\\;2;P:pass\\:word;;",
        payload.wifi_qr);
}

void test_same_payload_suppresses_redundant_refresh()
{
    pf_network::AccessPointScreenPayload first{};
    pf_network::AccessPointScreenPayload second{};
    pf_network::build_access_point_screen_payload(
        "PaperFrame-Setup-A1B2",
        "PF-ABCDEFGHJKMN",
        "A1B2",
        first);
    second = first;

    TEST_ASSERT_TRUE(
        pf_network::same_access_point_screen_payload(
            first,
            second));
    second.device_suffix[3] = 'F';
    TEST_ASSERT_FALSE(
        pf_network::same_access_point_screen_payload(
            first,
            second));
}

void test_ap_screen_stays_until_image_timeout_when_image_exists()
{
    constexpr std::uint64_t ap_started_ms = 1000U;

    TEST_ASSERT_EQUAL_UINT64(
        5U * 60U * 1000U,
        pf_network::kApModeImageTimeoutMs);

    TEST_ASSERT_TRUE(
        pf_network::should_hold_access_point_screen(
            true,
            true,
            ap_started_ms + pf_network::kApModeImageTimeoutMs - 1U,
            ap_started_ms));
    TEST_ASSERT_FALSE(
        pf_network::should_hold_access_point_screen(
            true,
            true,
            ap_started_ms + pf_network::kApModeImageTimeoutMs,
            ap_started_ms));
}

void test_ap_screen_stays_forever_without_a_displayable_image()
{
    TEST_ASSERT_TRUE(
        pf_network::should_hold_access_point_screen(
            true,
            false,
            std::numeric_limits<std::uint64_t>::max(),
            0U));
}

void test_ap_screen_policy_is_inactive_outside_ap_mode()
{
    TEST_ASSERT_FALSE(
        pf_network::should_hold_access_point_screen(
            false,
            false,
            0U,
            0U));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_ap_screen_payload_has_stable_golden_values);
    RUN_TEST(test_qr_payload_escapes_wifi_reserved_characters);
    RUN_TEST(test_same_payload_suppresses_redundant_refresh);
    RUN_TEST(test_ap_screen_stays_until_image_timeout_when_image_exists);
    RUN_TEST(test_ap_screen_stays_forever_without_a_displayable_image);
    RUN_TEST(test_ap_screen_policy_is_inactive_outside_ap_mode);
    return UNITY_END();
}
