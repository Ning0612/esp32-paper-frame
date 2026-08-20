#include <unity.h>

#include "pf_runtime/reboot_policy.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_idle_display_reboots_immediately()
{
    TEST_ASSERT_FALSE(pf_runtime::should_defer_reboot(false, 0U));
    // Having deferred earlier must not keep deferring once the panel is
    // free again: the refresh this reboot was waiting for has finished.
    TEST_ASSERT_FALSE(pf_runtime::should_defer_reboot(false, 5U));
}

void test_busy_display_defers_until_the_cap()
{
    TEST_ASSERT_TRUE(pf_runtime::should_defer_reboot(true, 0U));
    TEST_ASSERT_TRUE(
        pf_runtime::should_defer_reboot(
            true, pf_runtime::kMaxRebootDeferrals - 1U));
}

void test_stuck_panel_cannot_block_the_reboot_forever()
{
    // The boundary that matters: a panel that never reports completion
    // must not strand a device on firmware it has already replaced.
    TEST_ASSERT_FALSE(
        pf_runtime::should_defer_reboot(
            true, pf_runtime::kMaxRebootDeferrals));
    TEST_ASSERT_FALSE(
        pf_runtime::should_defer_reboot(
            true, pf_runtime::kMaxRebootDeferrals + 1U));
}

void test_cap_outlasts_a_full_panel_refresh()
{
    // A full E6 refresh measured 31.2 s on hardware; the deferral budget
    // has to exceed that or the wait is pointless. Each deferral is one
    // kRebootDelayUs interval.
    constexpr std::uint64_t kBudgetUs =
        static_cast<std::uint64_t>(pf_runtime::kMaxRebootDeferrals) *
        pf_runtime::kRebootDelayUs;
    TEST_ASSERT_TRUE(kBudgetUs > 35'000'000ULL);
}

void test_custom_cap_is_honored()
{
    TEST_ASSERT_TRUE(pf_runtime::should_defer_reboot(true, 1U, 2U));
    TEST_ASSERT_FALSE(pf_runtime::should_defer_reboot(true, 2U, 2U));
    // A zero budget means "never defer", not "defer once".
    TEST_ASSERT_FALSE(pf_runtime::should_defer_reboot(true, 0U, 0U));
}

}  // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_display_reboots_immediately);
    RUN_TEST(test_busy_display_defers_until_the_cap);
    RUN_TEST(test_stuck_panel_cannot_block_the_reboot_forever);
    RUN_TEST(test_cap_outlasts_a_full_panel_refresh);
    RUN_TEST(test_custom_cap_is_honored);
    return UNITY_END();
}
