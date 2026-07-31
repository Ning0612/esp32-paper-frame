#pragma once

#include <cstdint>

namespace pf_network {

enum class NetworkMode : std::uint8_t {
    boot,
    connecting_wifi,
    normal,
    provisioning_ap,
    offline_retry,
};

constexpr bool scan_allowed_in_mode(const NetworkMode mode)
{
    return mode == NetworkMode::normal ||
           mode == NetworkMode::provisioning_ap;
}

enum class WifiState : std::uint8_t {
    unknown,
    connecting,
    connected,
    starting_ap,
    provisioning,
    failed,
};

enum class InternetState : std::uint8_t {
    unknown,
    reachable,
    unreachable,
};

enum class NetworkEvent : std::uint8_t {
    sta_got_ip,
    sta_disconnected,
    sta_connect_timeout,
    internet_reachable,
    internet_unreachable,
    enter_recovery_ap,
    ap_started,
    ap_start_failed,
    wifi_initialize_failed,
    scan_requested,
    sntp_time_synced,
};

enum class NetworkAction : std::uint8_t {
    none,
    start_sta,
    retry_sta,
    start_ap,
};

struct NetworkPolicy {
    std::uint8_t maximum_sta_attempts = 3U;
    std::uint8_t maximum_ap_attempts = 3U;
};

class NetworkStateMachine {
public:
    NetworkStateMachine() = default;

    explicit NetworkStateMachine(const NetworkPolicy policy)
        : maximum_sta_attempts_(
              policy.maximum_sta_attempts == 0U
                  ? 1U
                  : policy.maximum_sta_attempts),
          maximum_ap_attempts_(
              policy.maximum_ap_attempts == 0U
                  ? 1U
                  : policy.maximum_ap_attempts)
    {
    }

    NetworkAction start(const bool credentials_configured)
    {
        internet_state_ = InternetState::unknown;
        if (!credentials_configured) {
            prepare_ap();
            return NetworkAction::start_ap;
        }

        mode_ = NetworkMode::connecting_wifi;
        wifi_state_ = WifiState::connecting;
        sta_attempt_ = 1U;
        return NetworkAction::start_sta;
    }

    NetworkAction handle(const NetworkEvent event)
    {
        if (event == NetworkEvent::wifi_initialize_failed) {
            enter_failed();
            return NetworkAction::none;
        }
        if (event == NetworkEvent::enter_recovery_ap) {
            prepare_ap();
            return NetworkAction::start_ap;
        }
        if (event == NetworkEvent::ap_started &&
            mode_ == NetworkMode::provisioning_ap &&
            wifi_state_ == WifiState::starting_ap) {
            wifi_state_ = WifiState::provisioning;
            ap_attempt_ = 0U;
            return NetworkAction::none;
        }
        if (event == NetworkEvent::ap_start_failed &&
            mode_ == NetworkMode::provisioning_ap &&
            wifi_state_ == WifiState::starting_ap) {
            if (ap_attempt_ < maximum_ap_attempts_) {
                ++ap_attempt_;
                return NetworkAction::start_ap;
            }
            enter_failed();
            return NetworkAction::none;
        }

        if (event == NetworkEvent::internet_reachable) {
            if (wifi_state_ == WifiState::connected) {
                internet_state_ = InternetState::reachable;
            }
            return NetworkAction::none;
        }
        if (event == NetworkEvent::internet_unreachable) {
            if (wifi_state_ == WifiState::connected) {
                internet_state_ = InternetState::unreachable;
            }
            return NetworkAction::none;
        }

        if (event == NetworkEvent::sta_got_ip &&
            (mode_ == NetworkMode::connecting_wifi ||
             mode_ == NetworkMode::offline_retry)) {
            mode_ = NetworkMode::normal;
            wifi_state_ = WifiState::connected;
            internet_state_ = InternetState::unknown;
            sta_attempt_ = 0U;
            return NetworkAction::none;
        }

        if ((event == NetworkEvent::sta_disconnected ||
             event == NetworkEvent::sta_connect_timeout) &&
            (mode_ == NetworkMode::connecting_wifi ||
             mode_ == NetworkMode::normal ||
             mode_ == NetworkMode::offline_retry)) {
            internet_state_ = InternetState::unknown;
            wifi_state_ = WifiState::connecting;
            mode_ = NetworkMode::offline_retry;
            if (sta_attempt_ == 0U) {
                sta_attempt_ = 1U;
            }
            if (sta_attempt_ < maximum_sta_attempts_) {
                ++sta_attempt_;
                return NetworkAction::retry_sta;
            }
            prepare_ap();
            return NetworkAction::start_ap;
        }

        return NetworkAction::none;
    }

    NetworkMode mode() const
    {
        return mode_;
    }

    WifiState wifi_state() const
    {
        return wifi_state_;
    }

    InternetState internet_state() const
    {
        return internet_state_;
    }

    std::uint8_t sta_attempt() const
    {
        return sta_attempt_;
    }

    std::uint8_t maximum_sta_attempts() const
    {
        return maximum_sta_attempts_;
    }

    std::uint8_t ap_attempt() const
    {
        return ap_attempt_;
    }

    std::uint8_t maximum_ap_attempts() const
    {
        return maximum_ap_attempts_;
    }

private:
    void prepare_ap()
    {
        mode_ = NetworkMode::provisioning_ap;
        wifi_state_ = WifiState::starting_ap;
        internet_state_ = InternetState::unknown;
        sta_attempt_ = 0U;
        ap_attempt_ = 1U;
    }

    void enter_failed()
    {
        mode_ = NetworkMode::boot;
        wifi_state_ = WifiState::failed;
        internet_state_ = InternetState::unknown;
        sta_attempt_ = 0U;
        ap_attempt_ = 0U;
    }

    NetworkMode mode_ = NetworkMode::boot;
    WifiState wifi_state_ = WifiState::unknown;
    InternetState internet_state_ = InternetState::unknown;
    std::uint8_t sta_attempt_ = 0U;
    std::uint8_t maximum_sta_attempts_ = 3U;
    std::uint8_t ap_attempt_ = 0U;
    std::uint8_t maximum_ap_attempts_ = 3U;
};

}  // namespace pf_network
