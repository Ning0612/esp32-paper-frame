#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include <unity.h>

#include "pf_display/display_task.hpp"
#include "pf_web/health_serializer.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_display::DisplayCommandProcessor;
using pf_display::DisplayCommandPublisher;
using pf_display::DisplayFramePool;
using pf_display::DisplayPanel;
using pf_display::DisplaySubmitter;
using pf_display::DriverResult;
using pf_display::DriverStage;
using pf_display::DriverStatus;
using pf_display::PanelState;
using pf_display::SubmitStatus;
using pf_runtime::CommandKind;
using pf_runtime::DisplayOutcome;
using pf_runtime::DisplayState;
using pf_runtime::FrameToken;
using pf_runtime::ResultStatus;
using pf_runtime::RuntimeCommand;
using pf_runtime::RuntimeError;
using pf_runtime::RuntimeResult;

class FakePublisher final : public DisplayCommandPublisher {
public:
    bool try_publish(const RuntimeCommand& command) override
    {
        ++publish_count;
        last_command = command;
        return accept;
    }

    bool accept = true;
    std::size_t publish_count = 0;
    RuntimeCommand last_command{};
};

class FakePanel final : public DisplayPanel {
public:
    DriverResult refresh_and_sleep(
        const std::uint8_t* frame,
        const std::size_t length) override
    {
        ++refresh_count;
        last_frame = frame;
        last_length = length;
        if (block) {
            entered.store(true);
            while (!release.load()) {
                std::this_thread::yield();
            }
        }
        state_value = result.status == DriverStatus::ok
                          ? success_state
                          : PanelState::unknown;
        return result;
    }

    PanelState state() const override
    {
        return state_value;
    }

    DriverResult result{DriverStatus::ok, DriverStage::deep_sleep};
    PanelState success_state = PanelState::deep_sleep;
    PanelState state_value = PanelState::unknown;
    bool block = false;
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::size_t refresh_count = 0;
    const std::uint8_t* last_frame = nullptr;
    std::size_t last_length = 0;
};

pf_runtime::RuntimeSnapshot ready_snapshot()
{
    return {
        .sequence = 12,
        .flash = pf_runtime::ServiceState::ready,
        .psram = pf_runtime::ServiceState::ready,
        .config = pf_runtime::ServiceState::ready,
        .imagefs = pf_runtime::ServiceState::ready,
        .display = DisplayState::refreshing,
        .active_display_request_id = 17,
        .queued_display_count = 1,
        .last_display_request_id = 0,
        .last_display_outcome = DisplayOutcome::none,
        .last_display_stage = 0,
    };
}

void test_frame_leases_transfer_ownership_and_reject_stale_tokens()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> first{};
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> second{};
    DisplayFramePool pool;
    TEST_ASSERT_TRUE(pool.initialize(first.data(), second.data()));

    auto first_writer = pool.try_acquire_write();
    auto second_writer = pool.try_acquire_write();
    TEST_ASSERT_TRUE(first_writer.valid());
    TEST_ASSERT_TRUE(second_writer.valid());
    TEST_ASSERT_FALSE(pool.try_acquire_write().valid());
    TEST_ASSERT_NOT_EQUAL(first_writer.token().slot, second_writer.token().slot);

    const FrameToken first_token = first_writer.token();
    first_writer.data()[0] = 0x56;
    FakePublisher publisher;
    DisplaySubmitter submitter{pool, publisher};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SubmitStatus::accepted),
        static_cast<int>(submitter.try_submit(41, first_writer)));
    TEST_ASSERT_FALSE(first_writer.valid());
    TEST_ASSERT_EQUAL_UINT32(1, publisher.publish_count);
    TEST_ASSERT_EQUAL_UINT32(41, publisher.last_command.request_id);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CommandKind::refresh_display),
        static_cast<int>(publisher.last_command.kind));
    TEST_ASSERT_EQUAL_UINT8(first_token.slot, publisher.last_command.frame.slot);
    TEST_ASSERT_EQUAL_UINT32(
        first_token.generation,
        publisher.last_command.frame.generation);

    auto reader = pool.try_begin_display(publisher.last_command.frame);
    TEST_ASSERT_TRUE(reader.valid());
    TEST_ASSERT_EQUAL_HEX8(0x56, reader.data()[0]);
    TEST_ASSERT_FALSE(pool.try_begin_display(first_token).valid());
    reader.release();

    auto reused_writer = pool.try_acquire_write();
    TEST_ASSERT_TRUE(reused_writer.valid());
    TEST_ASSERT_EQUAL_UINT8(first_token.slot, reused_writer.token().slot);
    TEST_ASSERT_NOT_EQUAL(
        first_token.generation,
        reused_writer.token().generation);
    TEST_ASSERT_FALSE(pool.try_begin_display(first_token).valid());
}

void test_queue_rejection_restores_the_write_lease_without_panel_io()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> first{};
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> second{};
    DisplayFramePool pool;
    TEST_ASSERT_TRUE(pool.initialize(first.data(), second.data()));
    auto writer = pool.try_acquire_write();
    const FrameToken token = writer.token();

    FakePublisher publisher;
    publisher.accept = false;
    DisplaySubmitter submitter{pool, publisher};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SubmitStatus::queue_full),
        static_cast<int>(submitter.try_submit(7, writer)));
    TEST_ASSERT_TRUE(writer.valid());
    TEST_ASSERT_EQUAL_UINT8(token.slot, writer.token().slot);
    TEST_ASSERT_EQUAL_UINT32(token.generation, writer.token().generation);

    FakePanel panel;
    TEST_ASSERT_EQUAL_UINT32(0, panel.refresh_count);
}

void test_pool_reset_requires_all_leases_to_be_released()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> first{};
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> second{};
    DisplayFramePool pool;
    TEST_ASSERT_TRUE(pool.initialize(first.data(), second.data()));
    auto writer = pool.try_acquire_write();
    TEST_ASSERT_TRUE(writer.valid());
    TEST_ASSERT_FALSE(pool.reset());

    writer.release();
    TEST_ASSERT_TRUE(pool.reset());
    TEST_ASSERT_FALSE(pool.try_acquire_write().valid());
    TEST_ASSERT_TRUE(pool.initialize(first.data(), second.data()));
    TEST_ASSERT_TRUE(pool.try_acquire_write().valid());
}

void test_processor_maps_success_only_after_deep_sleep()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> frame{};
    FakePanel panel;
    DisplayCommandProcessor processor{panel};
    const RuntimeCommand command{
        .request_id = 99,
        .kind = CommandKind::refresh_display,
        .frame = FrameToken{0, 1},
    };

    RuntimeResult result =
        processor.process(command, frame.data(), frame.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ResultStatus::completed),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RuntimeError::none),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayOutcome::refreshed_and_slept),
        static_cast<int>(result.display_outcome));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DriverStage::deep_sleep),
        result.driver_stage);
    TEST_ASSERT_EQUAL_UINT32(1, panel.refresh_count);

    panel.success_state = PanelState::active;
    result = processor.process(command, frame.data(), frame.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ResultStatus::failed),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RuntimeError::invalid_state),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayOutcome::panel_state_error),
        static_cast<int>(result.display_outcome));
}

void test_processor_maps_timeout_and_transport_failure()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> frame{};
    FakePanel panel;
    DisplayCommandProcessor processor{panel};
    const RuntimeCommand command{
        .request_id = 5,
        .kind = CommandKind::refresh_display,
        .frame = FrameToken{1, 8},
    };

    panel.result = {
        DriverStatus::busy_timeout,
        DriverStage::refresh,
    };
    RuntimeResult result =
        processor.process(command, frame.data(), frame.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ResultStatus::failed),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RuntimeError::timeout),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayOutcome::busy_timeout),
        static_cast<int>(result.display_outcome));

    panel.result = {
        DriverStatus::transport_error,
        DriverStage::frame_write,
    };
    result = processor.process(command, frame.data(), frame.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RuntimeError::transport),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayOutcome::transport_error),
        static_cast<int>(result.display_outcome));
}

void test_blocked_display_worker_does_not_block_health_serialization()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> frame{};
    FakePanel panel;
    panel.block = true;
    DisplayCommandProcessor processor{panel};
    const RuntimeCommand command{
        .request_id = 17,
        .kind = CommandKind::refresh_display,
        .frame = FrameToken{0, 4},
    };

    RuntimeResult display_result{};
    std::thread display_worker([&] {
        display_result =
            processor.process(command, frame.data(), frame.size());
    });
    while (!panel.entered.load()) {
        std::this_thread::yield();
    }

    char output[384]{};
    const pf_web::SerializeResult health =
        pf_web::serialize_health(
            ready_snapshot(),
            true,
            123,
            output,
            sizeof(output));
    TEST_ASSERT_TRUE(health.ok);
    TEST_ASSERT_NOT_NULL(output);

    panel.release.store(true);
    display_worker.join();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ResultStatus::completed),
        static_cast<int>(display_result.status));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_leases_transfer_ownership_and_reject_stale_tokens);
    RUN_TEST(test_queue_rejection_restores_the_write_lease_without_panel_io);
    RUN_TEST(test_pool_reset_requires_all_leases_to_be_released);
    RUN_TEST(test_processor_maps_success_only_after_deep_sleep);
    RUN_TEST(test_processor_maps_timeout_and_transport_failure);
    RUN_TEST(test_blocked_display_worker_does_not_block_health_serialization);
    return UNITY_END();
}
