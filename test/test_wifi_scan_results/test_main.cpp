#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pf_network/scan_results.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

pf_network::RawScanRecord record(
    const char* ssid,
    const std::int8_t rssi,
    const pf_network::WifiSecurity security)
{
    pf_network::RawScanRecord result{};
    std::strncpy(result.ssid, ssid, sizeof(result.ssid) - 1U);
    result.rssi = rssi;
    result.security = security;
    return result;
}

void test_scan_filters_empty_deduplicates_and_sorts_by_signal()
{
    const pf_network::RawScanRecord raw[] = {
        record("Workshop", -70, pf_network::WifiSecurity::wpa2),
        record("", -10, pf_network::WifiSecurity::open),
        record("Cafe", -55, pf_network::WifiSecurity::wpa3),
        record("Workshop", -42, pf_network::WifiSecurity::wpa2),
        record("Guest", -80, pf_network::WifiSecurity::open),
    };
    pf_network::ScanResult output[4]{};

    const std::size_t count =
        pf_network::normalize_scan_results(
            raw,
            sizeof(raw) / sizeof(raw[0]),
            output,
            sizeof(output) / sizeof(output[0]));

    TEST_ASSERT_EQUAL_UINT32(3U, count);
    TEST_ASSERT_EQUAL_STRING("Workshop", output[0].ssid);
    TEST_ASSERT_EQUAL_INT8(-42, output[0].rssi);
    TEST_ASSERT_EQUAL_STRING("Cafe", output[1].ssid);
    TEST_ASSERT_EQUAL_STRING("Guest", output[2].ssid);
}

void test_invalid_utf8_and_unterminated_ssids_are_filtered()
{
    pf_network::RawScanRecord raw[4]{};
    raw[0] = record("Valid", -30, pf_network::WifiSecurity::wpa2);
    raw[1].ssid[0] = static_cast<char>(0xC0);
    raw[1].ssid[1] = static_cast<char>(0xAF);
    raw[1].ssid[2] = '\0';
    std::memset(raw[2].ssid, 'x', sizeof(raw[2].ssid));
    raw[3] = record("Bad\nName", -20, pf_network::WifiSecurity::open);

    pf_network::ScanResult output[3]{};
    const std::size_t count =
        pf_network::normalize_scan_results(raw, 4U, output, 3U);

    TEST_ASSERT_EQUAL_UINT32(1U, count);
    TEST_ASSERT_EQUAL_STRING("Valid", output[0].ssid);
}

void test_result_capacity_keeps_strongest_unique_networks()
{
    const pf_network::RawScanRecord raw[] = {
        record("Weak", -90, pf_network::WifiSecurity::open),
        record("Strong", -20, pf_network::WifiSecurity::wpa2),
        record("Medium", -50, pf_network::WifiSecurity::wpa),
    };
    pf_network::ScanResult output[2]{};

    const std::size_t count =
        pf_network::normalize_scan_results(raw, 3U, output, 2U);

    TEST_ASSERT_EQUAL_UINT32(2U, count);
    TEST_ASSERT_EQUAL_STRING("Strong", output[0].ssid);
    TEST_ASSERT_EQUAL_STRING("Medium", output[1].ssid);
}

void test_scan_request_coalesces_pending_and_active_work()
{
    TEST_ASSERT_TRUE(
        pf_network::should_enqueue_scan_request(false, false));
    TEST_ASSERT_FALSE(
        pf_network::should_enqueue_scan_request(true, false));
    TEST_ASSERT_FALSE(
        pf_network::should_enqueue_scan_request(false, true));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_scan_filters_empty_deduplicates_and_sorts_by_signal);
    RUN_TEST(test_invalid_utf8_and_unterminated_ssids_are_filtered);
    RUN_TEST(test_result_capacity_keeps_strongest_unique_networks);
    RUN_TEST(test_scan_request_coalesces_pending_and_active_work);
    return UNITY_END();
}
