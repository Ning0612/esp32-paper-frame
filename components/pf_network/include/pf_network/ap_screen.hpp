#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_config/secure_memory.hpp"

namespace pf_network {

inline constexpr std::size_t kApScreenSsidCapacity = 33U;
inline constexpr std::size_t kApScreenPasswordCapacity = 65U;
inline constexpr std::size_t kWifiQrCapacity = 160U;
inline constexpr std::size_t kWebQrCapacity = 48U;

struct AccessPointScreenPayload {
    char ssid[kApScreenSsidCapacity]{};
    char password[kApScreenPasswordCapacity]{};
    char ip_address[16]{};
    char wifi_qr[kWifiQrCapacity]{};
    char web_qr[kWebQrCapacity]{};
    char device_suffix[5]{};
};

template <std::size_t Capacity>
inline bool copy_ap_screen_text(
    char (&destination)[Capacity],
    const char* const source)
{
    if (source == nullptr) {
        return false;
    }
    const std::size_t length = std::strlen(source);
    if (length >= Capacity) {
        return false;
    }
    std::memcpy(destination, source, length + 1U);
    return true;
}

inline bool append_qr_text(
    char* const destination,
    const std::size_t capacity,
    std::size_t& length,
    const char* const source,
    const bool escape_wifi)
{
    if (destination == nullptr || source == nullptr) {
        return false;
    }
    for (std::size_t index = 0U;
         source[index] != '\0';
         ++index) {
        const char value = source[index];
        const bool escaped =
            escape_wifi &&
            (value == '\\' || value == ';' ||
             value == ',' || value == ':' ||
             value == '"');
        if (length + (escaped ? 2U : 1U) >= capacity) {
            return false;
        }
        if (escaped) {
            destination[length++] = '\\';
        }
        destination[length++] = value;
    }
    destination[length] = '\0';
    return true;
}

inline bool build_access_point_screen_payload(
    const char* const ssid,
    const char* const password,
    const char* const device_suffix,
    AccessPointScreenPayload& destination)
{
    if (ssid == nullptr || password == nullptr ||
        device_suffix == nullptr ||
        std::strlen(device_suffix) != 4U ||
        std::strlen(password) < 8U) {
        return false;
    }

    AccessPointScreenPayload candidate{};
    const pf_config::SecureZeroGuard candidate_guard(candidate);
    if (!copy_ap_screen_text(candidate.ssid, ssid) ||
        !copy_ap_screen_text(candidate.password, password) ||
        !copy_ap_screen_text(
            candidate.ip_address,
            "192.168.4.1") ||
        !copy_ap_screen_text(
            candidate.web_qr,
            "http://192.168.4.1/") ||
        !copy_ap_screen_text(
            candidate.device_suffix,
            device_suffix)) {
        return false;
    }

    std::size_t length = 0U;
    if (!append_qr_text(
            candidate.wifi_qr,
            sizeof(candidate.wifi_qr),
            length,
            "WIFI:T:WPA;S:",
            false) ||
        !append_qr_text(
            candidate.wifi_qr,
            sizeof(candidate.wifi_qr),
            length,
            ssid,
            true) ||
        !append_qr_text(
            candidate.wifi_qr,
            sizeof(candidate.wifi_qr),
            length,
            ";P:",
            false) ||
        !append_qr_text(
            candidate.wifi_qr,
            sizeof(candidate.wifi_qr),
            length,
            password,
            true) ||
        !append_qr_text(
            candidate.wifi_qr,
            sizeof(candidate.wifi_qr),
            length,
            ";;",
            false)) {
        return false;
    }
    destination = candidate;
    return true;
}

inline bool same_access_point_screen_payload(
    const AccessPointScreenPayload& left,
    const AccessPointScreenPayload& right)
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

bool render_access_point_screen(
    std::uint8_t* frame,
    std::size_t length,
    const AccessPointScreenPayload& payload);

}  // namespace pf_network
