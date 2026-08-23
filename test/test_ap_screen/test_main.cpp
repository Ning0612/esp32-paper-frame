#include <cstring>
#include <limits>

#include <unity.h>

#include "pf_network/ap_screen.hpp"
#include "pf_network/detail/ap_screen_font.hpp"

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

void test_ap_screen_font_has_all_lowercase_letters()
{
    constexpr std::uint8_t kExpected[26][5] = {
        {0x20, 0x54, 0x54, 0x54, 0x78},
        {0x7F, 0x48, 0x44, 0x44, 0x38},
        {0x38, 0x44, 0x44, 0x44, 0x20},
        {0x38, 0x44, 0x44, 0x48, 0x7F},
        {0x38, 0x54, 0x54, 0x54, 0x18},
        {0x08, 0x7E, 0x09, 0x01, 0x02},
        {0x0C, 0x52, 0x52, 0x52, 0x3E},
        {0x7F, 0x08, 0x04, 0x04, 0x78},
        {0x00, 0x44, 0x7D, 0x40, 0x00},
        {0x20, 0x40, 0x44, 0x3D, 0x00},
        {0x7F, 0x10, 0x28, 0x44, 0x00},
        {0x00, 0x41, 0x7F, 0x40, 0x00},
        {0x7C, 0x04, 0x18, 0x04, 0x78},
        {0x7C, 0x08, 0x04, 0x04, 0x78},
        {0x38, 0x44, 0x44, 0x44, 0x38},
        {0x7C, 0x14, 0x14, 0x14, 0x08},
        {0x08, 0x14, 0x14, 0x18, 0x7C},
        {0x7C, 0x08, 0x04, 0x04, 0x08},
        {0x48, 0x54, 0x54, 0x54, 0x20},
        {0x08, 0x3E, 0x48, 0x48, 0x20},
        {0x3C, 0x40, 0x40, 0x40, 0x3C},
        {0x1C, 0x20, 0x40, 0x20, 0x1C},
        {0x3C, 0x40, 0x30, 0x40, 0x3C},
        {0x44, 0x28, 0x10, 0x28, 0x44},
        {0x0C, 0x50, 0x50, 0x50, 0x3C},
        {0x44, 0x64, 0x54, 0x4C, 0x44},
    };
    const std::uint8_t* const fallback =
        pf_network::detail::access_point_glyph_for('?');
    for (std::size_t index = 0U; index < 26U; ++index) {
        const char value = static_cast<char>('a' + index);
        const std::uint8_t* const glyph =
            pf_network::detail::access_point_glyph_for(value);
        TEST_ASSERT_NOT_NULL(glyph);
        TEST_ASSERT_TRUE(glyph != fallback);

        for (std::size_t column = 0U; column < 5U; ++column) {
            TEST_ASSERT_EQUAL_HEX8(kExpected[index][column], glyph[column]);
        }
    }
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

using pf_network::AccessPointScreenCache;

namespace {

pf_network::AccessPointScreenPayload cache_payload(const char* const ssid)
{
    pf_network::AccessPointScreenPayload payload{};
    pf_network::build_access_point_screen_payload(
        ssid, "PF-ABCDEFGHJKMN", "A1B2", payload);
    return payload;
}

}  // namespace

// The refresh skip is only sound while the AP screen really is on the
// panel. An empty cache claims nothing.
void test_an_empty_cache_never_claims_the_panel()
{
    const AccessPointScreenCache cache{};
    TEST_ASSERT_FALSE(cache.shows(cache_payload("PF-Setup")));
}

void test_a_displayed_payload_is_recognised()
{
    AccessPointScreenCache cache{};
    const pf_network::AccessPointScreenPayload payload = cache_payload("PF-Setup");
    cache.mark_displayed(payload);
    TEST_ASSERT_TRUE(cache.shows(payload));
    // A different payload is a different screen, cached or not.
    TEST_ASSERT_FALSE(cache.shows(cache_payload("PF-Other")));
}

// The defect this type exists to prevent (2026-08-23): the presenter held
// the payload but never dropped it, so a second AP session in the same
// boot -- same password, therefore same payload -- skipped its refresh
// even though the carousel had owned the panel in between. The radio came
// up with an image on the panel and the credentials nowhere to be seen.
void test_another_frame_on_the_panel_invalidates_the_cache()
{
    AccessPointScreenCache cache{};
    const pf_network::AccessPointScreenPayload payload = cache_payload("PF-Setup");
    cache.mark_displayed(payload);
    TEST_ASSERT_TRUE(cache.shows(payload));

    cache.invalidate();

    // Same payload, but the panel is showing something else now, so the
    // refresh must not be skipped.
    TEST_ASSERT_FALSE(cache.shows(payload));

    // And it recovers: showing it again re-establishes the claim.
    cache.mark_displayed(payload);
    TEST_ASSERT_TRUE(cache.shows(payload));
}


int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_ap_screen_payload_has_stable_golden_values);
    RUN_TEST(test_qr_payload_escapes_wifi_reserved_characters);
    RUN_TEST(test_ap_screen_font_has_all_lowercase_letters);
    RUN_TEST(test_same_payload_suppresses_redundant_refresh);
    RUN_TEST(test_ap_screen_stays_until_image_timeout_when_image_exists);
    RUN_TEST(test_ap_screen_stays_forever_without_a_displayable_image);
    RUN_TEST(test_ap_screen_policy_is_inactive_outside_ap_mode);
    RUN_TEST(test_an_empty_cache_never_claims_the_panel);
    RUN_TEST(test_a_displayed_payload_is_recognised);
    RUN_TEST(test_another_frame_on_the_panel_invalidates_the_cache);
    return UNITY_END();
}
