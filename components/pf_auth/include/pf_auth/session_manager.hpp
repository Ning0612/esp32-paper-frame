#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pf_config/management_password.hpp"
#include "pf_config/secure_memory.hpp"

namespace pf_auth {

inline constexpr std::size_t kSessionSecretBytes = 32U;
inline constexpr std::uint64_t kSessionIdleTimeoutMs =
    30ULL * 60ULL * 1000ULL;
inline constexpr std::uint64_t kSessionAbsoluteTimeoutMs =
    24ULL * 60ULL * 60ULL * 1000ULL;

using SessionSecret = std::array<std::uint8_t, kSessionSecretBytes>;

struct SessionSecrets {
    SessionSecret token{};
    SessionSecret csrf{};
};

enum class SessionCheck : std::uint8_t {
    valid,
    missing,
    invalid,
    expired,
};

class SessionManager {
public:
    ~SessionManager()
    {
        revoke();
    }

    bool issue(
        const SessionSecrets& secrets,
        const std::uint64_t now_ms)
    {
        if (all_zero(secrets.token) || all_zero(secrets.csrf)) {
            return false;
        }
        revoke();
        secrets_ = secrets;
        created_ms_ = now_ms;
        last_activity_ms_ = now_ms;
        active_ = true;
        return true;
    }

    SessionCheck authenticate(
        const SessionSecret* const candidate,
        const std::uint64_t now_ms,
        const bool touch = true)
    {
        if (!active_) {
            return SessionCheck::missing;
        }
        if (expired(now_ms)) {
            revoke();
            return SessionCheck::expired;
        }
        if (candidate == nullptr ||
            !pf_config::constant_time_equal(
                secrets_.token.data(),
                candidate->data(),
                secrets_.token.size())) {
            return SessionCheck::invalid;
        }
        if (touch) {
            last_activity_ms_ = now_ms;
        }
        return SessionCheck::valid;
    }

    bool validate_csrf(const SessionSecret* const candidate) const
    {
        return active_ && candidate != nullptr &&
               pf_config::constant_time_equal(
                   secrets_.csrf.data(),
                   candidate->data(),
                   secrets_.csrf.size());
    }

    const SessionSecret* csrf() const
    {
        return active_ ? &secrets_.csrf : nullptr;
    }

    bool active() const
    {
        return active_;
    }

    void revoke()
    {
        pf_config::secure_zero(secrets_);
        created_ms_ = 0U;
        last_activity_ms_ = 0U;
        active_ = false;
    }

private:
    static bool all_zero(const SessionSecret& value)
    {
        std::uint8_t combined = 0U;
        for (const std::uint8_t byte : value) {
            combined |= byte;
        }
        return combined == 0U;
    }

    bool expired(const std::uint64_t now_ms) const
    {
        if (now_ms < created_ms_ || now_ms < last_activity_ms_) {
            return true;
        }
        return now_ms - created_ms_ >=
                   kSessionAbsoluteTimeoutMs ||
               now_ms - last_activity_ms_ >=
                   kSessionIdleTimeoutMs;
    }

    SessionSecrets secrets_{};
    std::uint64_t created_ms_ = 0U;
    std::uint64_t last_activity_ms_ = 0U;
    bool active_ = false;
};

}  // namespace pf_auth
