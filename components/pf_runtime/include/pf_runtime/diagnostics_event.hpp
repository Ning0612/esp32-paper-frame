#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pf_runtime {

enum class DiagnosticCategory : std::uint8_t {
    network,
    display,
    storage,
    queue,
    lock,
    reboot,
    ota,
    auth,
};

enum class DiagnosticSeverity : std::uint8_t {
    info,
    warning,
    error,
};

constexpr std::size_t kDiagnosticMessageCapacity = 64U;
constexpr std::size_t kDiagnosticsRingCapacity = 32U;

struct DiagnosticEvent {
    std::uint32_t sequence_id = 0U;
    std::uint64_t uptime_ms = 0U;
    DiagnosticCategory category = DiagnosticCategory::queue;
    DiagnosticSeverity severity = DiagnosticSeverity::info;
    char message[kDiagnosticMessageCapacity] = {};
};

static_assert(std::is_trivially_copyable_v<DiagnosticEvent>);

struct DiagnosticsRing {
    DiagnosticEvent events[kDiagnosticsRingCapacity]{};
    // 0 is reserved to mean "no event assigned yet" (mirrors
    // RuntimeCoordinator::allocate_request_id's sentinel convention), so the
    // first real event gets sequence_id 1.
    std::uint32_t next_sequence_id = 1U;
    std::uint16_t head = 0U;
    std::uint16_t count = 0U;
};

static_assert(std::is_trivially_copyable_v<DiagnosticsRing>);

// Pushes a new event, evicting the oldest one once the ring is full. Returns
// the assigned sequence_id. Truncates message at kDiagnosticMessageCapacity -
// 1; callers are expected to pass short, secret-free, caller-built ASCII
// strings (never raw URLs, headers, or credentials).
constexpr std::uint32_t diagnostics_ring_push(
    DiagnosticsRing& ring,
    const DiagnosticCategory category,
    const DiagnosticSeverity severity,
    const char* const message,
    const std::uint64_t uptime_ms)
{
    std::uint32_t sequence_id = ring.next_sequence_id++;
    if (ring.next_sequence_id == 0U) {
        // Skip the reserved sentinel on wraparound instead of silently
        // reusing it for a real event.
        ring.next_sequence_id = 1U;
    }

    const std::uint16_t write_index =
        static_cast<std::uint16_t>(
            (ring.head + ring.count) % kDiagnosticsRingCapacity);

    DiagnosticEvent& slot =
        ring.count < kDiagnosticsRingCapacity
            ? ring.events[write_index]
            : ring.events[ring.head];

    slot.sequence_id = sequence_id;
    slot.uptime_ms = uptime_ms;
    slot.category = category;
    slot.severity = severity;
    std::size_t index = 0U;
    if (message != nullptr) {
        while (index < kDiagnosticMessageCapacity - 1U &&
               message[index] != '\0') {
            slot.message[index] = message[index];
            ++index;
        }
    }
    slot.message[index] = '\0';

    if (ring.count < kDiagnosticsRingCapacity) {
        ++ring.count;
    } else {
        // Ring was already full: the write above overwrote the oldest slot
        // in place, so the logical head advances by one.
        ring.head = static_cast<std::uint16_t>(
            (ring.head + 1U) % kDiagnosticsRingCapacity);
    }

    return sequence_id;
}

struct DiagnosticsReadResult {
    std::size_t count = 0U;
    std::uint32_t highest_sequence_id = 0U;
    bool more_available = false;
};

// Copies events with sequence_id > since_sequence_id into destination,
// oldest first, up to destination_capacity entries. If more matching events
// exist than destination_capacity allows, more_available is set and the
// caller should re-poll with the returned highest_sequence_id as the new
// since_sequence_id.
//
// Known limitation (accepted, not fixed): sequence_id is a wrapping
// uint32_t, and this comparison is a plain `>`, not wraparound-aware
// modular comparison. If the counter ever wraps (billions of events over
// the device's uptime), a client polling with a since_sequence_id from
// just before the wrap would incorrectly skip the low-numbered events
// right after it. Given the realistic event rate for this device, wrapping
// would take far longer than any single boot session, so this is treated
// as an MVP-scope tradeoff rather than implemented with modular distance
// comparison.
constexpr DiagnosticsReadResult diagnostics_ring_read_since(
    const DiagnosticsRing& ring,
    const std::uint32_t since_sequence_id,
    DiagnosticEvent* const destination,
    const std::size_t destination_capacity)
{
    DiagnosticsReadResult result{};
    result.highest_sequence_id = since_sequence_id;

    for (std::uint16_t offset = 0U; offset < ring.count; ++offset) {
        const std::uint16_t index = static_cast<std::uint16_t>(
            (ring.head + offset) % kDiagnosticsRingCapacity);
        const DiagnosticEvent& event = ring.events[index];
        if (event.sequence_id <= since_sequence_id) {
            continue;
        }
        if (result.count >= destination_capacity) {
            result.more_available = true;
            continue;
        }
        if (destination != nullptr) {
            destination[result.count] = event;
        }
        ++result.count;
        if (event.sequence_id > result.highest_sequence_id) {
            result.highest_sequence_id = event.sequence_id;
        }
    }

    return result;
}

constexpr const char* to_string(const DiagnosticCategory category)
{
    switch (category) {
        case DiagnosticCategory::network:
            return "network";
        case DiagnosticCategory::display:
            return "display";
        case DiagnosticCategory::storage:
            return "storage";
        case DiagnosticCategory::queue:
            return "queue";
        case DiagnosticCategory::lock:
            return "lock";
        case DiagnosticCategory::reboot:
            return "reboot";
        case DiagnosticCategory::ota:
            return "ota";
        case DiagnosticCategory::auth:
            return "auth";
    }
    return "queue";
}

constexpr const char* to_string(const DiagnosticSeverity severity)
{
    switch (severity) {
        case DiagnosticSeverity::info:
            return "info";
        case DiagnosticSeverity::warning:
            return "warning";
        case DiagnosticSeverity::error:
            return "error";
    }
    return "info";
}

}  // namespace pf_runtime
