#include <cstdint>

#include "pf_network/state_machine.hpp"
#include "unity.h"

namespace {

using pf_network::InternetState;
using pf_network::NetworkAction;
using pf_network::NetworkEvent;
using pf_network::NetworkMode;
using pf_network::NetworkStateMachine;
using pf_network::scan_allowed_in_mode;
using pf_network::WifiState;

void test_blank_credentials_enter_provisioning_ap()
{
    NetworkStateMachine machine{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(machine.start(false)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkMode::provisioning_ap),
        static_cast<int>(machine.mode()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WifiState::starting_ap),
        static_cast<int>(machine.wifi_state()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::none),
        static_cast<int>(machine.handle(NetworkEvent::ap_started)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WifiState::provisioning),
        static_cast<int>(machine.wifi_state()));
}

void test_configured_credentials_start_station_and_reach_normal()
{
    NetworkStateMachine machine{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_sta),
        static_cast<int>(machine.start(true)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkMode::connecting_wifi),
        static_cast<int>(machine.mode()));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::none),
        static_cast<int>(machine.handle(NetworkEvent::sta_got_ip)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkMode::normal),
        static_cast<int>(machine.mode()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WifiState::connected),
        static_cast<int>(machine.wifi_state()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InternetState::unknown),
        static_cast<int>(machine.internet_state()));
}

void test_authenticated_station_allows_wifi_scan()
{
    TEST_ASSERT_TRUE(scan_allowed_in_mode(NetworkMode::normal));
    TEST_ASSERT_TRUE(scan_allowed_in_mode(NetworkMode::provisioning_ap));
    TEST_ASSERT_FALSE(scan_allowed_in_mode(NetworkMode::boot));
    TEST_ASSERT_FALSE(scan_allowed_in_mode(NetworkMode::connecting_wifi));
    TEST_ASSERT_FALSE(scan_allowed_in_mode(NetworkMode::offline_retry));
}

void test_internet_failure_never_forces_provisioning_ap()
{
    NetworkStateMachine machine{};
    machine.start(true);
    machine.handle(NetworkEvent::sta_got_ip);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::none),
        static_cast<int>(
            machine.handle(NetworkEvent::internet_unreachable)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkMode::normal),
        static_cast<int>(machine.mode()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WifiState::connected),
        static_cast<int>(machine.wifi_state()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InternetState::unreachable),
        static_cast<int>(machine.internet_state()));
}

void test_station_failures_retry_then_fall_back_to_ap()
{
    NetworkStateMachine machine{
        pf_network::NetworkPolicy{.maximum_sta_attempts = 3U},
    };
    machine.start(true);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::retry_sta),
        static_cast<int>(
            machine.handle(NetworkEvent::sta_disconnected)));
    TEST_ASSERT_EQUAL_UINT8(2U, machine.sta_attempt());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::retry_sta),
        static_cast<int>(
            machine.handle(NetworkEvent::sta_connect_timeout)));
    TEST_ASSERT_EQUAL_UINT8(3U, machine.sta_attempt());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(
            machine.handle(NetworkEvent::sta_disconnected)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkMode::provisioning_ap),
        static_cast<int>(machine.mode()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WifiState::starting_ap),
        static_cast<int>(machine.wifi_state()));
}

void test_disconnect_after_normal_uses_same_bounded_retry_policy()
{
    NetworkStateMachine machine{
        pf_network::NetworkPolicy{.maximum_sta_attempts = 2U},
    };
    machine.start(true);
    machine.handle(NetworkEvent::sta_got_ip);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::retry_sta),
        static_cast<int>(
            machine.handle(NetworkEvent::sta_disconnected)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkMode::offline_retry),
        static_cast<int>(machine.mode()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InternetState::unknown),
        static_cast<int>(machine.internet_state()));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(
            machine.handle(NetworkEvent::sta_connect_timeout)));
}

void test_recovery_request_enters_ap_from_any_station_state()
{
    NetworkStateMachine connecting{};
    connecting.start(true);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(
            connecting.handle(NetworkEvent::enter_recovery_ap)));

    NetworkStateMachine normal{};
    normal.start(true);
    normal.handle(NetworkEvent::sta_got_ip);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(
            normal.handle(NetworkEvent::enter_recovery_ap)));
}

void test_access_point_failures_retry_then_publish_failed()
{
    NetworkStateMachine machine{
        pf_network::NetworkPolicy{
            .maximum_sta_attempts = 3U,
            .maximum_ap_attempts = 3U,
        },
    };
    machine.start(false);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(
            machine.handle(NetworkEvent::ap_start_failed)));
    TEST_ASSERT_EQUAL_UINT8(2U, machine.ap_attempt());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(
            machine.handle(NetworkEvent::ap_start_failed)));
    TEST_ASSERT_EQUAL_UINT8(3U, machine.ap_attempt());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::none),
        static_cast<int>(
            machine.handle(NetworkEvent::ap_start_failed)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WifiState::failed),
        static_cast<int>(machine.wifi_state()));
}

void test_wifi_initialization_failure_is_not_reported_as_provisioning()
{
    NetworkStateMachine machine{};
    machine.start(false);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::none),
        static_cast<int>(
            machine.handle(NetworkEvent::wifi_initialize_failed)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WifiState::failed),
        static_cast<int>(machine.wifi_state()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InternetState::unknown),
        static_cast<int>(machine.internet_state()));
}

void test_invalid_policy_is_normalized_to_one_attempt()
{
    NetworkStateMachine machine{
        pf_network::NetworkPolicy{
            .maximum_sta_attempts = 0U,
            .maximum_ap_attempts = 0U,
        },
    };

    machine.start(true);
    TEST_ASSERT_EQUAL_UINT8(1U, machine.maximum_sta_attempts());
    TEST_ASSERT_EQUAL_UINT8(1U, machine.maximum_ap_attempts());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkAction::start_ap),
        static_cast<int>(
            machine.handle(NetworkEvent::sta_connect_timeout)));
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_credentials_enter_provisioning_ap);
    RUN_TEST(test_configured_credentials_start_station_and_reach_normal);
    RUN_TEST(test_authenticated_station_allows_wifi_scan);
    RUN_TEST(test_internet_failure_never_forces_provisioning_ap);
    RUN_TEST(test_station_failures_retry_then_fall_back_to_ap);
    RUN_TEST(test_disconnect_after_normal_uses_same_bounded_retry_policy);
    RUN_TEST(test_recovery_request_enters_ap_from_any_station_state);
    RUN_TEST(test_access_point_failures_retry_then_publish_failed);
    RUN_TEST(
        test_wifi_initialization_failure_is_not_reported_as_provisioning);
    RUN_TEST(test_invalid_policy_is_normalized_to_one_attempt);
    return UNITY_END();
}
