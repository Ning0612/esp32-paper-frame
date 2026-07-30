#include <unity.h>

#include "pf_auth/session_manager.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

pf_auth::SessionSecrets secrets(const std::uint8_t seed)
{
    pf_auth::SessionSecrets value{};
    for (std::size_t index = 0U;
         index < value.token.size();
         ++index) {
        value.token[index] =
            static_cast<std::uint8_t>(seed + index);
        value.csrf[index] =
            static_cast<std::uint8_t>(seed + 0x40U + index);
    }
    return value;
}

void test_idle_timeout_expires_at_exactly_thirty_minutes()
{
    pf_auth::SessionManager manager{};
    const auto value = secrets(1U);
    TEST_ASSERT_TRUE(manager.issue(value, 1000U));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_auth::SessionCheck::valid),
        static_cast<std::uint8_t>(manager.authenticate(
            &value.token,
            1000U + pf_auth::kSessionIdleTimeoutMs - 1U)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_auth::SessionCheck::expired),
        static_cast<std::uint8_t>(manager.authenticate(
            &value.token,
            1000U + 2U * pf_auth::kSessionIdleTimeoutMs - 1U)));
}

void test_absolute_timeout_expires_even_when_session_is_active()
{
    pf_auth::SessionManager manager{};
    const auto value = secrets(3U);
    constexpr std::uint64_t kStart = 7000U;
    TEST_ASSERT_TRUE(manager.issue(value, kStart));

    for (std::uint64_t elapsed = pf_auth::kSessionIdleTimeoutMs / 2U;
         elapsed < pf_auth::kSessionAbsoluteTimeoutMs;
         elapsed += pf_auth::kSessionIdleTimeoutMs / 2U) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<std::uint8_t>(pf_auth::SessionCheck::valid),
            static_cast<std::uint8_t>(manager.authenticate(
                &value.token,
                kStart + elapsed)));
    }
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_auth::SessionCheck::expired),
        static_cast<std::uint8_t>(manager.authenticate(
            &value.token,
            kStart + pf_auth::kSessionAbsoluteTimeoutMs)));
}

void test_new_login_immediately_revokes_previous_session()
{
    pf_auth::SessionManager manager{};
    const auto first = secrets(5U);
    const auto second = secrets(9U);
    TEST_ASSERT_TRUE(manager.issue(first, 0U));
    TEST_ASSERT_TRUE(manager.issue(second, 10U));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_auth::SessionCheck::invalid),
        static_cast<std::uint8_t>(
            manager.authenticate(&first.token, 11U)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_auth::SessionCheck::valid),
        static_cast<std::uint8_t>(
            manager.authenticate(&second.token, 11U)));
    TEST_ASSERT_TRUE(manager.validate_csrf(&second.csrf));
    TEST_ASSERT_FALSE(manager.validate_csrf(&first.csrf));
}

void test_zero_secrets_and_clock_rollback_are_rejected()
{
    pf_auth::SessionManager manager{};
    const pf_auth::SessionSecrets zero{};
    TEST_ASSERT_FALSE(manager.issue(zero, 100U));

    const auto value = secrets(12U);
    TEST_ASSERT_TRUE(manager.issue(value, 100U));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_auth::SessionCheck::expired),
        static_cast<std::uint8_t>(
            manager.authenticate(&value.token, 99U)));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_timeout_expires_at_exactly_thirty_minutes);
    RUN_TEST(test_absolute_timeout_expires_even_when_session_is_active);
    RUN_TEST(test_new_login_immediately_revokes_previous_session);
    RUN_TEST(test_zero_secrets_and_clock_rollback_are_rejected);
    return UNITY_END();
}
