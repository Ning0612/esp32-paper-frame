#include <cstring>

#include <unity.h>

#include "pf_config/management_password.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

pf_config::ManagementPasswordHash valid_record()
{
    pf_config::ManagementPasswordHash record{};
    record.iterations = 600000U;
    for (std::size_t index = 0U;
         index < sizeof(record.salt);
         ++index) {
        record.salt[index] =
            static_cast<std::uint8_t>(index + 1U);
    }
    for (std::size_t index = 0U;
         index < sizeof(record.hash);
         ++index) {
        record.hash[index] =
            static_cast<std::uint8_t>(0xA0U + index);
    }
    record.crc32 =
        pf_config::management_password_crc32(record);
    return record;
}

void test_versioned_password_hash_record_detects_corruption()
{
    auto record = valid_record();
    TEST_ASSERT_TRUE(
        pf_config::management_password_hash_valid(record));

    record.hash[7] ^= 0x40U;
    TEST_ASSERT_FALSE(
        pf_config::management_password_hash_valid(record));
}

void test_unknown_algorithm_and_unsafe_work_factor_are_rejected()
{
    auto record = valid_record();
    ++record.algorithm;
    record.crc32 =
        pf_config::management_password_crc32(record);
    TEST_ASSERT_FALSE(
        pf_config::management_password_hash_valid(record));

    record = valid_record();
    record.iterations =
        pf_config::kManagementPasswordMinimumIterations - 1U;
    record.crc32 =
        pf_config::management_password_crc32(record);
    TEST_ASSERT_FALSE(
        pf_config::management_password_hash_valid(record));
}

void test_constant_time_comparison_checks_all_bytes()
{
    std::uint8_t left[] = {1U, 2U, 3U, 4U};
    std::uint8_t right[] = {1U, 2U, 3U, 4U};
    TEST_ASSERT_TRUE(
        pf_config::constant_time_equal(left, right, sizeof(left)));
    right[3] = 9U;
    TEST_ASSERT_FALSE(
        pf_config::constant_time_equal(left, right, sizeof(left)));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_versioned_password_hash_record_detects_corruption);
    RUN_TEST(test_unknown_algorithm_and_unsafe_work_factor_are_rejected);
    RUN_TEST(test_constant_time_comparison_checks_all_bytes);
    return UNITY_END();
}
