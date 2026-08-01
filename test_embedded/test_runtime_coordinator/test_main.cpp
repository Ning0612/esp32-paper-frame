#include <cstdint>
#include <initializer_list>

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_sensors/daily_stats.hpp"
#include "pf_sensors/environment_sensor.hpp"
#include "pf_sensors/light_sensor.hpp"
#include "pf_sensors/presence.hpp"
#include "pf_weather/weather.hpp"
#include "unity.h"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_runtime_queues_and_snapshot()
{
    pf_runtime::RuntimeCoordinator& runtime = pf_runtime::coordinator();
    pf_runtime::RuntimeSnapshot observed{};
    TEST_ASSERT_FALSE(runtime.read_snapshot(observed));

    const pf_runtime::RuntimeSnapshot initial{
        .sequence = 7,
        .flash = pf_runtime::ServiceState::ready,
        .psram = pf_runtime::ServiceState::degraded,
        .config = pf_runtime::ServiceState::ready,
        .webfs = pf_runtime::ServiceState::ready,
        .imagefs = pf_runtime::ServiceState::degraded,
        .wifi = pf_runtime::WifiState::unknown,
        .internet = pf_runtime::InternetState::unknown,
        .time_sync = pf_runtime::TimeSyncState::unsynced,
        .display = pf_runtime::DisplayState::unknown,
        .active_display_request_id = 0,
        .queued_display_count = 0,
        .last_display_request_id = 0,
        .last_display_outcome = pf_runtime::DisplayOutcome::none,
        .last_display_stage = 0,
    };
    TEST_ASSERT_EQUAL(ESP_OK, runtime.initialize(initial));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(initial.sequence, observed.sequence);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(initial.imagefs),
        static_cast<int>(observed.imagefs));

    pf_runtime::RuntimeSnapshot updated = initial;
    updated.sequence = 8;
    runtime.publish_snapshot(updated);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(updated.sequence, observed.sequence);

    runtime.update_network(
        pf_runtime::WifiState::provisioning,
        pf_runtime::InternetState::unknown);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::WifiState::provisioning),
        static_cast<int>(observed.wifi));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::InternetState::unknown),
        static_cast<int>(observed.internet));
    TEST_ASSERT_EQUAL_UINT32(updated.sequence + 1U, observed.sequence);

    runtime.update_time_sync(pf_runtime::TimeSyncState::synced);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::TimeSyncState::synced),
        static_cast<int>(observed.time_sync));

    pf_weather::Cache weather{};
    weather.has_observation = true;
    weather.observation.temperature = 21.5F;
    weather.last_success_epoch_s = 1700000000U;
    runtime.update_weather(weather, "metric");
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_TRUE(observed.weather.has_observation);
    TEST_ASSERT_EQUAL_FLOAT(
        21.5F, observed.weather.observation.temperature);
    TEST_ASSERT_EQUAL_STRING("metric", observed.weather_units);

    pf_sensors::EnvironmentCache environment{};
    environment.has_reading = true;
    environment.reading.temperature_c = 24.4F;
    pf_sensors::DailyStats environment_daily{};
    pf_sensors::record_daily_reading(
        environment_daily, environment.reading, 1700000000U);
    runtime.update_environment(
        environment, pf_sensors::SensorStatus::online, environment_daily);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_TRUE(observed.environment.has_reading);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::SensorStatus::online),
        static_cast<int>(observed.environment_status));
    TEST_ASSERT_EQUAL_FLOAT(
        24.4F, observed.environment.reading.temperature_c);
    TEST_ASSERT_EQUAL_UINT32(
        1U, observed.environment_daily.temperature_c.sample_count);

    runtime.update_light_and_presence(
        pf_sensors::LightSensorStatus::online,
        1234U,
        2000U,
        pf_sensors::PresenceState::present);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::LightSensorStatus::online),
        static_cast<int>(observed.light_status));
    TEST_ASSERT_EQUAL_UINT16(1234U, observed.light_raw_filtered);
    TEST_ASSERT_EQUAL_UINT16(2000U, observed.light_threshold);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_sensors::PresenceState::present),
        static_cast<int>(observed.presence));

    const std::uint32_t sequence_before_manual_activate = observed.sequence;
    const std::uint32_t request_id_before_manual_activate =
        observed.manual_activate_request_id;
    runtime.request_manual_carousel_activation(42U);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(42U, observed.manual_activate_image_id);
    TEST_ASSERT_EQUAL_UINT32(
        request_id_before_manual_activate + 1U,
        observed.manual_activate_request_id);
    TEST_ASSERT_EQUAL_UINT32(
        sequence_before_manual_activate + 1U, observed.sequence);

    // Re-activating the same image id must still bump request_id: callers
    // detect "new request" by request_id, not by whether image_id changed.
    runtime.request_manual_carousel_activation(42U);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(
        request_id_before_manual_activate + 2U,
        observed.manual_activate_request_id);

    for (std::uint32_t index = 0; index < 4; ++index) {
        const pf_runtime::RuntimeCommand command{
            .request_id = index,
            .kind = pf_runtime::CommandKind::refresh_display,
            .frame = pf_runtime::FrameToken{
                static_cast<std::uint8_t>(index % 2U),
                index + 1U,
            },
        };
        TEST_ASSERT_TRUE(runtime.try_submit_command(command));
    }
    const pf_runtime::RuntimeCommand overflow_command{
        .request_id = 4,
        .kind = pf_runtime::CommandKind::refresh_display,
        .frame = pf_runtime::FrameToken{0, 5},
    };
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    const std::uint32_t sequence_before_queue_full = observed.sequence;
    TEST_ASSERT_FALSE(runtime.try_submit_command(overflow_command));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::queued),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(0, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(4, observed.queued_display_count);
    TEST_ASSERT_EQUAL_UINT32(1U, observed.command_queue_rejected_count);
    // The counter increment and its diagnostic event are published as one
    // atomic snapshot change (see record_diagnostic_event_locked): exactly
    // one sequence bump for this whole failure, not two.
    TEST_ASSERT_EQUAL_UINT32(
        sequence_before_queue_full + 1U, observed.sequence);

    for (std::uint32_t index = 0; index < 4; ++index) {
        pf_runtime::RuntimeCommand command{};
        TEST_ASSERT_TRUE(runtime.try_receive_command(command));
        TEST_ASSERT_EQUAL_UINT32(index, command.request_id);
    }

    runtime.update_display_started(0);
    const pf_runtime::RuntimeCommand queued_behind_zero{
        .request_id = 99,
        .kind = pf_runtime::CommandKind::refresh_display,
        .frame = pf_runtime::FrameToken{0, 100},
    };
    TEST_ASSERT_TRUE(runtime.try_submit_command(queued_behind_zero));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::refreshing),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(0, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(4, observed.queued_display_count);
    pf_runtime::RuntimeCommand received_behind_zero{};
    TEST_ASSERT_TRUE(runtime.try_receive_command(received_behind_zero));
    TEST_ASSERT_EQUAL_UINT32(99, received_behind_zero.request_id);
    runtime.update_display_finished(
        0,
        pf_runtime::DisplayOutcome::busy_timeout,
        7);
    const pf_runtime::RuntimeResult zero_result{
        .request_id = 0,
        .status = pf_runtime::ResultStatus::failed,
        .error = pf_runtime::RuntimeError::timeout,
        .display_outcome = pf_runtime::DisplayOutcome::busy_timeout,
        .driver_stage = 7,
    };
    TEST_ASSERT_TRUE(runtime.retain_terminal_result(zero_result));
    pf_runtime::RuntimeResult released_terminal{};
    TEST_ASSERT_TRUE(
        runtime.try_take_terminal_result(0, released_terminal));

    TEST_ASSERT_TRUE(runtime.lock_flash_display(0));
    TEST_ASSERT_FALSE(runtime.lock_flash_display(0));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    // A zero-wait probe failing is routine HTTP-handler backoff and must
    // not be counted as lock contention.
    TEST_ASSERT_EQUAL_UINT32(0U, observed.flash_display_lock_timeout_count);
    // A genuine timed wait that still fails (lock already held by this
    // same task, so it cannot be released mid-wait) must be counted.
    TEST_ASSERT_FALSE(runtime.lock_flash_display(pdMS_TO_TICKS(10)));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(1U, observed.flash_display_lock_timeout_count);
    runtime.unlock_flash_display();
    TEST_ASSERT_TRUE(runtime.lock_flash_display(0));
    runtime.unlock_flash_display();

    runtime.update_display_started(3);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::refreshing),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(3, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(3, observed.queued_display_count);

    runtime.update_display_finished(
        3,
        pf_runtime::DisplayOutcome::busy_timeout,
        7);
    const pf_runtime::RuntimeResult three_result{
        .request_id = 3,
        .status = pf_runtime::ResultStatus::failed,
        .error = pf_runtime::RuntimeError::timeout,
        .display_outcome = pf_runtime::DisplayOutcome::busy_timeout,
        .driver_stage = 7,
    };
    TEST_ASSERT_TRUE(runtime.retain_terminal_result(three_result));
    TEST_ASSERT_TRUE(
        runtime.try_take_terminal_result(3, released_terminal));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::queued),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(0, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT32(3, observed.last_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(3, observed.queued_display_count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayOutcome::busy_timeout),
        static_cast<int>(observed.last_display_outcome));
    TEST_ASSERT_EQUAL_UINT8(7, observed.last_display_stage);

    for (std::uint32_t index = 0; index < 4; ++index) {
        const pf_runtime::RuntimeResult result{
            .request_id = index,
            .status = pf_runtime::ResultStatus::completed,
            .error = pf_runtime::RuntimeError::none,
            .display_outcome =
                pf_runtime::DisplayOutcome::refreshed_and_slept,
            .driver_stage = 10,
        };
        TEST_ASSERT_TRUE(runtime.try_publish_result(result));
    }
    const pf_runtime::RuntimeResult overflow_result{
        .request_id = 4,
        .status = pf_runtime::ResultStatus::failed,
        .error = pf_runtime::RuntimeError::transport,
        .display_outcome = pf_runtime::DisplayOutcome::transport_error,
        .driver_stage = 5,
    };
    TEST_ASSERT_FALSE(runtime.try_publish_result(overflow_result));

    for (std::uint32_t index = 0; index < 4; ++index) {
        pf_runtime::RuntimeResult result{};
        TEST_ASSERT_TRUE(runtime.try_receive_result(result));
        TEST_ASSERT_EQUAL_UINT32(index, result.request_id);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(
                pf_runtime::DisplayOutcome::refreshed_and_slept),
            static_cast<int>(result.display_outcome));
    }

    for (const std::uint32_t request_id : {1U, 2U, 99U}) {
        const pf_runtime::RuntimeResult cleanup{
            .request_id = request_id,
            .status = pf_runtime::ResultStatus::failed,
            .error = pf_runtime::RuntimeError::invalid_state,
            .display_outcome =
                pf_runtime::DisplayOutcome::invalid_lease,
            .driver_stage = 0,
        };
        TEST_ASSERT_TRUE(runtime.retain_terminal_result(cleanup));
        TEST_ASSERT_TRUE(
            runtime.try_take_terminal_result(
                request_id,
                released_terminal));
    }

    const std::uint32_t first_request_id =
        runtime.allocate_request_id();
    const std::uint32_t second_request_id =
        runtime.allocate_request_id();
    TEST_ASSERT_NOT_EQUAL(0, first_request_id);
    TEST_ASSERT_EQUAL_UINT32(
        first_request_id + 1U,
        second_request_id);

    for (const std::uint32_t request_id :
         {first_request_id, second_request_id}) {
        const pf_runtime::RuntimeCommand command{
            .request_id = request_id,
            .kind = pf_runtime::CommandKind::refresh_display,
            .frame = pf_runtime::FrameToken{0, request_id},
        };
        TEST_ASSERT_TRUE(runtime.try_submit_command(command));
        pf_runtime::RuntimeCommand received{};
        TEST_ASSERT_TRUE(runtime.try_receive_command(received));
        TEST_ASSERT_EQUAL_UINT32(request_id, received.request_id);
        runtime.update_display_started(request_id);
    }

    const pf_runtime::RuntimeResult retained_first{
        .request_id = first_request_id,
        .status = pf_runtime::ResultStatus::completed,
        .error = pf_runtime::RuntimeError::none,
        .display_outcome =
            pf_runtime::DisplayOutcome::refreshed_and_slept,
        .driver_stage = 10,
    };
    const pf_runtime::RuntimeResult retained_second{
        .request_id = second_request_id,
        .status = pf_runtime::ResultStatus::failed,
        .error = pf_runtime::RuntimeError::transport,
        .display_outcome =
            pf_runtime::DisplayOutcome::transport_error,
        .driver_stage = 5,
    };
    TEST_ASSERT_TRUE(
        runtime.retain_terminal_result(retained_first));
    TEST_ASSERT_TRUE(
        runtime.retain_terminal_result(retained_second));

    pf_runtime::RuntimeResult terminal{};
    TEST_ASSERT_FALSE(
        runtime.try_take_terminal_result(0xFFFFFFFFU, terminal));
    TEST_ASSERT_TRUE(
        runtime.try_take_terminal_result(first_request_id, terminal));
    TEST_ASSERT_EQUAL_UINT32(first_request_id, terminal.request_id);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_runtime::DisplayOutcome::refreshed_and_slept),
        static_cast<int>(terminal.display_outcome));
    TEST_ASSERT_FALSE(
        runtime.try_take_terminal_result(first_request_id, terminal));
    TEST_ASSERT_TRUE(
        runtime.try_take_terminal_result(second_request_id, terminal));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_runtime::DisplayOutcome::transport_error),
        static_cast<int>(terminal.display_outcome));

    std::uint32_t reserved_ids[8]{};
    for (std::uint32_t index = 0; index < 8U; ++index) {
        reserved_ids[index] = 1000U + index;
        const pf_runtime::RuntimeCommand command{
            .request_id = reserved_ids[index],
            .kind = pf_runtime::CommandKind::refresh_display,
            .frame = pf_runtime::FrameToken{
                static_cast<std::uint8_t>(index % 2U),
                index + 1U,
            },
        };
        TEST_ASSERT_TRUE(runtime.try_submit_command(command));
        pf_runtime::RuntimeCommand received{};
        TEST_ASSERT_TRUE(runtime.try_receive_command(received));
        runtime.update_display_started(received.request_id);
    }
    const pf_runtime::RuntimeCommand no_terminal_slot{
        .request_id = 2000U,
        .kind = pf_runtime::CommandKind::refresh_display,
        .frame = pf_runtime::FrameToken{0, 1},
    };
    TEST_ASSERT_FALSE(runtime.try_submit_command(no_terminal_slot));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(1U, observed.terminal_result_exhausted_count);

    for (const std::uint32_t request_id : reserved_ids) {
        const pf_runtime::RuntimeResult completed{
            .request_id = request_id,
            .status = pf_runtime::ResultStatus::completed,
            .error = pf_runtime::RuntimeError::none,
            .display_outcome =
                pf_runtime::DisplayOutcome::refreshed_and_slept,
            .driver_stage = 10,
        };
        TEST_ASSERT_TRUE(runtime.retain_terminal_result(completed));
        TEST_ASSERT_TRUE(
            runtime.try_take_terminal_result(request_id, terminal));
        runtime.update_display_finished(
            request_id,
            completed.display_outcome,
            completed.driver_stage);
    }

    TEST_ASSERT_TRUE(runtime.try_submit_command(no_terminal_slot));
    pf_runtime::RuntimeCommand received_after_release{};
    TEST_ASSERT_TRUE(
        runtime.try_receive_command(received_after_release));
    const pf_runtime::RuntimeResult after_release{
        .request_id = no_terminal_slot.request_id,
        .status = pf_runtime::ResultStatus::failed,
        .error = pf_runtime::RuntimeError::invalid_state,
        .display_outcome =
            pf_runtime::DisplayOutcome::invalid_lease,
        .driver_stage = 0,
    };
    TEST_ASSERT_TRUE(
        runtime.retain_terminal_result(after_release));
    TEST_ASSERT_TRUE(
        runtime.try_take_terminal_result(
            no_terminal_slot.request_id,
            terminal));

    // Round-trip real diagnostic events recorded above (command queue full,
    // flash/display lock timeout, terminal result exhausted) through the
    // real coordinator, not just the pure ring-buffer logic covered by the
    // host test.
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
        3U, observed.diagnostics_latest_sequence_id);
    pf_runtime::DiagnosticEvent events[pf_runtime::kDiagnosticsRingCapacity]{};
    pf_runtime::DiagnosticsReadResult read_result{};
    runtime.read_diagnostics_since(
        0U, events, pf_runtime::kDiagnosticsRingCapacity, read_result);
    TEST_ASSERT_EQUAL_UINT32(
        observed.diagnostics_latest_sequence_id,
        read_result.highest_sequence_id);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(3U, read_result.count);
    for (std::size_t index = 1U; index < read_result.count; ++index) {
        // Oldest-first: sequence ids must be strictly increasing.
        TEST_ASSERT_TRUE(
            events[index].sequence_id > events[index - 1U].sequence_id);
    }
}

}  // namespace

extern "C" void app_main()
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    UNITY_BEGIN();
    RUN_TEST(test_runtime_queues_and_snapshot);
    UNITY_END();

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
