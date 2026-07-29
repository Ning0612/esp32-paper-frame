#include <cstdint>

#include <unity.h>

#include "pf_web/http_receive_policy.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_absolute_deadline_does_not_extend_when_bytes_arrive()
{
    constexpr std::uint64_t started_ms = 1000U;
    constexpr std::uint32_t maximum_ms = 15000U;

    TEST_ASSERT_FALSE(
        pf_web::body_receive_deadline_expired(
            started_ms + maximum_ms - 1U,
            started_ms,
            maximum_ms));
    TEST_ASSERT_TRUE(
        pf_web::body_receive_deadline_expired(
            started_ms + maximum_ms,
            started_ms,
            maximum_ms));
    TEST_ASSERT_TRUE(
        pf_web::body_receive_deadline_expired(
            started_ms + maximum_ms + 5000U,
            started_ms,
            maximum_ms));
}

void test_idle_timeout_limit_is_bounded()
{
    TEST_ASSERT_FALSE(
        pf_web::body_receive_idle_limit_reached(2U, 3U));
    TEST_ASSERT_TRUE(
        pf_web::body_receive_idle_limit_reached(3U, 3U));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_absolute_deadline_does_not_extend_when_bytes_arrive);
    RUN_TEST(test_idle_timeout_limit_is_bounded);
    return UNITY_END();
}
