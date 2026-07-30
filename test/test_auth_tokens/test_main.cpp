#include <cstring>

#include <unity.h>

#include "pf_auth/token_codec.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_session_secret_round_trips_as_fixed_lowercase_hex()
{
    pf_auth::SessionSecret secret{};
    for (std::size_t index = 0U; index < secret.size(); ++index) {
        secret[index] = static_cast<std::uint8_t>(index);
    }
    char encoded[pf_auth::kEncodedSecretCapacity]{};
    pf_auth::encode_secret(secret, encoded);
    TEST_ASSERT_EQUAL_UINT(
        pf_auth::kEncodedSecretLength,
        std::strlen(encoded));
    TEST_ASSERT_EQUAL_STRING(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f",
        encoded);

    pf_auth::SessionSecret decoded{};
    TEST_ASSERT_TRUE(pf_auth::decode_secret(encoded, decoded));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        secret.data(),
        decoded.data(),
        secret.size());
}

void test_wrong_length_or_non_hex_secret_is_rejected()
{
    pf_auth::SessionSecret decoded{};
    TEST_ASSERT_FALSE(pf_auth::decode_secret("abcd", decoded));

    char encoded[pf_auth::kEncodedSecretCapacity]{};
    for (std::size_t index = 0U;
         index < pf_auth::kEncodedSecretLength;
         ++index) {
        encoded[index] = '0';
    }
    encoded[11] = 'z';
    TEST_ASSERT_FALSE(pf_auth::decode_secret(encoded, decoded));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_session_secret_round_trips_as_fixed_lowercase_hex);
    RUN_TEST(test_wrong_length_or_non_hex_secret_is_rejected);
    return UNITY_END();
}
