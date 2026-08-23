#include <cstdint>
#include <initializer_list>

#include <unity.h>

#include "pf_sensors/presence.hpp"

using pf_sensors::LightSensorStatus;
using pf_sensors::PresenceState;
using pf_sensors::PresenceTracker;
using pf_sensors::update_presence;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

constexpr std::uint64_t kAwayMs = 180U * 1000U;
constexpr std::uint64_t kReturnMs = 30U * 1000U;
constexpr std::uint16_t kThreshold = 2000U;

void test_away_requires_the_full_debounce_duration()
{
    PresenceTracker tracker{};
    // Establish PRESENT first so the away transition isn't the initial one.
    update_presence(
        tracker, LightSensorStatus::online, 3000U, kThreshold, 0U, kAwayMs,
        kReturnMs);
    auto result = update_presence(
        tracker, LightSensorStatus::online, 3000U, kThreshold, kReturnMs,
        kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::present),
        static_cast<int>(result.state));

    // Darkness candidate starts now, at kReturnMs.
    constexpr std::uint64_t kDarkStartMs = kReturnMs;
    result = update_presence(
        tracker, LightSensorStatus::online, 1000U, kThreshold, kDarkStartMs,
        kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::present),
        static_cast<int>(result.state));

    // Same darkness held for just under kAwayMs since it started: still
    // present.
    result = update_presence(
        tracker, LightSensorStatus::online, 1000U, kThreshold,
        kDarkStartMs + kAwayMs - 1U, kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::present),
        static_cast<int>(result.state));
    TEST_ASSERT_FALSE(result.transitioned);

    // Darkness held for the full kAwayMs now switches to away.
    result = update_presence(
        tracker, LightSensorStatus::online, 1000U, kThreshold,
        kDarkStartMs + kAwayMs, kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::away),
        static_cast<int>(result.state));
    TEST_ASSERT_TRUE(result.transitioned);
}

void test_return_requires_its_own_shorter_duration()
{
    PresenceTracker tracker{};
    tracker.state = PresenceState::away;
    tracker.candidate = PresenceState::away;

    constexpr std::uint64_t kBrightStartMs = 1000000U;
    auto result = update_presence(
        tracker, LightSensorStatus::online, 3000U, kThreshold,
        kBrightStartMs, kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::away),
        static_cast<int>(result.state));

    result = update_presence(
        tracker, LightSensorStatus::online, 3000U, kThreshold,
        kBrightStartMs + kReturnMs - 1U, kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::away),
        static_cast<int>(result.state));
    TEST_ASSERT_FALSE(result.transitioned);

    result = update_presence(
        tracker, LightSensorStatus::online, 3000U, kThreshold,
        kBrightStartMs + kReturnMs, kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::present),
        static_cast<int>(result.state));
    TEST_ASSERT_TRUE(result.transitioned);
}

void test_flickering_candidate_resets_the_debounce_timer()
{
    PresenceTracker tracker{};
    tracker.state = PresenceState::present;
    tracker.candidate = PresenceState::present;

    update_presence(
        tracker, LightSensorStatus::online, 1000U, kThreshold, 0U, kAwayMs,
        kReturnMs);
    // Held dark for kAwayMs - 1ms...
    auto result = update_presence(
        tracker, LightSensorStatus::online, 1000U, kThreshold,
        kAwayMs - 1U, kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::present),
        static_cast<int>(result.state));
    // ...then a single bright sample resets the away candidate timer.
    update_presence(
        tracker, LightSensorStatus::online, 3000U, kThreshold, kAwayMs,
        kAwayMs, kReturnMs);
    result = update_presence(
        tracker, LightSensorStatus::online, 1000U, kThreshold,
        kAwayMs + kAwayMs - 1U, kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::present),
        static_cast<int>(result.state));
}

void test_not_detected_saturated_and_error_never_trigger_away()
{
    for (const LightSensorStatus status : {
             LightSensorStatus::not_detected,
             LightSensorStatus::saturated,
             LightSensorStatus::error,
             LightSensorStatus::disabled,
         }) {
        PresenceTracker tracker{};
        tracker.state = PresenceState::present;
        tracker.candidate = PresenceState::present;
        const auto result = update_presence(
            tracker, status, 0U, kThreshold, 1000000U, kAwayMs, kReturnMs);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(PresenceState::unknown),
            static_cast<int>(result.state));
    }
}

void test_unknown_light_sensor_status_leaves_state_unknown_immediately()
{
    PresenceTracker tracker{};
    const auto result = update_presence(
        tracker, LightSensorStatus::not_detected, 0U, kThreshold, 0U,
        kAwayMs, kReturnMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresenceState::unknown),
        static_cast<int>(result.state));
    TEST_ASSERT_FALSE(result.transitioned);
}

using pf_sensors::PresencePanelAction;
using pf_sensors::presence_panel_action;
using pf_sensors::PresenceState;

// The boot case. Presence starts `unknown` and converges to `present` a
// few seconds in, but nothing blanked the panel, so there is nothing to
// restore. Treating it as a return cost a full ~31 s refresh on every
// boot and replaced the stored current image with a fresh carousel pick
// (2026-08-23, reproduced across two reboots).
void test_unknown_to_present_is_not_a_return()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresencePanelAction::none),
        static_cast<int>(presence_panel_action(
            PresenceState::unknown, PresenceState::present, false)));
}

void test_away_to_present_restores_the_blanked_panel()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresencePanelAction::restore_after_away),
        static_cast<int>(presence_panel_action(
            PresenceState::away, PresenceState::present, true)));
}

// A sensor fault mid-debounce collapses presence to `unknown` while the
// away blank is still on the panel. Coming back from that must still
// restore, which is why the decision keys on what the panel shows rather
// than on the previous state.
void test_unknown_to_present_restores_when_the_panel_is_blank()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PresencePanelAction::restore_after_away),
        static_cast<int>(presence_panel_action(
            PresenceState::unknown, PresenceState::present, true)));
}

// The inverse guard: a return can never be claimed while the panel is
// showing a real frame, whatever state it came from.
void test_present_is_never_a_return_without_a_blank()
{
    for (const PresenceState previous : {PresenceState::unknown,
                                         PresenceState::away,
                                         PresenceState::present}) {
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(PresencePanelAction::none),
            static_cast<int>(presence_panel_action(
                previous, PresenceState::present, false)));
    }
}

void test_any_transition_to_away_blanks_regardless_of_panel_contents()
{
    for (const bool blank_on_panel : {false, true}) {
        for (const PresenceState previous : {PresenceState::unknown,
                                             PresenceState::present}) {
            TEST_ASSERT_EQUAL_INT(
                static_cast<int>(PresencePanelAction::blank_for_away),
                static_cast<int>(presence_panel_action(
                    previous, PresenceState::away, blank_on_panel)));
        }
    }
}

// Presence going unknown is not itself a panel event: the device does not
// know whether anyone is there, and whatever is displayed stays.
void test_collapsing_to_unknown_touches_nothing()
{
    for (const bool blank_on_panel : {false, true}) {
        for (const PresenceState previous : {PresenceState::present,
                                             PresenceState::away}) {
            TEST_ASSERT_EQUAL_INT(
                static_cast<int>(PresencePanelAction::none),
                static_cast<int>(presence_panel_action(
                    previous, PresenceState::unknown, blank_on_panel)));
        }
    }
}

void test_no_transition_means_no_action()
{
    for (const PresenceState state : {PresenceState::unknown,
                                      PresenceState::present,
                                      PresenceState::away}) {
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(PresencePanelAction::none),
            static_cast<int>(presence_panel_action(state, state, true)));
    }
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_away_requires_the_full_debounce_duration);
    RUN_TEST(test_return_requires_its_own_shorter_duration);
    RUN_TEST(test_flickering_candidate_resets_the_debounce_timer);
    RUN_TEST(test_not_detected_saturated_and_error_never_trigger_away);
    RUN_TEST(
        test_unknown_light_sensor_status_leaves_state_unknown_immediately);
    RUN_TEST(test_unknown_to_present_is_not_a_return);
    RUN_TEST(test_away_to_present_restores_the_blanked_panel);
    RUN_TEST(test_unknown_to_present_restores_when_the_panel_is_blank);
    RUN_TEST(test_present_is_never_a_return_without_a_blank);
    RUN_TEST(test_any_transition_to_away_blanks_regardless_of_panel_contents);
    RUN_TEST(test_collapsing_to_unknown_touches_nothing);
    RUN_TEST(test_no_transition_means_no_action);
    return UNITY_END();
}
