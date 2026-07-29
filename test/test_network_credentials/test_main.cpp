#include <array>
#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pf_config/network_credentials.hpp"
#include "pf_config/secure_memory.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_valid_credentials_round_trip_without_plaintext_metadata()
{
    pf_config::NetworkCredentials credentials{};
    TEST_ASSERT_TRUE(
        pf_config::copy_network_credential(
            credentials.ssid,
            "Home Network"));
    TEST_ASSERT_TRUE(
        pf_config::copy_network_credential(
            credentials.password,
            "correct horse battery staple"));

    pf_config::NetworkCredentialBlob blob{};
    TEST_ASSERT_TRUE(
        pf_config::encode_network_credentials(credentials, blob));

    pf_config::NetworkCredentials decoded{};
    TEST_ASSERT_TRUE(
        pf_config::decode_network_credentials(blob, decoded));
    TEST_ASSERT_EQUAL_STRING("Home Network", decoded.ssid);
    TEST_ASSERT_EQUAL_STRING(
        "correct horse battery staple",
        decoded.password);
}

void test_invalid_ssid_and_password_lengths_are_rejected()
{
    pf_config::NetworkCredentials credentials{};
    TEST_ASSERT_FALSE(
        pf_config::network_credentials_valid(credentials));

    std::memset(credentials.ssid, 's', 32U);
    credentials.ssid[32] = '\0';
    std::memset(credentials.password, 'p', 7U);
    credentials.password[7] = '\0';
    TEST_ASSERT_FALSE(
        pf_config::network_credentials_valid(credentials));

    credentials.password[0] = '\0';
    TEST_ASSERT_TRUE(
        pf_config::network_credentials_valid(credentials));
}

void test_corrupt_or_unknown_blob_is_rejected_without_output()
{
    pf_config::NetworkCredentials credentials{};
    pf_config::copy_network_credential(credentials.ssid, "Studio");
    pf_config::copy_network_credential(
        credentials.password,
        "12345678");

    pf_config::NetworkCredentialBlob blob{};
    TEST_ASSERT_TRUE(
        pf_config::encode_network_credentials(credentials, blob));
    blob.payload[0] ^= 0x80U;

    pf_config::NetworkCredentials decoded{};
    pf_config::copy_network_credential(decoded.ssid, "unchanged");
    TEST_ASSERT_FALSE(
        pf_config::decode_network_credentials(blob, decoded));
    TEST_ASSERT_EQUAL_STRING("unchanged", decoded.ssid);

    TEST_ASSERT_TRUE(
        pf_config::encode_network_credentials(credentials, blob));
    ++blob.version;
    TEST_ASSERT_FALSE(
        pf_config::decode_network_credentials(blob, decoded));
}

void test_sensitive_buffers_are_explicitly_zeroed()
{
    char secret[] = "temporary-password";
    pf_config::secure_zero(secret, sizeof(secret));
    for (const char value : secret) {
        TEST_ASSERT_EQUAL_INT8(0, value);
    }
}

void test_sensitive_buffers_are_zeroed_when_guard_leaves_scope()
{
    std::array<char, 20U> secret{};
    std::memcpy(
        secret.data(),
        "temporary-password",
        sizeof("temporary-password"));
    {
        const pf_config::SecureZeroGuard guard(secret);
        TEST_ASSERT_EQUAL_STRING(
            "temporary-password",
            secret.data());
    }
    for (const char value : secret) {
        TEST_ASSERT_EQUAL_INT8(0, value);
    }
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_credentials_round_trip_without_plaintext_metadata);
    RUN_TEST(test_invalid_ssid_and_password_lengths_are_rejected);
    RUN_TEST(test_corrupt_or_unknown_blob_is_rejected_without_output);
    RUN_TEST(test_sensitive_buffers_are_explicitly_zeroed);
    RUN_TEST(test_sensitive_buffers_are_zeroed_when_guard_leaves_scope);
    return UNITY_END();
}
