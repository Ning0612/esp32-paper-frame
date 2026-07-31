#pragma once

#include <cstdint>

namespace pf_network {

// ESP-IDF's SNTP client has no notion of a terminal failure: it keeps
// retrying in the background once started, so there is no event that
// would drive a "failed" state. Only track what actually happens:
// not yet started, waiting for the first sync, or synced at least once.
enum class TimeSyncState : std::uint8_t {
    unsynced,
    syncing,
    synced,
};

enum class TimeSyncEvent : std::uint8_t {
    wifi_connected,
    wifi_disconnected,
    sntp_synced,
};

constexpr TimeSyncState next_time_sync_state(
    const TimeSyncState current,
    const TimeSyncEvent event)
{
    switch (event) {
        case TimeSyncEvent::wifi_connected:
            return current == TimeSyncState::synced
                       ? TimeSyncState::synced
                       : TimeSyncState::syncing;
        case TimeSyncEvent::wifi_disconnected:
            // A previously synced clock keeps drifting forward and stays
            // usable; only reset to unsynced if we never synced at all.
            return current == TimeSyncState::synced
                       ? TimeSyncState::synced
                       : TimeSyncState::unsynced;
        case TimeSyncEvent::sntp_synced:
            return TimeSyncState::synced;
    }
    return current;
}

constexpr const char* to_string(const TimeSyncState state)
{
    switch (state) {
        case TimeSyncState::unsynced:
            return "unsynced";
        case TimeSyncState::syncing:
            return "syncing";
        case TimeSyncState::synced:
            return "synced";
    }
    return "unsynced";
}

}  // namespace pf_network
