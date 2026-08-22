#include <cstdint>
#include <vector>

#include <unity.h>

#include "pf_carousel/scheduler.hpp"
#include "pf_carousel/welcome_frame.hpp"
#include "pf_display/packed_framebuffer.hpp"

namespace {

using pf_carousel::CarouselConfig;
using pf_carousel::CarouselDecision;
using pf_carousel::CarouselItem;
using pf_carousel::CarouselMode;
using pf_carousel::CarouselScheduler;
using pf_carousel::DecisionKind;
using pf_carousel::DecisionReason;

constexpr std::uint64_t kMinuteMs = 60U * 1000U;

CarouselItem item(
    const std::uint32_t id,
    const bool shown_once = true,
    const bool enabled = true,
    const bool valid = true)
{
    return {id, enabled, valid, shown_once};
}

void complete_success(
    CarouselScheduler& scheduler,
    const CarouselDecision decision,
    const std::uint64_t completed_at_ms)
{
    TEST_ASSERT_TRUE(
        scheduler.complete(decision, true, completed_at_ms));
}

void test_default_interval_and_hard_bounds_are_enforced()
{
    CarouselScheduler scheduler;
    TEST_ASSERT_EQUAL_UINT32(30U, scheduler.interval_minutes());

    TEST_ASSERT_FALSE(scheduler.configure(
        CarouselConfig{9U, CarouselMode::sequential, 1U}));
    TEST_ASSERT_EQUAL_UINT32(30U, scheduler.interval_minutes());

    TEST_ASSERT_TRUE(scheduler.configure(
        CarouselConfig{1440U, CarouselMode::random, 7U}));
    TEST_ASSERT_EQUAL_UINT32(1440U, scheduler.interval_minutes());
    TEST_ASSERT_EQUAL(
        static_cast<int>(CarouselMode::random),
        static_cast<int>(scheduler.mode()));

    TEST_ASSERT_FALSE(scheduler.configure(
        CarouselConfig{1441U, CarouselMode::sequential, 1U}));
    TEST_ASSERT_EQUAL_UINT32(1440U, scheduler.interval_minutes());
}

void test_empty_library_displays_welcome_once()
{
    CarouselScheduler scheduler;

    const CarouselDecision first = scheduler.poll(0U, nullptr, 0U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_welcome),
        static_cast<int>(first.kind));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionReason::library_empty),
        static_cast<int>(first.reason));

    complete_success(scheduler, first, 25U * 1000U);
    TEST_ASSERT_EQUAL_UINT64(
        (30U * kMinuteMs) + (25U * 1000U),
        scheduler.next_due_ms());

    const CarouselDecision later =
        scheduler.poll(scheduler.next_due_ms(), nullptr, 0U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(later.kind));
}

void test_sequential_rotation_waits_a_full_interval_after_completion()
{
    CarouselScheduler scheduler;
    const CarouselItem items[] = {item(10U), item(20U), item(30U)};

    const CarouselDecision first = scheduler.poll(0U, items, 3U);
    TEST_ASSERT_EQUAL_UINT32(10U, first.image_id);
    complete_success(scheduler, first, 20U * 1000U);

    const std::uint64_t due = (30U * kMinuteMs) + (20U * 1000U);
    TEST_ASSERT_EQUAL_UINT64(due, scheduler.next_due_ms());
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(scheduler.poll(due - 1U, items, 3U).kind));

    const CarouselDecision second = scheduler.poll(due, items, 3U);
    TEST_ASSERT_EQUAL_UINT32(20U, second.image_id);
}

void test_unseen_images_are_selected_before_already_shown_images()
{
    CarouselScheduler scheduler;
    CarouselItem initial[] = {item(1U), item(2U)};
    const CarouselDecision first = scheduler.poll(0U, initial, 2U);
    TEST_ASSERT_EQUAL_UINT32(1U, first.image_id);
    complete_success(scheduler, first, 1U);

    CarouselItem changed[] = {
        item(1U, true),
        item(2U, true),
        item(3U, false),
    };
    const CarouselDecision next =
        scheduler.poll(scheduler.next_due_ms(), changed, 3U);
    TEST_ASSERT_EQUAL_UINT32(3U, next.image_id);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionReason::pending_first_display),
        static_cast<int>(next.reason));
}

void test_manual_selection_waits_for_safety_deadline_then_holds_cycle()
{
    CarouselScheduler scheduler;
    const CarouselItem items[] = {item(4U), item(5U), item(6U)};
    const CarouselDecision first = scheduler.poll(0U, items, 3U);
    complete_success(scheduler, first, 10U);

    TEST_ASSERT_TRUE(scheduler.request_manual(6U, items, 3U));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(
            scheduler.poll(scheduler.next_due_ms() - 1U, items, 3U).kind));

    const CarouselDecision manual =
        scheduler.poll(scheduler.next_due_ms(), items, 3U);
    TEST_ASSERT_EQUAL_UINT32(6U, manual.image_id);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionReason::manual),
        static_cast<int>(manual.reason));
    complete_success(scheduler, manual, 50U);

    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(
            scheduler.poll(
                50U + (30U * kMinuteMs) - 1U,
                items,
                3U)
                .kind));
}

void test_disabled_or_invalid_items_are_never_selected()
{
    CarouselScheduler scheduler;
    const CarouselItem items[] = {
        item(1U, false, false, true),
        item(2U, false, true, false),
    };

    TEST_ASSERT_FALSE(scheduler.request_manual(1U, items, 2U));
    TEST_ASSERT_FALSE(scheduler.request_manual(2U, items, 2U));
    const CarouselDecision decision = scheduler.poll(0U, items, 2U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_welcome),
        static_cast<int>(decision.kind));
}

void test_random_mode_avoids_immediate_repeat_when_an_alternative_exists()
{
    CarouselScheduler scheduler(
        CarouselConfig{10U, CarouselMode::random, 0x12345678U});
    const CarouselItem items[] = {item(11U), item(22U), item(33U)};

    const CarouselDecision first = scheduler.poll(0U, items, 3U);
    complete_success(scheduler, first, 1U);
    const CarouselDecision second =
        scheduler.poll(scheduler.next_due_ms(), items, 3U);

    TEST_ASSERT_NOT_EQUAL(first.image_id, second.image_id);
}

void test_random_mode_with_two_items_selects_the_only_alternative()
{
    CarouselScheduler scheduler(
        CarouselConfig{10U, CarouselMode::random, 0x87654321U});
    const CarouselItem items[] = {item(71U), item(72U)};

    const CarouselDecision first = scheduler.poll(0U, items, 2U);
    complete_success(scheduler, first, 1U);
    const CarouselDecision second =
        scheduler.poll(scheduler.next_due_ms(), items, 2U);

    TEST_ASSERT_NOT_EQUAL(first.image_id, second.image_id);
}

void test_failed_manual_display_retries_only_after_full_interval()
{
    CarouselScheduler scheduler(
        CarouselConfig{10U, CarouselMode::sequential, 1U});
    const CarouselItem items[] = {item(1U), item(2U)};
    const CarouselDecision first = scheduler.poll(0U, items, 2U);
    complete_success(scheduler, first, 1U);
    TEST_ASSERT_TRUE(scheduler.request_manual(2U, items, 2U));

    const CarouselDecision manual =
        scheduler.poll(scheduler.next_due_ms(), items, 2U);
    TEST_ASSERT_TRUE(scheduler.complete(manual, false, 100U));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(
            scheduler.poll(
                100U + (10U * kMinuteMs) - 1U,
                items,
                2U)
                .kind));

    const CarouselDecision retry =
        scheduler.poll(100U + (10U * kMinuteMs), items, 2U);
    TEST_ASSERT_EQUAL_UINT32(2U, retry.image_id);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionReason::manual),
        static_cast<int>(retry.reason));
}

void test_abandoned_submission_can_retry_without_counting_as_a_refresh()
{
    CarouselScheduler scheduler;
    const CarouselItem items[] = {item(9U)};
    const CarouselDecision decision = scheduler.poll(0U, items, 1U);

    TEST_ASSERT_TRUE(scheduler.abandon(decision, 1000U));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(scheduler.poll(999U, items, 1U).kind));
    TEST_ASSERT_EQUAL_UINT32(
        9U,
        scheduler.poll(1000U, items, 1U).image_id);
}

void test_mutated_or_stale_decision_cannot_complete_active_work()
{
    CarouselScheduler scheduler;
    const CarouselItem items[] = {item(41U), item(42U)};
    const CarouselDecision active = scheduler.poll(0U, items, 2U);

    CarouselDecision mutated = active;
    mutated.reason = DecisionReason::manual;
    TEST_ASSERT_FALSE(scheduler.complete(mutated, true, 1U));
    TEST_ASSERT_TRUE(scheduler.in_flight());

    mutated = active;
    ++mutated.not_before_ms;
    TEST_ASSERT_FALSE(scheduler.abandon(mutated, 2U));
    TEST_ASSERT_TRUE(scheduler.in_flight());
    complete_success(scheduler, active, 3U);
}

void test_each_new_scheduler_displays_welcome_once_per_boot()
{
    CarouselScheduler first_boot;
    const CarouselDecision first =
        first_boot.poll(0U, nullptr, 0U);
    complete_success(first_boot, first, 1U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(
            first_boot.poll(
                first_boot.next_due_ms(),
                nullptr,
                0U)
                .kind));

    CarouselScheduler second_boot;
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_welcome),
        static_cast<int>(
            second_boot.poll(0U, nullptr, 0U).kind));
}

void test_welcome_frame_has_stable_status_and_logo_geometry()
{
    std::vector<std::uint8_t> frame(pf_display::kFullFramebufferBytes);
    TEST_ASSERT_TRUE(
        pf_carousel::render_welcome_frame(frame.data(), frame.size()));

    pf_display::PackedFramebufferView view{
        frame.data(),
        frame.size(),
        pf_display::kPanelWidth,
        pf_display::kPanelHeight,
    };
    pf_display::Color color = pf_display::Color::red;
    TEST_ASSERT_TRUE(view.get_pixel(0U, 0U, color));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_display::Color::blue),
        static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(100U, 100U, color));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_display::Color::white),
        static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(250U, 160U, color));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_display::Color::black),
        static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(328U, 220U, color));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_display::Color::black),
        static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(292U, 376U, color));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_display::Color::red),
        static_cast<int>(color));
}

void test_welcome_frame_rejects_wrong_length_without_writing()
{
    std::uint8_t byte = 0xA5U;
    TEST_ASSERT_FALSE(pf_carousel::render_welcome_frame(&byte, 1U));
    TEST_ASSERT_EQUAL_HEX8(0xA5U, byte);
    TEST_ASSERT_FALSE(
        pf_carousel::render_welcome_frame(
            nullptr,
            pf_display::kFullFramebufferBytes));
}

void test_blank_frame_is_entirely_white()
{
    std::vector<std::uint8_t> frame(pf_display::kFullFramebufferBytes);
    TEST_ASSERT_TRUE(
        pf_carousel::render_blank_frame(frame.data(), frame.size()));

    const std::uint8_t white = pf_display::native_code(pf_display::Color::white);
    const std::uint8_t expected =
        static_cast<std::uint8_t>((white << 4U) | white);
    for (const std::uint8_t byte : frame) {
        TEST_ASSERT_EQUAL_HEX8(expected, byte);
    }
}

void test_blank_frame_rejects_wrong_length_without_writing()
{
    std::uint8_t byte = 0xA5U;
    TEST_ASSERT_FALSE(pf_carousel::render_blank_frame(&byte, 1U));
    TEST_ASSERT_EQUAL_HEX8(0xA5U, byte);
    TEST_ASSERT_FALSE(
        pf_carousel::render_blank_frame(
            nullptr,
            pf_display::kFullFramebufferBytes));
}

void test_force_immediate_makes_the_next_poll_issue_a_decision()
{
    CarouselItem items[1] = {{7U, true, true, true}};
    CarouselScheduler scheduler;
    const CarouselDecision first = scheduler.poll(0U, items, 1U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_image),
        static_cast<int>(first.kind));
    TEST_ASSERT_TRUE(scheduler.complete(first, true, 0U));

    // Still well within the normal interval: poll() waits.
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(scheduler.poll(1U, items, 1U).kind));

    TEST_ASSERT_TRUE(scheduler.force_immediate(1U));
    TEST_ASSERT_EQUAL_UINT64(1U, scheduler.next_due_ms());
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_image),
        static_cast<int>(scheduler.poll(1U, items, 1U).kind));
}

void test_force_immediate_is_refused_while_a_decision_is_in_flight()
{
    CarouselItem items[1] = {{7U, true, true, true}};
    CarouselScheduler scheduler;
    const CarouselDecision decision = scheduler.poll(0U, items, 1U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_image),
        static_cast<int>(decision.kind));
    TEST_ASSERT_TRUE(scheduler.in_flight());

    const std::uint64_t due_before = scheduler.next_due_ms();
    TEST_ASSERT_FALSE(scheduler.force_immediate(999U));
    TEST_ASSERT_EQUAL_UINT64(due_before, scheduler.next_due_ms());

    TEST_ASSERT_TRUE(scheduler.complete(decision, true, 100U));
    TEST_ASSERT_TRUE(scheduler.force_immediate(200U));
    TEST_ASSERT_EQUAL_UINT64(200U, scheduler.next_due_ms());
}

// Drives an empty-library scheduler until the welcome frame is on the
// panel and poll() has settled into its park-forever state.
CarouselScheduler welcome_displayed_scheduler()
{
    CarouselScheduler scheduler;
    const CarouselDecision welcome = scheduler.poll(0U, nullptr, 0U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_welcome),
        static_cast<int>(welcome.kind));
    complete_success(scheduler, welcome, 0U);
    return scheduler;
}

// Regression guard for the behaviour the redraw hook must not break: with
// an empty library the welcome frame is drawn once and then never again on
// its own, however far the clock runs.
void test_welcome_is_shown_once_while_the_library_stays_empty()
{
    CarouselScheduler scheduler = welcome_displayed_scheduler();

    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(scheduler.poll(1U, nullptr, 0U).kind));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(
            scheduler.poll(600U * kMinuteMs, nullptr, 0U).kind));
}

// The device IP is drawn on the welcome frame's status bar, and at boot it
// is not known yet. Without this the panel would never show the address.
void test_welcome_redraw_reissues_the_welcome_frame()
{
    CarouselScheduler scheduler = welcome_displayed_scheduler();
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(scheduler.poll(1U, nullptr, 0U).kind));

    TEST_ASSERT_TRUE(scheduler.request_welcome_redraw(2U));
    const CarouselDecision redraw = scheduler.poll(2U, nullptr, 0U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_welcome),
        static_cast<int>(redraw.kind));

    // Once the replacement is on the panel the scheduler parks again, so a
    // single address change costs exactly one refresh.
    complete_success(scheduler, redraw, 3U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(
            scheduler.poll(600U * kMinuteMs, nullptr, 0U).kind));
}

// An abandoned redraw must stay pending: dropping it would leave the stale
// address on the panel with nothing left to trigger another attempt.
void test_welcome_redraw_survives_an_abandoned_decision()
{
    CarouselScheduler scheduler = welcome_displayed_scheduler();
    TEST_ASSERT_TRUE(scheduler.request_welcome_redraw(2U));

    const CarouselDecision redraw = scheduler.poll(2U, nullptr, 0U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_welcome),
        static_cast<int>(redraw.kind));
    TEST_ASSERT_TRUE(scheduler.abandon(redraw, 5U));

    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_welcome),
        static_cast<int>(scheduler.poll(5U, nullptr, 0U).kind));
}

void test_welcome_redraw_is_refused_when_no_welcome_is_displayed()
{
    // Nothing displayed yet.
    CarouselScheduler fresh;
    TEST_ASSERT_FALSE(fresh.request_welcome_redraw(1U));

    // A real image is on the panel: its status bar is redrawn at every
    // rotation, so there is nothing for this to fix.
    CarouselItem items[1] = {item(7U)};
    CarouselScheduler scheduler;
    const CarouselDecision image = scheduler.poll(0U, items, 1U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_image),
        static_cast<int>(image.kind));
    complete_success(scheduler, image, 0U);
    const std::uint64_t due_before = scheduler.next_due_ms();
    TEST_ASSERT_FALSE(scheduler.request_welcome_redraw(1U));
    TEST_ASSERT_EQUAL_UINT64(due_before, scheduler.next_due_ms());
}

// Same in-flight guard as force_immediate(): complete()/abandon() rely on
// it, so the caller has to retry after the in-flight decision resolves.
void test_welcome_redraw_is_refused_while_a_decision_is_in_flight()
{
    CarouselScheduler scheduler = welcome_displayed_scheduler();
    TEST_ASSERT_TRUE(scheduler.request_welcome_redraw(2U));
    const CarouselDecision redraw = scheduler.poll(2U, nullptr, 0U);
    TEST_ASSERT_TRUE(scheduler.in_flight());

    const std::uint64_t due_before = scheduler.next_due_ms();
    TEST_ASSERT_FALSE(scheduler.request_welcome_redraw(99U));
    TEST_ASSERT_EQUAL_UINT64(due_before, scheduler.next_due_ms());

    complete_success(scheduler, redraw, 100U);
    TEST_ASSERT_TRUE(scheduler.request_welcome_redraw(200U));
    TEST_ASSERT_EQUAL_UINT64(200U, scheduler.next_due_ms());
}

// A real image appearing wins over a pending redraw, and the pending flag
// must not then resurrect the welcome frame over that image.
void test_welcome_redraw_does_not_override_a_newly_available_image()
{
    CarouselScheduler scheduler = welcome_displayed_scheduler();
    TEST_ASSERT_TRUE(scheduler.request_welcome_redraw(2U));

    CarouselItem items[1] = {item(7U, false)};
    const CarouselDecision image = scheduler.poll(2U, items, 1U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::display_image),
        static_cast<int>(image.kind));
    complete_success(scheduler, image, 3U);

    TEST_ASSERT_EQUAL(
        static_cast<int>(DecisionKind::wait),
        static_cast<int>(scheduler.poll(4U, items, 1U).kind));
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_interval_and_hard_bounds_are_enforced);
    RUN_TEST(test_empty_library_displays_welcome_once);
    RUN_TEST(test_sequential_rotation_waits_a_full_interval_after_completion);
    RUN_TEST(test_unseen_images_are_selected_before_already_shown_images);
    RUN_TEST(test_manual_selection_waits_for_safety_deadline_then_holds_cycle);
    RUN_TEST(test_disabled_or_invalid_items_are_never_selected);
    RUN_TEST(test_random_mode_avoids_immediate_repeat_when_an_alternative_exists);
    RUN_TEST(test_random_mode_with_two_items_selects_the_only_alternative);
    RUN_TEST(test_failed_manual_display_retries_only_after_full_interval);
    RUN_TEST(test_abandoned_submission_can_retry_without_counting_as_a_refresh);
    RUN_TEST(test_mutated_or_stale_decision_cannot_complete_active_work);
    RUN_TEST(test_each_new_scheduler_displays_welcome_once_per_boot);
    RUN_TEST(test_welcome_frame_has_stable_status_and_logo_geometry);
    RUN_TEST(test_welcome_frame_rejects_wrong_length_without_writing);
    RUN_TEST(test_blank_frame_is_entirely_white);
    RUN_TEST(test_blank_frame_rejects_wrong_length_without_writing);
    RUN_TEST(test_force_immediate_makes_the_next_poll_issue_a_decision);
    RUN_TEST(
        test_force_immediate_is_refused_while_a_decision_is_in_flight);
    RUN_TEST(test_welcome_is_shown_once_while_the_library_stays_empty);
    RUN_TEST(test_welcome_redraw_reissues_the_welcome_frame);
    RUN_TEST(test_welcome_redraw_survives_an_abandoned_decision);
    RUN_TEST(test_welcome_redraw_is_refused_when_no_welcome_is_displayed);
    RUN_TEST(
        test_welcome_redraw_is_refused_while_a_decision_is_in_flight);
    RUN_TEST(test_welcome_redraw_does_not_override_a_newly_available_image);
    return UNITY_END();
}
