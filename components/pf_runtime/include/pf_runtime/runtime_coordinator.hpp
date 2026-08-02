#pragma once

#include <cstdint>
#include <type_traits>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "pf_runtime/diagnostics_event.hpp"
#include "pf_runtime/runtime_messages.hpp"
#include "pf_runtime/runtime_snapshot.hpp"

namespace pf_runtime {

static_assert(std::is_trivially_copyable_v<RuntimeSnapshot>);

class RuntimeCoordinator {
public:
    esp_err_t initialize(const RuntimeSnapshot& initial_snapshot);

    bool read_snapshot(RuntimeSnapshot& destination) const;
    void publish_snapshot(const RuntimeSnapshot& snapshot);

    bool try_submit_command(const RuntimeCommand& command);
    bool try_receive_command(RuntimeCommand& command);
    bool receive_command(RuntimeCommand& command, TickType_t wait_ticks);
    bool try_publish_result(const RuntimeResult& result);
    bool try_receive_result(RuntimeResult& result);
    bool retain_terminal_result(const RuntimeResult& result);
    bool try_take_terminal_result(
        std::uint32_t request_id,
        RuntimeResult& result);
    std::uint32_t allocate_request_id();

    void update_network(
        WifiState wifi,
        InternetState internet,
        const char* ip_address = nullptr);
    void update_time_sync(TimeSyncState time_sync);
    void update_weather(
        const pf_weather::Cache& weather,
        const char* units);
    void update_environment(
        const pf_sensors::EnvironmentCache& environment,
        pf_sensors::SensorStatus environment_status,
        const pf_sensors::DailyStats& environment_daily);
    void update_light_and_presence(
        pf_sensors::LightSensorStatus light_status,
        std::uint16_t light_raw_filtered,
        std::uint16_t light_threshold,
        pf_sensors::PresenceState presence);
    void update_display_started(std::uint32_t request_id);
    void update_display_finished(
        std::uint32_t request_id,
        DisplayOutcome outcome,
        std::uint8_t driver_stage);
    void request_manual_carousel_activation(std::uint32_t image_id);
    // Split into two setters (rather than one combined one) so a
    // check-for-update publish can never clobber in-progress update_state/
    // progress/error fields, and vice versa -- the two operations run at
    // different times and must not stomp on each other's last-known state.
    void update_ota_check_status(
        OtaCheckState check_state,
        const char* latest_version,
        std::uint64_t last_check_epoch_s);
    void update_ota_update_status(
        OtaUpdateState update_state,
        std::uint8_t progress_percent,
        const char* last_error);
    void update_carousel_status(
        std::uint32_t current_image_id,
        std::uint64_t next_due_ms);
    void request_carousel_mode(bool random);
    // Re-publishes the imagefs partition's live used-byte count. Callers
    // must recompute this from a fresh esp_littlefs_info()/free_bytes()
    // query after a capacity-changing storage mutation (upload/remove) --
    // the boot-time value captured in the initial snapshot is never
    // otherwise refreshed, so it goes stale the moment the image library
    // changes.
    //
    // `generation` must be the pf_storage::Catalog generation the sample
    // was derived from (pf_storage::ImageStoreResult::catalog_generation).
    // Publish order across independent mutation/upload tasks is not
    // guaranteed once StorageWorker's own per-mutation serialization ends,
    // so a slower caller's older sample could otherwise land after a
    // faster caller's newer one and silently overwrite it. Samples with
    // generation <= the last applied one are dropped instead of applied.
    // Not wraparound-safe: after generation wraps past UINT32_MAX this
    // comparison would reject all future samples. Accepted as
    // unreachable in practice -- it needs billions of successful
    // upload/remove calls on one device, far beyond this product's
    // realistic image-library churn.
    void update_imagefs_used_bytes(
        std::uint32_t used_bytes,
        std::uint32_t generation);

    bool lock_flash_display(TickType_t wait_ticks);
    void unlock_flash_display();

    // Appends a diagnostic event (uptime_ms is captured internally via
    // esp_timer_get_time()) and bumps diagnostics_latest_sequence_id in the
    // snapshot. message must be a short, secret-free, caller-built ASCII
    // string (never a raw URL, header, session token, or credential).
    void record_diagnostic_event(
        DiagnosticCategory category,
        DiagnosticSeverity severity,
        const char* message);
    void read_diagnostics_since(
        std::uint32_t since_sequence_id,
        DiagnosticEvent* destination,
        std::size_t destination_capacity,
        DiagnosticsReadResult& result) const;

private:
    static constexpr UBaseType_t kQueueLength = 4;
    static constexpr std::size_t kTerminalResultCapacity = 8U;

    struct TerminalResultSlot {
        bool occupied = false;
        bool completed = false;
        std::uint32_t request_id = 0U;
        RuntimeResult result{};
    };

    RuntimeCoordinator() = default;
    RuntimeCoordinator(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator& operator=(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator(RuntimeCoordinator&&) = delete;
    RuntimeCoordinator& operator=(RuntimeCoordinator&&) = delete;

    friend RuntimeCoordinator& coordinator();

    bool reserve_terminal_result(std::uint32_t request_id);
    void release_terminal_reservation(std::uint32_t request_id);
    // Assumes snapshot_lock_ is already held by the caller; appends one
    // ring event and updates diagnostics_latest_sequence_id but does NOT
    // bump snapshot_.sequence itself (the caller's own critical section
    // does that once for its whole update), so a counter increment and its
    // matching diagnostic event are always published as a single atomic
    // snapshot change instead of two separately-observable ones.
    void record_diagnostic_event_locked(
        DiagnosticCategory category,
        DiagnosticSeverity severity,
        const char* message);

    mutable portMUX_TYPE snapshot_lock_ = portMUX_INITIALIZER_UNLOCKED;
    RuntimeSnapshot snapshot_{};
    bool snapshot_valid_ = false;
    std::uint32_t next_request_id_ = 1U;
    // Guarded by snapshot_lock_; not part of RuntimeSnapshot since it is
    // bookkeeping for update_imagefs_used_bytes's staleness check, not
    // itself a value external readers need.
    std::uint32_t imagefs_usage_generation_ = 0U;
    TerminalResultSlot terminal_results_[kTerminalResultCapacity]{};
    // Guarded by snapshot_lock_ like the rest of this coordinator's shared
    // state; not part of RuntimeSnapshot itself so it is never copied on
    // every publish_snapshot/read_snapshot call (same rationale as
    // terminal_results_ above).
    DiagnosticsRing diagnostics_{};
    StaticQueue_t command_queue_control_{};
    StaticQueue_t result_queue_control_{};
    StaticSemaphore_t flash_display_mutex_control_{};
    std::uint8_t command_queue_storage_[
        kQueueLength * sizeof(RuntimeCommand)]{};
    std::uint8_t result_queue_storage_[
        kQueueLength * sizeof(RuntimeResult)]{};
    QueueHandle_t command_queue_ = nullptr;
    QueueHandle_t result_queue_ = nullptr;
    SemaphoreHandle_t flash_display_mutex_ = nullptr;
};

RuntimeCoordinator& coordinator();

// Records a diagnostic event with the given (short, secret-free,
// caller-built) message immediately, then arms a one-shot esp_timer to
// reboot the device ~500ms later -- giving the caller time to flush an HTTP
// response to the socket first. The event is recorded now, not inside the
// timer callback right before esp_restart(), because a message written
// that late is never actually readable by any client before the reboot
// happens. Returns false if the timer could not be created/armed, in which
// case the device will NOT reboot and callers must not report success.
// Shared by pf_web's admin-triggered reboot endpoint and pf_ota's
// post-update reboot so neither duplicates the esp_timer bookkeeping.
bool schedule_reboot(const char* message);

}  // namespace pf_runtime
