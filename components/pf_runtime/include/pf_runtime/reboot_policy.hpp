#pragma once

#include <cstdint>

namespace pf_runtime {

// A scheduled reboot fires from a one-shot timer this many microseconds
// after it is armed, which is enough for an HTTP response to reach the
// socket before the device goes down.
inline constexpr std::uint64_t kRebootDelayUs = 500'000ULL;

// If the panel is mid-refresh when that timer fires, the reboot is deferred
// by one more interval rather than cutting the refresh short. A full E6
// refresh measured 31.2 s on hardware (see docs/hardware/VALIDATION.md), so
// the cap is set well above it: 90 x 500 ms = 45 s of deferral, landing
// 45.5 s after the reboot was requested once the initial delay is counted.
// Reaching the cap means the refresh is stuck, and a stuck panel must not be
// able to block a reboot indefinitely -- particularly an OTA one, where
// refusing to reboot would strand the device on firmware it has replaced.
inline constexpr std::uint32_t kMaxRebootDeferrals = 90U;

// Whether a reboot that is due right now should wait for the display
// instead. Kept as a pure function so the "stuck panel still reboots"
// boundary is covered by host tests rather than only by hardware.
constexpr bool should_defer_reboot(
    const bool display_busy,
    const std::uint32_t deferrals_so_far,
    const std::uint32_t maximum_deferrals = kMaxRebootDeferrals)
{
    return display_busy && deferrals_so_far < maximum_deferrals;
}

}  // namespace pf_runtime
