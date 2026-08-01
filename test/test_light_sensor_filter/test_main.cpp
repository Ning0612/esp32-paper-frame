#include <cstdint>

#include <unity.h>

#include "pf_sensors/light_sensor.hpp"

using pf_sensors::MovingAverageFilter;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_moving_average_ramps_up_while_buffer_fills()
{
    MovingAverageFilter<4> filter;
    TEST_ASSERT_EQUAL_UINT16(100U, filter.push(100U));
    TEST_ASSERT_EQUAL_UINT16(150U, filter.push(200U));
    TEST_ASSERT_EQUAL_UINT16(200U, filter.push(300U));
    TEST_ASSERT_EQUAL_UINT(3U, filter.sample_count());
}

void test_moving_average_drops_oldest_sample_once_full()
{
    MovingAverageFilter<3> filter;
    filter.push(100U);
    filter.push(100U);
    filter.push(100U);
    // Buffer full at [100,100,100]; pushing 400 evicts the oldest 100.
    TEST_ASSERT_EQUAL_UINT16(200U, filter.push(400U));
    TEST_ASSERT_EQUAL_UINT16(3U, filter.sample_count());
}

void test_reset_clears_accumulated_samples()
{
    MovingAverageFilter<3> filter;
    filter.push(1000U);
    filter.push(2000U);
    filter.reset();
    TEST_ASSERT_EQUAL_UINT(0U, filter.sample_count());
    TEST_ASSERT_EQUAL_UINT16(50U, filter.push(50U));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_moving_average_ramps_up_while_buffer_fills);
    RUN_TEST(test_moving_average_drops_oldest_sample_once_full);
    RUN_TEST(test_reset_clears_accumulated_samples);
    return UNITY_END();
}
