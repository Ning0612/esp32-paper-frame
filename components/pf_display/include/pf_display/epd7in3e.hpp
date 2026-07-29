#pragma once

// The command sequence in this file is adapted from Waveshare's
// EPD_7in3e.c at commit 06e834491bf62023a1b86a481b4530978883d2c4.
//
// Original author: Waveshare team
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "pf_display/packed_framebuffer.hpp"

namespace pf_display {

inline constexpr int kPanelMosiGpio = 11;
inline constexpr int kPanelClockGpio = 12;
inline constexpr int kPanelChipSelectGpio = 10;
inline constexpr int kPanelDataCommandGpio = 13;
inline constexpr int kPanelResetGpio = 14;
inline constexpr int kPanelBusyGpio = 4;

inline constexpr std::uint32_t kPanelSpiClockHz = 2'000'000U;
inline constexpr std::size_t kPanelTransactionBytes = 4096U;
inline constexpr std::uint32_t kBusyTimeoutMs = 60'000U;
inline constexpr std::uint32_t kBusyPollIntervalMs = 5U;

namespace detail {

constexpr std::uint64_t ticks_to_wait_at_least(
    const std::uint32_t duration_ms,
    const std::uint32_t tick_period_ms)
{
    if (duration_ms == 0U || tick_period_ms == 0U) {
        return 0U;
    }
    const std::uint64_t rounded_up =
        (duration_ms / tick_period_ms) +
        ((duration_ms % tick_period_ms) == 0U ? 0U : 1U);
    // vTaskDelay() wakes on tick boundaries, so one additional tick is needed
    // to guarantee the elapsed wall time is not shorter than duration_ms.
    return rounded_up + 1U;
}

}  // namespace detail

enum class DriverStatus : std::uint8_t {
    ok,
    invalid_frame,
    transport_error,
    busy_timeout,
};

enum class DriverStage : std::uint8_t {
    validate,
    reset,
    initial_busy,
    register_init,
    init_power_on,
    frame_write,
    refresh_power_on,
    refresh,
    refresh_power_off,
    sleep_power_off,
    deep_sleep,
};

enum class PanelState : std::uint8_t {
    unknown,
    active,
    deep_sleep,
};

struct DriverResult {
    DriverStatus status;
    DriverStage stage;

    constexpr bool succeeded() const
    {
        return status == DriverStatus::ok;
    }
};

class PanelTransport {
public:
    virtual ~PanelTransport() = default;

    // Each write method is one complete hardware-CS transaction. It must
    // return only after CS has been deasserted and the SPI transaction ended.
    virtual bool set_reset_level(bool high) = 0;
    virtual bool write_command_transaction(std::uint8_t command) = 0;
    virtual bool write_data_transaction(
        const std::uint8_t* data,
        std::size_t length) = 0;
    virtual bool busy_is_idle(bool& idle) = 0;
    virtual std::uint64_t monotonic_ms() const = 0;
    virtual void delay_ms(std::uint32_t duration_ms) = 0;
};

class Epd7in3eDriver {
public:
    explicit Epd7in3eDriver(PanelTransport& transport)
        : transport_(transport)
    {
    }

    // This is a synchronous, full-panel lifecycle. Every call starts with
    // reset + complete initialization and ends in deep sleep on success.
    //
    // The caller retains ownership of exactly 192,000 readable bytes and must
    // not mutate them or start a cache-disabling flash operation until this
    // method returns. The driver does not retain the pointer after return.
    DriverResult refresh_and_sleep(
        const std::uint8_t* const frame,
        const std::size_t length)
    {
        if (frame == nullptr || length != kFullFramebufferBytes) {
            return {DriverStatus::invalid_frame, DriverStage::validate};
        }

        state_ = PanelState::unknown;
        DriverResult result = reset_and_initialize();
        if (!result.succeeded()) {
            return result;
        }

        if (!send_command(0x10U)) {
            return transport_failure(DriverStage::frame_write);
        }
        for (std::size_t offset = 0; offset < length;
             offset += kPanelTransactionBytes) {
            const std::size_t chunk =
                std::min(kPanelTransactionBytes, length - offset);
            if (!transport_.write_data_transaction(frame + offset, chunk)) {
                return transport_failure(DriverStage::frame_write);
            }
        }

        if (!send_command(0x04U)) {
            return transport_failure(DriverStage::refresh_power_on);
        }
        result = wait_until_idle(DriverStage::refresh_power_on);
        if (!result.succeeded()) {
            return result;
        }

        constexpr std::uint8_t kSecondSetting[] = {
            0x6FU,
            0x1FU,
            0x17U,
            0x49U,
        };
        if (!send_command_with_data(0x06U, kSecondSetting)) {
            return transport_failure(DriverStage::refresh);
        }

        constexpr std::uint8_t kZero[] = {0x00U};
        if (!send_command_with_data(0x12U, kZero)) {
            return transport_failure(DriverStage::refresh);
        }
        result = wait_until_idle(DriverStage::refresh);
        if (!result.succeeded()) {
            return result;
        }

        if (!send_command_with_data(0x02U, kZero)) {
            return transport_failure(DriverStage::refresh_power_off);
        }
        result = wait_until_idle(DriverStage::refresh_power_off);
        if (!result.succeeded()) {
            return result;
        }

        if (!send_command_with_data(0x02U, kZero)) {
            return transport_failure(DriverStage::sleep_power_off);
        }
        result = wait_until_idle(DriverStage::sleep_power_off);
        if (!result.succeeded()) {
            return result;
        }

        constexpr std::uint8_t kDeepSleepCheckCode[] = {0xA5U};
        if (!send_command_with_data(0x07U, kDeepSleepCheckCode)) {
            return transport_failure(DriverStage::deep_sleep);
        }

        state_ = PanelState::deep_sleep;
        return {DriverStatus::ok, DriverStage::deep_sleep};
    }

    PanelState state() const
    {
        return state_;
    }

private:
    DriverResult reset_and_initialize()
    {
        if (!transport_.set_reset_level(true)) {
            return transport_failure(DriverStage::reset);
        }
        transport_.delay_ms(20U);
        if (!transport_.set_reset_level(false)) {
            return transport_failure(DriverStage::reset);
        }
        transport_.delay_ms(2U);
        if (!transport_.set_reset_level(true)) {
            return transport_failure(DriverStage::reset);
        }
        transport_.delay_ms(20U);

        DriverResult result = wait_until_idle(DriverStage::initial_busy);
        if (!result.succeeded()) {
            return result;
        }
        transport_.delay_ms(30U);

        constexpr std::uint8_t kCmdh[] = {
            0x49U,
            0x55U,
            0x20U,
            0x08U,
            0x09U,
            0x18U,
        };
        constexpr std::uint8_t kPowerSetting[] = {0x3FU};
        constexpr std::uint8_t kPanelSetting[] = {0x5FU, 0x69U};
        constexpr std::uint8_t kPowerOffSequence[] = {
            0x00U,
            0x54U,
            0x00U,
            0x44U,
        };
        constexpr std::uint8_t kPowerOnSequence[] = {
            0x40U,
            0x1FU,
            0x1FU,
            0x2CU,
        };
        constexpr std::uint8_t kBoosterSoftStart[] = {
            0x6FU,
            0x1FU,
            0x17U,
            0x49U,
        };
        constexpr std::uint8_t kBoosterSoftStart2[] = {
            0x6FU,
            0x1FU,
            0x1FU,
            0x22U,
        };
        constexpr std::uint8_t kPllControl[] = {0x03U};
        constexpr std::uint8_t kVcomAndDataInterval[] = {0x3FU};
        constexpr std::uint8_t kTconSetting[] = {0x02U, 0x00U};
        constexpr std::uint8_t kResolution[] = {
            0x03U,
            0x20U,
            0x01U,
            0xE0U,
        };
        constexpr std::uint8_t kTconControl[] = {0x01U};
        constexpr std::uint8_t kPowerSaving[] = {0x2FU};

        const bool initialized =
            send_command_with_data(0xAAU, kCmdh) &&
            send_command_with_data(0x01U, kPowerSetting) &&
            send_command_with_data(0x00U, kPanelSetting) &&
            send_command_with_data(0x03U, kPowerOffSequence) &&
            send_command_with_data(0x05U, kPowerOnSequence) &&
            send_command_with_data(0x06U, kBoosterSoftStart) &&
            send_command_with_data(0x08U, kBoosterSoftStart2) &&
            send_command_with_data(0x30U, kPllControl) &&
            send_command_with_data(0x50U, kVcomAndDataInterval) &&
            send_command_with_data(0x60U, kTconSetting) &&
            send_command_with_data(0x61U, kResolution) &&
            send_command_with_data(0x84U, kTconControl) &&
            send_command_with_data(0xE3U, kPowerSaving);
        if (!initialized || !send_command(0x04U)) {
            return transport_failure(
                initialized ? DriverStage::init_power_on
                            : DriverStage::register_init);
        }

        result = wait_until_idle(DriverStage::init_power_on);
        if (!result.succeeded()) {
            return result;
        }
        state_ = PanelState::active;
        return {DriverStatus::ok, DriverStage::init_power_on};
    }

    bool send_command(const std::uint8_t command)
    {
        return transport_.write_command_transaction(command);
    }

    template <std::size_t Length>
    bool send_command_with_data(
        const std::uint8_t command,
        const std::uint8_t (&data)[Length])
    {
        static_assert(Length > 0U);
        return send_command(command) &&
               transport_.write_data_transaction(data, Length);
    }

    DriverResult wait_until_idle(const DriverStage stage)
    {
        const std::uint64_t started_at = transport_.monotonic_ms();
        while (true) {
            bool idle = false;
            if (!transport_.busy_is_idle(idle)) {
                return transport_failure(stage);
            }
            if (idle) {
                return {DriverStatus::ok, stage};
            }

            const std::uint64_t elapsed =
                transport_.monotonic_ms() - started_at;
            if (elapsed >= kBusyTimeoutMs) {
                return failure(DriverStatus::busy_timeout, stage);
            }
            transport_.delay_ms(kBusyPollIntervalMs);
        }
    }

    DriverResult transport_failure(const DriverStage stage)
    {
        return failure(DriverStatus::transport_error, stage);
    }

    DriverResult failure(
        const DriverStatus status,
        const DriverStage stage)
    {
        state_ = PanelState::unknown;
        return {status, stage};
    }

    PanelTransport& transport_;
    PanelState state_ = PanelState::unknown;
};

}  // namespace pf_display
