#include <unity.h>

#include "pf_runtime/reboot_reason.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_runtime::classify_reset_reason;
using pf_runtime::RebootReason;

void test_known_esp_rst_values_map_to_expected_reasons()
{
    // Values below are esp_reset_reason_t's implicit declaration-order
    // integers from the pinned ESP-IDF's
    // components/esp_system/include/esp_system.h (plain C enum, no explicit
    // values assigned). Re-verify against that header if the pinned IDF
    // version ever changes.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::unknown),
        static_cast<int>(classify_reset_reason(0)));   // ESP_RST_UNKNOWN
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::power_on),
        static_cast<int>(classify_reset_reason(1)));   // ESP_RST_POWERON
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::external_reset),
        static_cast<int>(classify_reset_reason(2)));   // ESP_RST_EXT
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::software_reset),
        static_cast<int>(classify_reset_reason(3)));   // ESP_RST_SW
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::panic_or_watchdog),
        static_cast<int>(classify_reset_reason(4)));   // ESP_RST_PANIC
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::panic_or_watchdog),
        static_cast<int>(classify_reset_reason(5)));   // ESP_RST_INT_WDT
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::panic_or_watchdog),
        static_cast<int>(classify_reset_reason(6)));   // ESP_RST_TASK_WDT
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::panic_or_watchdog),
        static_cast<int>(classify_reset_reason(7)));   // ESP_RST_WDT
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::deep_sleep_wake),
        static_cast<int>(classify_reset_reason(8)));   // ESP_RST_DEEPSLEEP
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::brownout),
        static_cast<int>(classify_reset_reason(9)));   // ESP_RST_BROWNOUT
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::other),
        static_cast<int>(classify_reset_reason(10)));  // ESP_RST_SDIO
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::other),
        static_cast<int>(classify_reset_reason(11)));  // ESP_RST_USB
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::other),
        static_cast<int>(classify_reset_reason(12)));  // ESP_RST_JTAG
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::other),
        static_cast<int>(classify_reset_reason(13)));  // ESP_RST_EFUSE
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::other),
        static_cast<int>(classify_reset_reason(14)));  // ESP_RST_PWR_GLITCH
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::panic_or_watchdog),
        static_cast<int>(classify_reset_reason(15)));  // ESP_RST_CPU_LOCKUP
}

void test_out_of_range_values_map_to_unknown_not_ub()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::unknown),
        static_cast<int>(classify_reset_reason(16)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::unknown),
        static_cast<int>(classify_reset_reason(-1)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RebootReason::unknown),
        static_cast<int>(classify_reset_reason(1000)));
}

void test_to_string_covers_every_reason()
{
    TEST_ASSERT_EQUAL_STRING(
        "unknown", pf_runtime::to_string(RebootReason::unknown));
    TEST_ASSERT_EQUAL_STRING(
        "power_on", pf_runtime::to_string(RebootReason::power_on));
    TEST_ASSERT_EQUAL_STRING(
        "external_reset", pf_runtime::to_string(RebootReason::external_reset));
    TEST_ASSERT_EQUAL_STRING(
        "software_reset", pf_runtime::to_string(RebootReason::software_reset));
    TEST_ASSERT_EQUAL_STRING(
        "panic_or_watchdog",
        pf_runtime::to_string(RebootReason::panic_or_watchdog));
    TEST_ASSERT_EQUAL_STRING(
        "deep_sleep_wake",
        pf_runtime::to_string(RebootReason::deep_sleep_wake));
    TEST_ASSERT_EQUAL_STRING(
        "brownout", pf_runtime::to_string(RebootReason::brownout));
    TEST_ASSERT_EQUAL_STRING(
        "other", pf_runtime::to_string(RebootReason::other));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_esp_rst_values_map_to_expected_reasons);
    RUN_TEST(test_out_of_range_values_map_to_unknown_not_ub);
    RUN_TEST(test_to_string_covers_every_reason);
    return UNITY_END();
}
