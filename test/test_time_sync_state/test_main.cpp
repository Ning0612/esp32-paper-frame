#include "pf_network/time_sync_state.hpp"
#include "unity.h"

namespace {

using pf_network::next_time_sync_state;
using pf_network::TimeSyncEvent;
using pf_network::TimeSyncState;

void test_wifi_connected_starts_syncing_from_unsynced()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TimeSyncState::syncing),
        static_cast<int>(next_time_sync_state(
            TimeSyncState::unsynced,
            TimeSyncEvent::wifi_connected)));
}

void test_sntp_synced_reaches_synced_from_syncing()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TimeSyncState::synced),
        static_cast<int>(next_time_sync_state(
            TimeSyncState::syncing,
            TimeSyncEvent::sntp_synced)));
}

void test_disconnect_before_first_sync_reverts_to_unsynced()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TimeSyncState::unsynced),
        static_cast<int>(next_time_sync_state(
            TimeSyncState::syncing,
            TimeSyncEvent::wifi_disconnected)));
}

void test_disconnect_after_sync_keeps_synced_clock()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TimeSyncState::synced),
        static_cast<int>(next_time_sync_state(
            TimeSyncState::synced,
            TimeSyncEvent::wifi_disconnected)));
}

void test_reconnect_after_sync_stays_synced_without_resyncing()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TimeSyncState::synced),
        static_cast<int>(next_time_sync_state(
            TimeSyncState::synced,
            TimeSyncEvent::wifi_connected)));
}

void test_to_string_covers_every_state()
{
    TEST_ASSERT_EQUAL_STRING(
        "unsynced", pf_network::to_string(TimeSyncState::unsynced));
    TEST_ASSERT_EQUAL_STRING(
        "syncing", pf_network::to_string(TimeSyncState::syncing));
    TEST_ASSERT_EQUAL_STRING(
        "synced", pf_network::to_string(TimeSyncState::synced));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_wifi_connected_starts_syncing_from_unsynced);
    RUN_TEST(test_sntp_synced_reaches_synced_from_syncing);
    RUN_TEST(test_disconnect_before_first_sync_reverts_to_unsynced);
    RUN_TEST(test_disconnect_after_sync_keeps_synced_clock);
    RUN_TEST(test_reconnect_after_sync_stays_synced_without_resyncing);
    RUN_TEST(test_to_string_covers_every_state);
    return UNITY_END();
}
