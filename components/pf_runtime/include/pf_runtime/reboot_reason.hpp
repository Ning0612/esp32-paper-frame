#pragma once

namespace pf_runtime {

enum class RebootReason {
    unknown,
    power_on,
    external_reset,
    software_reset,
    panic_or_watchdog,
    deep_sleep_wake,
    brownout,
    other,
};

constexpr const char* to_string(const RebootReason reason)
{
    switch (reason) {
        case RebootReason::unknown:
            return "unknown";
        case RebootReason::power_on:
            return "power_on";
        case RebootReason::external_reset:
            return "external_reset";
        case RebootReason::software_reset:
            return "software_reset";
        case RebootReason::panic_or_watchdog:
            return "panic_or_watchdog";
        case RebootReason::deep_sleep_wake:
            return "deep_sleep_wake";
        case RebootReason::brownout:
            return "brownout";
        case RebootReason::other:
            return "other";
    }
    return "unknown";
}

// Classifies the raw esp_reset_reason_t value (passed as plain int so this
// header never includes esp_system.h and stays host-buildable). The mapping
// below was read directly from the pinned ESP-IDF's
// components/esp_system/include/esp_system.h, which declares
// esp_reset_reason_t as a plain C enum with no explicit values, so the
// integers are its declaration order (ESP_RST_UNKNOWN=0 ... ESP_RST_CPU_LOCKUP
// =15). If the pinned ESP-IDF version ever changes, re-verify this mapping
// against that header before trusting it.
constexpr RebootReason classify_reset_reason(const int raw_esp_reset_reason)
{
    switch (raw_esp_reset_reason) {
        case 0:  // ESP_RST_UNKNOWN
            return RebootReason::unknown;
        case 1:  // ESP_RST_POWERON
            return RebootReason::power_on;
        case 2:  // ESP_RST_EXT
            return RebootReason::external_reset;
        case 3:  // ESP_RST_SW
            return RebootReason::software_reset;
        case 4:   // ESP_RST_PANIC
        case 5:   // ESP_RST_INT_WDT
        case 6:   // ESP_RST_TASK_WDT
        case 7:   // ESP_RST_WDT
        case 15:  // ESP_RST_CPU_LOCKUP
            return RebootReason::panic_or_watchdog;
        case 8:  // ESP_RST_DEEPSLEEP
            return RebootReason::deep_sleep_wake;
        case 9:  // ESP_RST_BROWNOUT
            return RebootReason::brownout;
        case 10:  // ESP_RST_SDIO
        case 11:  // ESP_RST_USB
        case 12:  // ESP_RST_JTAG
        case 13:  // ESP_RST_EFUSE
        case 14:  // ESP_RST_PWR_GLITCH
            return RebootReason::other;
        default:
            return RebootReason::unknown;
    }
}

}  // namespace pf_runtime
