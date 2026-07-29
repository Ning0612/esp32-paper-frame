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

void test_default_interval_and_hard_minimum_are_enforced()
{
    CarouselScheduler scheduler;
    TEST_ASSERT_EQUAL_UINT32(30U, scheduler.interval_minutes());

    TEST_ASSERT_FALSE(scheduler.configure(
        CarouselConfig{4U, CarouselMode::sequential, 1U}));
    TEST_ASSERT_EQUAL_UINT32(30U, scheduler.interval_minutes());

    TEST_ASSERT_TRUE(scheduler.configure(
        CarouselConfig{5U, CarouselMode::random, 7U}));
    TEST_ASSERT_EQUAL_UINT32(5U, scheduler.interval_minutes());
    TEST_ASSERT_EQUAL(
        static_cast<int>(CarouselMode::random),
        static_cast<int>(scheduler.mode()));
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
        CarouselConfig{5U, CarouselMode::random, 0x12345678U});
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
        CarouselConfig{5U, CarouselMode::random, 0x87654321U});
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
        CarouselConfig{5U, CarouselMode::sequential, 1U});
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
                100U + (5U * kMinuteMs) - 1U,
                items,
                2U)
                .kind));

    const CarouselDecision retry =
        scheduler.poll(100U + (5U * kMinuteMs), items, 2U);
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
    RUN_TEST(test_default_interval_and_hard_minimum_are_enforced);
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
    return UNITY_END();
}
