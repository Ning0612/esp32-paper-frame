#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <unity.h>

#include "pf_display/epd7in3e.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_display::DriverStage;
using pf_display::DriverStatus;
using pf_display::Epd7in3eDriver;
using pf_display::PanelState;
using pf_display::PanelTransport;

enum class TransactionKind : std::uint8_t {
    command,
    data,
};

struct Transaction {
    TransactionKind kind;
    std::vector<std::uint8_t> bytes;
};

void assert_transaction_equal(
    const Transaction& expected,
    const Transaction& actual)
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(expected.kind),
        static_cast<int>(actual.kind));
    TEST_ASSERT_EQUAL_UINT32(expected.bytes.size(), actual.bytes.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected.bytes.data(),
        actual.bytes.data(),
        expected.bytes.size());
}

enum class BusyMode : std::uint8_t {
    idle,
    stuck,
    idle_then_stuck,
};

class FakeTransport final : public PanelTransport {
public:
    bool set_reset_level(const bool high) override
    {
        reset_levels.push_back(high);
        return !fail_reset;
    }

    bool write_command_transaction(const std::uint8_t command) override
    {
        transactions.push_back(
            {TransactionKind::command, std::vector<std::uint8_t>{command}});
        return !fail_commands;
    }

    bool write_data_transaction(
        const std::uint8_t* const data,
        const std::size_t length) override
    {
        if (fail_data_length == length) {
            return false;
        }
        transactions.push_back(
            {TransactionKind::data,
             std::vector<std::uint8_t>(data, data + length)});
        return true;
    }

    bool busy_is_idle(bool& idle) override
    {
        ++busy_read_count;
        if (fail_busy_read) {
            return false;
        }
        switch (busy_mode) {
        case BusyMode::idle:
            idle = true;
            break;
        case BusyMode::stuck:
            idle = false;
            break;
        case BusyMode::idle_then_stuck:
            idle = successful_busy_waits < idle_waits_before_stuck;
            if (idle) {
                ++successful_busy_waits;
            }
            break;
        }
        return true;
    }

    std::uint64_t monotonic_ms() const override
    {
        return now_ms;
    }

    void delay_ms(const std::uint32_t duration_ms) override
    {
        delays.push_back(duration_ms);
        now_ms += duration_ms;
    }

    std::vector<std::uint8_t> command_bytes() const
    {
        std::vector<std::uint8_t> commands;
        for (const Transaction& transaction : transactions) {
            if (transaction.kind == TransactionKind::command) {
                commands.push_back(transaction.bytes.front());
            }
        }
        return commands;
    }

    bool fail_reset = false;
    bool fail_commands = false;
    bool fail_busy_read = false;
    std::size_t fail_data_length = std::numeric_limits<std::size_t>::max();
    BusyMode busy_mode = BusyMode::idle;
    std::size_t idle_waits_before_stuck = 0;
    std::size_t successful_busy_waits = 0;
    std::size_t busy_read_count = 0;
    std::uint64_t now_ms = 0;
    std::vector<bool> reset_levels;
    std::vector<std::uint32_t> delays;
    std::vector<Transaction> transactions;
};

std::size_t find_command_transaction(
    const FakeTransport& transport,
    const std::uint8_t command)
{
    for (std::size_t index = 0; index < transport.transactions.size(); ++index) {
        const Transaction& transaction = transport.transactions[index];
        if (transaction.kind == TransactionKind::command &&
            transaction.bytes.front() == command) {
            return index;
        }
    }
    return transport.transactions.size();
}

void test_fixed_transport_contract_matches_g2_and_g3()
{
    TEST_ASSERT_EQUAL_INT(11, pf_display::kPanelMosiGpio);
    TEST_ASSERT_EQUAL_INT(12, pf_display::kPanelClockGpio);
    TEST_ASSERT_EQUAL_INT(10, pf_display::kPanelChipSelectGpio);
    TEST_ASSERT_EQUAL_INT(13, pf_display::kPanelDataCommandGpio);
    TEST_ASSERT_EQUAL_INT(14, pf_display::kPanelResetGpio);
    TEST_ASSERT_EQUAL_INT(4, pf_display::kPanelBusyGpio);
    TEST_ASSERT_EQUAL_UINT32(2000000, pf_display::kPanelSpiClockHz);
    TEST_ASSERT_EQUAL_UINT32(4096, pf_display::kPanelTransactionBytes);
    TEST_ASSERT_EQUAL_UINT32(60000, pf_display::kBusyTimeoutMs);
}

void test_tick_conversion_guarantees_minimum_delays()
{
    TEST_ASSERT_EQUAL_UINT32(
        2,
        pf_display::detail::ticks_to_wait_at_least(2, 10));
    TEST_ASSERT_EQUAL_UINT32(
        2,
        pf_display::detail::ticks_to_wait_at_least(5, 10));
    TEST_ASSERT_EQUAL_UINT32(
        3,
        pf_display::detail::ticks_to_wait_at_least(20, 10));
    TEST_ASSERT_EQUAL_UINT32(
        4,
        pf_display::detail::ticks_to_wait_at_least(30, 10));
    TEST_ASSERT_EQUAL_UINT32(
        6,
        pf_display::detail::ticks_to_wait_at_least(5, 1));
    TEST_ASSERT_EQUAL_UINT32(
        0,
        pf_display::detail::ticks_to_wait_at_least(0, 10));
    TEST_ASSERT_EQUAL_UINT32(
        0,
        pf_display::detail::ticks_to_wait_at_least(5, 0));
}

void test_invalid_frame_is_rejected_before_any_panel_io()
{
    FakeTransport transport;
    Epd7in3eDriver driver{transport};
    std::array<std::uint8_t, 4> short_frame{};

    const auto null_result =
        driver.refresh_and_sleep(nullptr, pf_display::kFullFramebufferBytes);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStatus::invalid_frame),
        static_cast<int>(null_result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStage::validate),
        static_cast<int>(null_result.stage));

    const auto short_result =
        driver.refresh_and_sleep(short_frame.data(), short_frame.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStatus::invalid_frame),
        static_cast<int>(short_result.status));
    TEST_ASSERT_TRUE(transport.reset_levels.empty());
    TEST_ASSERT_TRUE(transport.transactions.empty());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PanelState::unknown),
        static_cast<int>(driver.state()));
}

void test_successful_refresh_matches_upstream_trace_and_sleeps()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> frame{};
    std::fill(frame.begin(), frame.end(), 0x56);
    FakeTransport transport;
    Epd7in3eDriver driver{transport};

    const auto result = driver.refresh_and_sleep(frame.data(), frame.size());

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStatus::ok),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStage::deep_sleep),
        static_cast<int>(result.stage));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PanelState::deep_sleep),
        static_cast<int>(driver.state()));

    const bool expected_reset[] = {true, false, true};
    TEST_ASSERT_EQUAL_INT(
        3,
        static_cast<int>(transport.reset_levels.size()));
    for (std::size_t index = 0; index < 3; ++index) {
        TEST_ASSERT_EQUAL(
            expected_reset[index],
            transport.reset_levels[index]);
    }

    const std::uint8_t expected_commands[] = {
        0xAA, 0x01, 0x00, 0x03, 0x05, 0x06, 0x08,
        0x30, 0x50, 0x60, 0x61, 0x84, 0xE3, 0x04,
        0x10, 0x04, 0x06, 0x12, 0x02, 0x02, 0x07,
    };
    const std::vector<std::uint8_t> commands = transport.command_bytes();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected_commands,
        commands.data(),
        sizeof(expected_commands));
    TEST_ASSERT_EQUAL_UINT32(6, transport.busy_read_count);

    const std::vector<Transaction> expected_prefix = {
        {TransactionKind::command, {0xAA}},
        {TransactionKind::data, {0x49, 0x55, 0x20, 0x08, 0x09, 0x18}},
        {TransactionKind::command, {0x01}},
        {TransactionKind::data, {0x3F}},
        {TransactionKind::command, {0x00}},
        {TransactionKind::data, {0x5F, 0x69}},
        {TransactionKind::command, {0x03}},
        {TransactionKind::data, {0x00, 0x54, 0x00, 0x44}},
        {TransactionKind::command, {0x05}},
        {TransactionKind::data, {0x40, 0x1F, 0x1F, 0x2C}},
        {TransactionKind::command, {0x06}},
        {TransactionKind::data, {0x6F, 0x1F, 0x17, 0x49}},
        {TransactionKind::command, {0x08}},
        {TransactionKind::data, {0x6F, 0x1F, 0x1F, 0x22}},
        {TransactionKind::command, {0x30}},
        {TransactionKind::data, {0x03}},
        {TransactionKind::command, {0x50}},
        {TransactionKind::data, {0x3F}},
        {TransactionKind::command, {0x60}},
        {TransactionKind::data, {0x02, 0x00}},
        {TransactionKind::command, {0x61}},
        {TransactionKind::data, {0x03, 0x20, 0x01, 0xE0}},
        {TransactionKind::command, {0x84}},
        {TransactionKind::data, {0x01}},
        {TransactionKind::command, {0xE3}},
        {TransactionKind::data, {0x2F}},
        {TransactionKind::command, {0x04}},
        {TransactionKind::command, {0x10}},
    };
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
        expected_prefix.size(),
        transport.transactions.size());
    for (std::size_t index = 0; index < expected_prefix.size(); ++index) {
        assert_transaction_equal(
            expected_prefix[index],
            transport.transactions[index]);
    }

    const std::size_t frame_command_index =
        find_command_transaction(transport, 0x10);
    TEST_ASSERT_EQUAL_UINT32(
        expected_prefix.size() - 1U,
        frame_command_index);
    std::size_t chunk_count = 0;
    std::size_t payload_bytes = 0;
    for (std::size_t index = frame_command_index + 1U;
         index < transport.transactions.size() &&
         transport.transactions[index].kind == TransactionKind::data;
         ++index) {
        const Transaction& chunk = transport.transactions[index];
        ++chunk_count;
        payload_bytes += chunk.bytes.size();
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(
            pf_display::kPanelTransactionBytes,
            chunk.bytes.size());
        TEST_ASSERT_EACH_EQUAL_HEX8(
            0x56,
            chunk.bytes.data(),
            chunk.bytes.size());
    }
    TEST_ASSERT_EQUAL_UINT32(47, chunk_count);
    TEST_ASSERT_EQUAL_UINT32(pf_display::kFullFramebufferBytes, payload_bytes);
    TEST_ASSERT_EQUAL_UINT32(72, transport.now_ms);

    const std::vector<Transaction> expected_suffix = {
        {TransactionKind::command, {0x04}},
        {TransactionKind::command, {0x06}},
        {TransactionKind::data, {0x6F, 0x1F, 0x17, 0x49}},
        {TransactionKind::command, {0x12}},
        {TransactionKind::data, {0x00}},
        {TransactionKind::command, {0x02}},
        {TransactionKind::data, {0x00}},
        {TransactionKind::command, {0x02}},
        {TransactionKind::data, {0x00}},
        {TransactionKind::command, {0x07}},
        {TransactionKind::data, {0xA5}},
    };
    const std::size_t suffix_start =
        frame_command_index + 1U + chunk_count;
    TEST_ASSERT_EQUAL_UINT32(
        suffix_start + expected_suffix.size(),
        transport.transactions.size());
    for (std::size_t index = 0; index < expected_suffix.size(); ++index) {
        assert_transaction_equal(
            expected_suffix[index],
            transport.transactions[suffix_start + index]);
    }
}

void test_initial_busy_timeout_aborts_before_commands_and_next_call_resets()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> frame{};
    FakeTransport transport;
    transport.busy_mode = BusyMode::stuck;
    Epd7in3eDriver driver{transport};

    const auto timeout = driver.refresh_and_sleep(frame.data(), frame.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStatus::busy_timeout),
        static_cast<int>(timeout.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStage::initial_busy),
        static_cast<int>(timeout.stage));
    TEST_ASSERT_TRUE(transport.transactions.empty());
    TEST_ASSERT_EQUAL_UINT32(60042, transport.now_ms);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PanelState::unknown),
        static_cast<int>(driver.state()));

    transport.busy_mode = BusyMode::idle;
    const auto recovered = driver.refresh_and_sleep(frame.data(), frame.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStatus::ok),
        static_cast<int>(recovered.status));
    TEST_ASSERT_EQUAL_UINT32(6, transport.reset_levels.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PanelState::deep_sleep),
        static_cast<int>(driver.state()));
}

void test_sleep_power_off_timeout_never_sends_deep_sleep()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> frame{};
    FakeTransport transport;
    transport.busy_mode = BusyMode::idle_then_stuck;
    transport.idle_waits_before_stuck = 5;
    Epd7in3eDriver driver{transport};

    const auto result = driver.refresh_and_sleep(frame.data(), frame.size());

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStatus::busy_timeout),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStage::sleep_power_off),
        static_cast<int>(result.stage));
    const std::vector<std::uint8_t> commands = transport.command_bytes();
    TEST_ASSERT_FALSE(
        std::find(commands.begin(), commands.end(), 0x07) != commands.end());
    TEST_ASSERT_EQUAL_HEX8(0x02, commands.back());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PanelState::unknown),
        static_cast<int>(driver.state()));
}

void test_frame_transport_failure_aborts_before_refresh()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> frame{};
    FakeTransport transport;
    transport.fail_data_length = pf_display::kPanelTransactionBytes;
    Epd7in3eDriver driver{transport};

    const auto result = driver.refresh_and_sleep(frame.data(), frame.size());

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStatus::transport_error),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStage::frame_write),
        static_cast<int>(result.stage));
    const std::vector<std::uint8_t> commands = transport.command_bytes();
    TEST_ASSERT_EQUAL_HEX8(0x10, commands.back());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(PanelState::unknown),
        static_cast<int>(driver.state()));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_fixed_transport_contract_matches_g2_and_g3);
    RUN_TEST(test_tick_conversion_guarantees_minimum_delays);
    RUN_TEST(test_invalid_frame_is_rejected_before_any_panel_io);
    RUN_TEST(test_successful_refresh_matches_upstream_trace_and_sleeps);
    RUN_TEST(test_initial_busy_timeout_aborts_before_commands_and_next_call_resets);
    RUN_TEST(test_sleep_power_off_timeout_never_sends_deep_sleep);
    RUN_TEST(test_frame_transport_failure_aborts_before_refresh);
    return UNITY_END();
}
