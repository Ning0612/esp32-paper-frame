#include <cstring>

#include <unity.h>

#include "pf_runtime/diagnostics_event.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_runtime::DiagnosticCategory;
using pf_runtime::DiagnosticEvent;
using pf_runtime::DiagnosticsReadResult;
using pf_runtime::DiagnosticsRing;
using pf_runtime::DiagnosticSeverity;
using pf_runtime::diagnostics_ring_push;
using pf_runtime::diagnostics_ring_read_since;
using pf_runtime::kDiagnosticsRingCapacity;

void test_push_assigns_increasing_sequence_ids_starting_at_one()
{
    DiagnosticsRing ring{};
    const std::uint32_t first = diagnostics_ring_push(
        ring, DiagnosticCategory::queue, DiagnosticSeverity::info, "a", 10U);
    const std::uint32_t second = diagnostics_ring_push(
        ring, DiagnosticCategory::queue, DiagnosticSeverity::info, "b", 20U);

    TEST_ASSERT_EQUAL_UINT32(1U, first);
    TEST_ASSERT_EQUAL_UINT32(2U, second);
    TEST_ASSERT_EQUAL_UINT16(2U, ring.count);
}

void test_push_truncates_overlong_message()
{
    DiagnosticsRing ring{};
    char overlong[pf_runtime::kDiagnosticMessageCapacity + 16];
    std::memset(overlong, 'x', sizeof(overlong) - 1U);
    overlong[sizeof(overlong) - 1U] = '\0';

    diagnostics_ring_push(
        ring, DiagnosticCategory::storage, DiagnosticSeverity::error,
        overlong, 0U);

    const std::size_t message_length = std::strlen(ring.events[0].message);
    TEST_ASSERT_EQUAL_size_t(
        pf_runtime::kDiagnosticMessageCapacity - 1U, message_length);
}

void test_push_beyond_capacity_evicts_oldest()
{
    DiagnosticsRing ring{};
    for (std::size_t i = 0U; i < kDiagnosticsRingCapacity; ++i) {
        diagnostics_ring_push(
            ring, DiagnosticCategory::network, DiagnosticSeverity::info,
            "fill", static_cast<std::uint64_t>(i));
    }
    TEST_ASSERT_EQUAL_UINT16(
        static_cast<std::uint16_t>(kDiagnosticsRingCapacity), ring.count);

    // One more push should evict sequence_id 1 (the oldest), not grow count.
    const std::uint32_t overflow_id = diagnostics_ring_push(
        ring, DiagnosticCategory::network, DiagnosticSeverity::warning,
        "overflow", 999U);
    TEST_ASSERT_EQUAL_UINT16(
        static_cast<std::uint16_t>(kDiagnosticsRingCapacity), ring.count);

    DiagnosticEvent destination[kDiagnosticsRingCapacity]{};
    const DiagnosticsReadResult result =
        diagnostics_ring_read_since(ring, 0U, destination, kDiagnosticsRingCapacity);
    TEST_ASSERT_EQUAL_size_t(kDiagnosticsRingCapacity, result.count);
    TEST_ASSERT_EQUAL_UINT32(2U, destination[0].sequence_id);
    TEST_ASSERT_EQUAL_UINT32(
        overflow_id, destination[result.count - 1U].sequence_id);
}

void test_read_since_returns_only_newer_events_oldest_first()
{
    DiagnosticsRing ring{};
    diagnostics_ring_push(
        ring, DiagnosticCategory::lock, DiagnosticSeverity::warning, "1",
        1U);
    diagnostics_ring_push(
        ring, DiagnosticCategory::lock, DiagnosticSeverity::warning, "2",
        2U);
    diagnostics_ring_push(
        ring, DiagnosticCategory::lock, DiagnosticSeverity::warning, "3",
        3U);

    DiagnosticEvent destination[4]{};
    const DiagnosticsReadResult result =
        diagnostics_ring_read_since(ring, 1U, destination, 4U);

    TEST_ASSERT_EQUAL_size_t(2U, result.count);
    TEST_ASSERT_EQUAL_UINT32(2U, destination[0].sequence_id);
    TEST_ASSERT_EQUAL_UINT32(3U, destination[1].sequence_id);
    TEST_ASSERT_EQUAL_UINT32(3U, result.highest_sequence_id);
    TEST_ASSERT_FALSE(result.more_available);
}

void test_read_since_reports_more_available_when_destination_too_small()
{
    DiagnosticsRing ring{};
    diagnostics_ring_push(
        ring, DiagnosticCategory::auth, DiagnosticSeverity::info, "1", 1U);
    diagnostics_ring_push(
        ring, DiagnosticCategory::auth, DiagnosticSeverity::info, "2", 2U);
    diagnostics_ring_push(
        ring, DiagnosticCategory::auth, DiagnosticSeverity::info, "3", 3U);

    DiagnosticEvent destination[2]{};
    const DiagnosticsReadResult result =
        diagnostics_ring_read_since(ring, 0U, destination, 2U);

    TEST_ASSERT_EQUAL_size_t(2U, result.count);
    TEST_ASSERT_TRUE(result.more_available);
    // highest_sequence_id must reflect the last event actually copied so a
    // re-poll with it as the new "since" continues where this call left off,
    // not the newest event overall (which was skipped this round).
    TEST_ASSERT_EQUAL_UINT32(2U, result.highest_sequence_id);
}

void test_read_since_with_latest_id_returns_nothing()
{
    DiagnosticsRing ring{};
    const std::uint32_t latest = diagnostics_ring_push(
        ring, DiagnosticCategory::ota, DiagnosticSeverity::error, "x", 5U);

    DiagnosticEvent destination[4]{};
    const DiagnosticsReadResult result =
        diagnostics_ring_read_since(ring, latest, destination, 4U);

    TEST_ASSERT_EQUAL_size_t(0U, result.count);
    TEST_ASSERT_FALSE(result.more_available);
    TEST_ASSERT_EQUAL_UINT32(latest, result.highest_sequence_id);
}

void test_to_string_covers_every_category_and_severity()
{
    TEST_ASSERT_EQUAL_STRING(
        "network", pf_runtime::to_string(DiagnosticCategory::network));
    TEST_ASSERT_EQUAL_STRING(
        "display", pf_runtime::to_string(DiagnosticCategory::display));
    TEST_ASSERT_EQUAL_STRING(
        "storage", pf_runtime::to_string(DiagnosticCategory::storage));
    TEST_ASSERT_EQUAL_STRING(
        "queue", pf_runtime::to_string(DiagnosticCategory::queue));
    TEST_ASSERT_EQUAL_STRING(
        "lock", pf_runtime::to_string(DiagnosticCategory::lock));
    TEST_ASSERT_EQUAL_STRING(
        "reboot", pf_runtime::to_string(DiagnosticCategory::reboot));
    TEST_ASSERT_EQUAL_STRING(
        "ota", pf_runtime::to_string(DiagnosticCategory::ota));
    TEST_ASSERT_EQUAL_STRING(
        "auth", pf_runtime::to_string(DiagnosticCategory::auth));

    TEST_ASSERT_EQUAL_STRING(
        "info", pf_runtime::to_string(DiagnosticSeverity::info));
    TEST_ASSERT_EQUAL_STRING(
        "warning", pf_runtime::to_string(DiagnosticSeverity::warning));
    TEST_ASSERT_EQUAL_STRING(
        "error", pf_runtime::to_string(DiagnosticSeverity::error));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_push_assigns_increasing_sequence_ids_starting_at_one);
    RUN_TEST(test_push_truncates_overlong_message);
    RUN_TEST(test_push_beyond_capacity_evicts_oldest);
    RUN_TEST(test_read_since_returns_only_newer_events_oldest_first);
    RUN_TEST(test_read_since_reports_more_available_when_destination_too_small);
    RUN_TEST(test_read_since_with_latest_id_returns_nothing);
    RUN_TEST(test_to_string_covers_every_category_and_severity);
    return UNITY_END();
}
