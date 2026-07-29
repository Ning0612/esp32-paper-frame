#include <cstddef>
#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pf_network/access_point_credentials.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_entropy_formats_a_fixed_length_unambiguous_password()
{
    std::uint8_t entropy[pf_network::kAccessPointEntropyBytes]{};
    for (std::size_t index = 0U;
         index < sizeof(entropy);
         ++index) {
        entropy[index] = static_cast<std::uint8_t>(index);
    }
    char password[pf_network::kFormattedAccessPointPasswordCapacity]{};

    TEST_ASSERT_TRUE(
        pf_network::format_access_point_password(
            entropy,
            sizeof(entropy),
            password,
            sizeof(password)));
    TEST_ASSERT_EQUAL_STRING("PF-ABCDEFGHJKLM", password);
    TEST_ASSERT_EQUAL_UINT32(
        15U,
        std::strlen(password));
}

void test_password_formatter_rejects_short_entropy_or_output()
{
    const std::uint8_t entropy[
        pf_network::kAccessPointEntropyBytes]{};
    char password[pf_network::kFormattedAccessPointPasswordCapacity]{};

    TEST_ASSERT_FALSE(
        pf_network::format_access_point_password(
            entropy,
            sizeof(entropy) - 1U,
            password,
            sizeof(password)));
    TEST_ASSERT_FALSE(
        pf_network::format_access_point_password(
            entropy,
            sizeof(entropy),
            password,
            sizeof(password) - 1U));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_entropy_formats_a_fixed_length_unambiguous_password);
    RUN_TEST(test_password_formatter_rejects_short_entropy_or_output);
    return UNITY_END();
}
