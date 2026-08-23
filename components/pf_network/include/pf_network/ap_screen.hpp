#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_config/secure_memory.hpp"

namespace pf_network {

inline constexpr std::size_t kApScreenSsidCapacity = 33U;
inline constexpr std::size_t kApScreenPasswordCapacity = 65U;
inline constexpr std::size_t kWifiQrCapacity = 160U;
inline constexpr std::size_t kWebQrCapacity = 48U;
inline constexpr std::uint64_t kApModeImageTimeoutMs =
    5U * 60U * 1000U;

inline bool should_hold_access_point_screen(
    const bool ap_mode,
    const bool has_displayable_image,
    const std::uint64_t now_ms,
    const std::uint64_t ap_mode_started_ms)
{
    if (!ap_mode) {
        return false;
    }
    if (!has_displayable_image) {
        return true;
    }
    return now_ms < ap_mode_started_ms ||
           now_ms - ap_mode_started_ms < kApModeImageTimeoutMs;
}

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

// Tracks whether the AP instruction screen is what the panel is currently
// showing, so an unchanged payload can skip a ~31 s refresh.
//
// The skip is only sound while the claim is true. Holding the payload
// without tracking that -- which is what the presenter did until
// 2026-08-23 -- means the second AP session in a boot skips its refresh:
// the password, and so the payload, is unchanged since the first session,
// but the carousel has owned the panel in between. The radio then comes up
// with an image on the panel and the user cannot read the SSID, password
// or QR code, which is exactly when they need them.
//
// A named type rather than two loose fields, so `invalidate()` is a call
// site that can be found and tested instead of an assignment that is easy
// to forget.
// Two tasks touch this, so each field has exactly one writer.
// `payload`/`displayed` belong to the task that presents the AP
// screen; `superseded` is set by whichever task owns the panel
// afterwards. They share no lock on purpose: the presenter holds its
// submission mutex across a full ~31 s refresh, so making the panel
// owner wait for it would stall the carousel loop for half a minute.
//
// The residual race is deliberately biased safe. If an AP frame lands
// and an older carousel result is consumed just after, the cache is
// dropped while the AP screen really is on the panel -- costing one
// unnecessary refresh next session. The opposite error, claiming the
// panel while something else is on it, is the one that strands a user
// without their credentials, and that cannot happen: only the
// presenter sets the claim, and only after its own refresh.
struct AccessPointScreenCache {
    AccessPointScreenPayload payload{};
    bool displayed = false;
    std::atomic<bool> superseded{false};

    // True only when this exact payload is believed to be on the panel.
    bool shows(const AccessPointScreenPayload& candidate) const
    {
        return displayed &&
               !superseded.load(std::memory_order_acquire) &&
               same_access_point_screen_payload(payload, candidate);
    }

    void mark_displayed(const AccessPointScreenPayload& shown)
    {
        payload = shown;
        displayed = true;
        superseded.store(false, std::memory_order_release);
    }

    // Call whenever any other frame reaches the panel.
    void mark_superseded()
    {
        superseded.store(true, std::memory_order_release);
    }
};

}  // namespace pf_network
