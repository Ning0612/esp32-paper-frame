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

using pf_network::ApModeWindowAction;

// The bug this guards against (2026-08-25): app_main's mirror of AP-session
// state is only refreshed once per loop tick, and a plain "did wifi go
// true/false" transition check can miss an entire starting_ap ->
// provisioning cycle -- or provisioning -> starting_ap -> provisioning,
// i.e. NetworkService re-entering starting_ap without ever leaving AP mode
// -- that happened between two ticks. Comparing RuntimeCoordinator's
// monotonic ap_session_id instead of diffing booleans catches both
// regardless of how many intermediate ticks were missed.
void test_fresh_entry_into_ap_mode_resyncs()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ApModeWindowAction::resync),
        static_cast<int>(
            pf_network::classify_ap_mode_window(
                false, false, 0U,   // was: inactive, session 0
                true, false, 1U))); // now: starting_ap, session 1
}

void test_session_change_while_still_in_ap_mode_resyncs()
{
    // Mirrors "provisioning -> starting_ap -> provisioning" happening
    // entirely between two main-loop ticks: both observations are
    // `provisioning`, but the session id moved on.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ApModeWindowAction::resync),
        static_cast<int>(
            pf_network::classify_ap_mode_window(
                true, true, 1U,     // was: active, ready, session 1
                true, true, 2U)));  // now: still provisioning, session 2
}

void test_same_session_becoming_ready_does_not_resync()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ApModeWindowAction::became_ready),
        static_cast<int>(
            pf_network::classify_ap_mode_window(
                true, false, 1U,    // was: active, waiting, session 1
                true, true, 1U)));  // now: ready, same session
}

void test_leaving_ap_mode_ends_regardless_of_session_id()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ApModeWindowAction::ended),
        static_cast<int>(
            pf_network::classify_ap_mode_window(
                true, true, 1U,
                false, false, 1U)));
}

void test_no_change_is_unchanged()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ApModeWindowAction::unchanged),
        static_cast<int>(
            pf_network::classify_ap_mode_window(
                true, true, 1U,
                true, true, 1U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ApModeWindowAction::unchanged),
        static_cast<int>(
            pf_network::classify_ap_mode_window(
                false, false, 0U,
                false, false, 0U)));
}

// The regression this guards against (round 2 of the same 2026-08-25
// codex-cowork review): ApModeWindowAction::ended resets the caller's
// local session id to 0, but RuntimeSnapshot::ap_session_id is never
// reset when AP mode ends -- it keeps counting starting_ap entries. An
// unconditional session-id comparison in the gate would then deny every
// submission for the rest of the device's uptime, since a permanently-0
// local id can never again equal the snapshot's permanently-nonzero one.
void test_gate_allows_submission_once_ap_mode_has_ended()
{
    TEST_ASSERT_FALSE(
        pf_network::submission_gate_denies_for_ap_session(
            false,  // ap_screen_owns_panel: false, AP mode is over
            false,  // fresh_wifi_in_ap_mode: wifi moved on (e.g. connected)
            1U,     // fresh snapshot's ap_session_id: last AP session
            0U));   // caller's local id, reset to 0 on ApModeWindowAction::ended
}

void test_gate_denies_when_session_id_disagrees_while_still_in_ap_mode()
{
    // The original bug this mechanism fixes: stale verdict said `false`,
    // but the fresh snapshot is still (or newly) in AP mode under a
    // session the caller's verdict was not computed against.
    TEST_ASSERT_TRUE(
        pf_network::submission_gate_denies_for_ap_session(
            false, true, 2U, 1U));
}

void test_gate_denies_when_stale_verdict_already_claims_ownership()
{
    TEST_ASSERT_TRUE(
        pf_network::submission_gate_denies_for_ap_session(
            true, false, 0U, 0U));
}

void test_gate_allows_when_session_matches_and_verdict_is_false()
{
    TEST_ASSERT_FALSE(
        pf_network::submission_gate_denies_for_ap_session(
            false, true, 5U, 5U));
}

// End-to-end regression test for a scenario raised in round 3 of the same
// review, which this test could not reproduce against the actual code:
// same-session starting_ap -> provisioning ("became_ready") within a
// single main-loop tick, followed by a gate check in that same tick.
// Mirrors app_main's ApModeWindowAction::became_ready handling exactly
// (ap_mode_ready and ap_mode_started_ms are set together, atomically, in
// the same branch, before any gate call later in that tick can run) and
// then evaluates the verdict + gate the same way app_main does, to prove
// this transition cannot produce a false verdict against a still-in-AP
// fresh snapshot.
void test_became_ready_transition_still_denies_within_same_tick()
{
    // Local mirror synced last tick to starting_ap: active, not ready yet.
    bool ap_mode_ready = false;
    std::uint64_t ap_mode_started_ms = 0U;
    const std::uint32_t ap_mode_session_id = 1U;

    // This tick's fresh read: still session 1, now provisioning (ready).
    const std::uint32_t current_ap_session_id = 1U;
    const std::uint64_t now_ms = 500'000U;

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ApModeWindowAction::became_ready),
        static_cast<int>(
            pf_network::classify_ap_mode_window(
                /*was_ap_mode_active=*/true,
                ap_mode_ready,
                ap_mode_session_id,
                /*is_ap_mode=*/true,
                /*is_ap_ready=*/true,
                current_ap_session_id)));

    // app_main's became_ready branch: both fields set together, same tick.
    ap_mode_ready = true;
    ap_mode_started_ms = now_ms;

    // A carousel call later in this same tick computes the verdict from
    // the just-updated mirror, then the gate rechecks a fresh snapshot
    // (same session id, same wifi-in-AP-mode state -- nothing changed
    // between the two reads within one tick).
    const bool verdict =
        /*ap_mode_active=*/true &&
        (!ap_mode_ready ||
         pf_network::should_hold_access_point_screen(
             true,
             /*last_has_displayable_image=*/true,
             now_ms,
             ap_mode_started_ms));
    TEST_ASSERT_TRUE(verdict);
    TEST_ASSERT_TRUE(
        pf_network::submission_gate_denies_for_ap_session(
            verdict,
            /*fresh_wifi_in_ap_mode=*/true,
            current_ap_session_id,
            /*verdict_session_id=*/ap_mode_session_id));
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
void test_another_frame_on_the_panel_supersedes_the_cache()
{
    AccessPointScreenCache cache{};
    const pf_network::AccessPointScreenPayload payload = cache_payload("PF-Setup");
    cache.mark_displayed(payload);
    TEST_ASSERT_TRUE(cache.shows(payload));

    cache.mark_superseded();

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
    RUN_TEST(test_fresh_entry_into_ap_mode_resyncs);
    RUN_TEST(test_session_change_while_still_in_ap_mode_resyncs);
    RUN_TEST(test_same_session_becoming_ready_does_not_resync);
    RUN_TEST(test_leaving_ap_mode_ends_regardless_of_session_id);
    RUN_TEST(test_no_change_is_unchanged);
    RUN_TEST(test_gate_allows_submission_once_ap_mode_has_ended);
    RUN_TEST(test_gate_denies_when_session_id_disagrees_while_still_in_ap_mode);
    RUN_TEST(test_gate_denies_when_stale_verdict_already_claims_ownership);
    RUN_TEST(test_gate_allows_when_session_matches_and_verdict_is_false);
    RUN_TEST(test_became_ready_transition_still_denies_within_same_tick);
    RUN_TEST(test_an_empty_cache_never_claims_the_panel);
    RUN_TEST(test_a_displayed_payload_is_recognised);
    RUN_TEST(test_another_frame_on_the_panel_supersedes_the_cache);
    return UNITY_END();
}
