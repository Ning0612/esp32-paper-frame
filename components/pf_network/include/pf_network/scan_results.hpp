#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_config/network_credentials.hpp"

namespace pf_network {

enum class WifiSecurity : std::uint8_t {
    open,
    wep,
    wpa,
    wpa2,
    wpa3,
    enterprise,
    unknown,
};

struct RawScanRecord {
    char ssid[pf_config::kNetworkSsidCapacity]{};
    std::int8_t rssi = -127;
    WifiSecurity security = WifiSecurity::unknown;
};

struct ScanResult {
    char ssid[pf_config::kNetworkSsidCapacity]{};
    std::int8_t rssi = -127;
    WifiSecurity security = WifiSecurity::unknown;
};

constexpr const char* to_string(const WifiSecurity security)
{
    switch (security) {
        case WifiSecurity::open:
            return "open";
        case WifiSecurity::wep:
            return "wep";
        case WifiSecurity::wpa:
            return "wpa";
        case WifiSecurity::wpa2:
            return "wpa2";
        case WifiSecurity::wpa3:
            return "wpa3";
        case WifiSecurity::enterprise:
            return "enterprise";
        case WifiSecurity::unknown:
            return "unknown";
    }
    return "unknown";
}

inline bool should_enqueue_scan_request(
    const bool request_pending,
    const bool scan_active)
{
    return !request_pending && !scan_active;
}

inline std::size_t normalize_scan_results(
    const RawScanRecord* const input,
    const std::size_t input_count,
    ScanResult* const output,
    const std::size_t output_capacity)
{
    if ((input == nullptr && input_count != 0U) ||
        output == nullptr ||
        output_capacity == 0U) {
        return 0U;
    }

    std::size_t count = 0U;
    for (std::size_t input_index = 0U;
         input_index < input_count;
         ++input_index) {
        const RawScanRecord& candidate = input[input_index];
        const std::size_t length =
            pf_config::bounded_text_length(
                candidate.ssid,
                sizeof(candidate.ssid));
        if (length == 0U ||
            length >= sizeof(candidate.ssid) ||
            !pf_config::valid_utf8_text(
                candidate.ssid,
                length)) {
            continue;
        }

        std::size_t existing = count;
        for (std::size_t index = 0U; index < count; ++index) {
            if (std::strcmp(
                    output[index].ssid,
                    candidate.ssid) == 0) {
                existing = index;
                break;
            }
        }
        if (existing < count) {
            if (candidate.rssi > output[existing].rssi) {
                output[existing].rssi = candidate.rssi;
                output[existing].security = candidate.security;
            }
            continue;
        }

        std::size_t destination = count;
        if (count < output_capacity) {
            ++count;
        } else {
            std::size_t weakest = 0U;
            for (std::size_t index = 1U;
                 index < count;
                 ++index) {
                if (output[index].rssi <
                    output[weakest].rssi) {
                    weakest = index;
                }
            }
            if (candidate.rssi <= output[weakest].rssi) {
                continue;
            }
            destination = weakest;
        }
        std::memcpy(
            output[destination].ssid,
            candidate.ssid,
            length + 1U);
        output[destination].rssi = candidate.rssi;
        output[destination].security = candidate.security;
    }

    for (std::size_t outer = 1U; outer < count; ++outer) {
        const ScanResult value = output[outer];
        std::size_t inner = outer;
        while (inner > 0U &&
               (value.rssi > output[inner - 1U].rssi ||
                (value.rssi == output[inner - 1U].rssi &&
                 std::strcmp(
                     value.ssid,
                     output[inner - 1U].ssid) < 0))) {
            output[inner] = output[inner - 1U];
            --inner;
        }
        output[inner] = value;
    }
    return count;
}

}  // namespace pf_network
