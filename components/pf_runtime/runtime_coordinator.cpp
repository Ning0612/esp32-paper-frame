#include "pf_runtime/runtime_coordinator.hpp"

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstring>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "pf_runtime/reboot_policy.hpp"

namespace pf_runtime {
namespace {

// Created once during RuntimeCoordinator::initialize(), which runs
// single-threaded before any other task (httpd worker, OTA worker, ...)
// exists -- this is what makes lazily-created-on-first-use races in
// schedule_reboot() impossible by construction, rather than needing a lock
// around esp_timer_create() itself (which allocates memory and must not be
// called from inside a portMUX critical section).
esp_timer_handle_t g_reboot_timer = nullptr;

constexpr char kRebootTag[] = "pf_runtime";

// Set once a reboot is actually armed. Other subsystems read it through
// reboot_pending() to stop starting work that cannot outlive the reboot --
// esp_restart() tears down Wi-Fi on its way out, so any network action
// issued after this point is guaranteed to fail.
std::atomic<bool> g_reboot_pending{false};

// Only ever touched from the timer callback, which ESP_TIMER_TASK
// dispatches on a single task, so a plain counter is sufficient.
std::uint32_t g_reboot_deferrals = 0U;

void reboot_timer_callback(void* /*arg*/)
{
    RuntimeSnapshot snapshot{};
    // A refresh in flight owns the panel for ~31 s. Rebooting through it
    // leaves the user looking at a half-drawn frame until the next refresh
    // completes after boot, so wait for it -- but only up to the cap, since
    // a panel that never finishes must not be able to block the reboot.
    //
    // "Busy" deliberately covers three cases, not just the active request
    // id: queued_display_count > 0 means DisplayTask is about to start a
    // refresh this callback would otherwise cut off microseconds later, and
    // DisplayState::refreshing is the state the panel owner publishes --
    // treating either as idle would reintroduce exactly the truncation this
    // defers to avoid. A residual window remains (a refresh can begin
    // between this read and esp_restart()); closing it entirely would need
    // a "no new refreshes" lock inside DisplayTask, which owns that
    // contract, and is not worth the intrusion for a microsecond-wide race.
    const bool display_busy =
        coordinator().read_snapshot(snapshot) &&
        (snapshot.display == DisplayState::refreshing ||
         snapshot.display == DisplayState::queued ||
         snapshot.active_display_request_id != 0U ||
         snapshot.queued_display_count > 0U);
    if (should_defer_reboot(display_busy, g_reboot_deferrals)) {
        ++g_reboot_deferrals;
        if (g_reboot_deferrals == 1U) {
            ESP_LOGI(
                kRebootTag,
                "reboot_deferred_for_display request=%" PRIu32
                " queued=%u",
                snapshot.active_display_request_id,
                static_cast<unsigned>(snapshot.queued_display_count));
        }
        const esp_err_t rearm =
            esp_timer_start_once(g_reboot_timer, kRebootDelayUs);
        // ESP_ERR_INVALID_STATE means the timer is already armed -- another
        // caller scheduled a reboot while this callback was running (the
        // timer counts as disarmed for the duration of its own callback).
        // A reboot is still coming, so return and let that one fire;
        // treating it as a failure would reboot immediately and truncate
        // the very refresh this deferral exists to protect.
        if (rearm == ESP_OK || rearm == ESP_ERR_INVALID_STATE) {
            return;
        }
        // Genuinely could not re-arm: reboot now rather than never. The
        // panel loses this refresh, which is still better than a device
        // that was told to reboot and silently does not.
        ESP_LOGW(
            kRebootTag,
            "reboot_defer_rearm_failed=%s; rebooting now",
            esp_err_to_name(rearm));
    } else if (g_reboot_deferrals > 0U) {
        ESP_LOGI(
            kRebootTag,
            "reboot_proceeding deferrals=%" PRIu32 " display_busy=%d",
            g_reboot_deferrals,
            static_cast<int>(display_busy));
    }
    esp_restart();
}

}  // namespace

esp_err_t RuntimeCoordinator::initialize(
    const RuntimeSnapshot& initial_snapshot)
{
    if (command_queue_ != nullptr || result_queue_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&snapshot_lock_);
    next_request_id_ = 1U;
    for (TerminalResultSlot& slot : terminal_results_) {
        slot = {};
    }
    portEXIT_CRITICAL(&snapshot_lock_);
    publish_snapshot(initial_snapshot);

    command_queue_ = xQueueCreateStatic(
        kQueueLength,
        sizeof(RuntimeCommand),
        command_queue_storage_,
        &command_queue_control_);
    if (command_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    result_queue_ = xQueueCreateStatic(
        kQueueLength,
        sizeof(RuntimeResult),
        result_queue_storage_,
        &result_queue_control_);
    if (result_queue_ == nullptr) {
        command_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    flash_display_mutex_ =
        xSemaphoreCreateMutexStatic(&flash_display_mutex_control_);
    if (flash_display_mutex_ == nullptr) {
        command_queue_ = nullptr;
        result_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    // Best-effort: reboot capability degrading (schedule_reboot() simply
    // returns false later) must not prevent the rest of the runtime --
    // snapshot, queues, flash/display lock -- from becoming available.
    if (g_reboot_timer == nullptr) {
        const esp_timer_create_args_t timer_args{
            .callback = &reboot_timer_callback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "pf_reboot",
            .skip_unhandled_events = false,
        };
        esp_timer_create(&timer_args, &g_reboot_timer);
    }

    return ESP_OK;
}

bool RuntimeCoordinator::read_snapshot(
    RuntimeSnapshot& destination) const
{
    portENTER_CRITICAL(&snapshot_lock_);
    const bool valid = snapshot_valid_;
    if (valid) {
        destination = snapshot_;
    }
    portEXIT_CRITICAL(&snapshot_lock_);
    return valid;
}

void RuntimeCoordinator::publish_snapshot(
    const RuntimeSnapshot& snapshot)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_ = snapshot;
    snapshot_valid_ = true;
    portEXIT_CRITICAL(&snapshot_lock_);
}

bool RuntimeCoordinator::try_submit_command(
    const RuntimeCommand& command)
{
    if (command_queue_ == nullptr) {
        return false;
    }

    const bool requires_terminal_result =
        command.kind == CommandKind::refresh_display;
    if (requires_terminal_result &&
        !reserve_terminal_result(command.request_id)) {
        portENTER_CRITICAL(&snapshot_lock_);
        ++snapshot_.terminal_result_exhausted_count;
        record_diagnostic_event_locked(
            DiagnosticCategory::queue,
            DiagnosticSeverity::warning,
            "terminal_result_exhausted");
        ++snapshot_.sequence;
        portEXIT_CRITICAL(&snapshot_lock_);
        return false;
    }

    DisplayState previous_display = DisplayState::unknown;
    if (command.kind == CommandKind::refresh_display) {
        portENTER_CRITICAL(&snapshot_lock_);
        previous_display = snapshot_.display;
        ++snapshot_.queued_display_count;
        if (snapshot_.display != DisplayState::refreshing) {
            snapshot_.display = DisplayState::queued;
        }
        ++snapshot_.sequence;
        portEXIT_CRITICAL(&snapshot_lock_);
    }

    const bool submitted =
        xQueueSend(command_queue_, &command, 0) == pdTRUE;
    if (!submitted && command.kind == CommandKind::refresh_display) {
        portENTER_CRITICAL(&snapshot_lock_);
        if (snapshot_.queued_display_count > 0) {
            --snapshot_.queued_display_count;
        }
        if (snapshot_.queued_display_count == 0 &&
            snapshot_.display != DisplayState::refreshing) {
            snapshot_.display = previous_display;
        }
        ++snapshot_.command_queue_rejected_count;
        record_diagnostic_event_locked(
            DiagnosticCategory::queue,
            DiagnosticSeverity::warning,
            "command_queue_full");
        ++snapshot_.sequence;
        portEXIT_CRITICAL(&snapshot_lock_);
        release_terminal_reservation(command.request_id);
    }
    return submitted;
}

bool RuntimeCoordinator::try_receive_command(RuntimeCommand& command)
{
    return receive_command(command, 0);
}

bool RuntimeCoordinator::receive_command(
    RuntimeCommand& command,
    const TickType_t wait_ticks)
{
    return command_queue_ != nullptr &&
           xQueueReceive(
               command_queue_,
               &command,
               wait_ticks) == pdTRUE;
}

bool RuntimeCoordinator::try_publish_result(const RuntimeResult& result)
{
    return result_queue_ != nullptr &&
           xQueueSend(result_queue_, &result, 0) == pdTRUE;
}

bool RuntimeCoordinator::try_receive_result(RuntimeResult& result)
{
    return result_queue_ != nullptr &&
           xQueueReceive(result_queue_, &result, 0) == pdTRUE;
}

bool RuntimeCoordinator::retain_terminal_result(
    const RuntimeResult& result)
{
    portENTER_CRITICAL(&snapshot_lock_);
    for (TerminalResultSlot& slot : terminal_results_) {
        if (slot.occupied &&
            slot.request_id == result.request_id) {
            slot.result = result;
            slot.completed = true;
            portEXIT_CRITICAL(&snapshot_lock_);
            return true;
        }
    }
    portEXIT_CRITICAL(&snapshot_lock_);
    return false;
}

bool RuntimeCoordinator::try_take_terminal_result(
    const std::uint32_t request_id,
    RuntimeResult& result)
{
    portENTER_CRITICAL(&snapshot_lock_);
    for (TerminalResultSlot& slot : terminal_results_) {
        if (slot.occupied && slot.completed &&
            slot.request_id == request_id) {
            result = slot.result;
            slot = {};
            portEXIT_CRITICAL(&snapshot_lock_);
            return true;
        }
    }
    portEXIT_CRITICAL(&snapshot_lock_);
    return false;
}

bool RuntimeCoordinator::reserve_terminal_result(
    const std::uint32_t request_id)
{
    portENTER_CRITICAL(&snapshot_lock_);
    TerminalResultSlot* available = nullptr;
    for (TerminalResultSlot& slot : terminal_results_) {
        if (slot.occupied && slot.request_id == request_id) {
            portEXIT_CRITICAL(&snapshot_lock_);
            return false;
        }
        if (!slot.occupied && available == nullptr) {
            available = &slot;
        }
    }
    if (available != nullptr) {
        available->occupied = true;
        available->completed = false;
        available->request_id = request_id;
        available->result = {};
    }
    portEXIT_CRITICAL(&snapshot_lock_);
    return available != nullptr;
}

void RuntimeCoordinator::release_terminal_reservation(
    const std::uint32_t request_id)
{
    portENTER_CRITICAL(&snapshot_lock_);
    for (TerminalResultSlot& slot : terminal_results_) {
        if (slot.occupied && !slot.completed &&
            slot.request_id == request_id) {
            slot = {};
            break;
        }
    }
    portEXIT_CRITICAL(&snapshot_lock_);
}

std::uint32_t RuntimeCoordinator::allocate_request_id()
{
    portENTER_CRITICAL(&snapshot_lock_);
    std::uint32_t request_id = next_request_id_++;
    if (request_id == 0U) {
        request_id = next_request_id_++;
    }
    if (next_request_id_ == 0U) {
        ++next_request_id_;
    }
    portEXIT_CRITICAL(&snapshot_lock_);
    return request_id;
}

void RuntimeCoordinator::update_network(
    const WifiState wifi,
    const InternetState internet,
    const char* const ip_address)
{
    char bounded_ip_address[kIpAddressCapacity]{};
    if (ip_address != nullptr) {
        std::size_t index = 0U;
        while (index < sizeof(bounded_ip_address) - 1U &&
               ip_address[index] != '\0') {
            bounded_ip_address[index] = ip_address[index];
            ++index;
        }
        bounded_ip_address[index] = '\0';
    }

    portENTER_CRITICAL(&snapshot_lock_);
    const WifiState previous_wifi = snapshot_.wifi;
    snapshot_.wifi = wifi;
    if (wifi == WifiState::starting_ap &&
        previous_wifi != WifiState::starting_ap) {
        ++snapshot_.ap_session_id;
    }
    snapshot_.internet = internet;
    std::memcpy(
        snapshot_.ip_address,
        bounded_ip_address,
        sizeof(snapshot_.ip_address));
    if (previous_wifi != WifiState::failed && wifi == WifiState::failed) {
        record_diagnostic_event_locked(
            DiagnosticCategory::network,
            DiagnosticSeverity::warning,
            "wifi_failed");
    } else if (
        previous_wifi == WifiState::failed && wifi == WifiState::connected) {
        record_diagnostic_event_locked(
            DiagnosticCategory::network,
            DiagnosticSeverity::info,
            "wifi_reconnected");
    }
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_time_sync(
    const TimeSyncState time_sync)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.time_sync = time_sync;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_timezone_offset(
    const std::int32_t offset_minutes)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.timezone_offset_minutes = offset_minutes;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_weather(
    const pf_weather::Cache& weather,
    const char* const units)
{
    char bounded_units[pf_weather::kUnitsCapacity]{};
    if (units != nullptr) {
        std::size_t index = 0U;
        while (index < sizeof(bounded_units) - 1U &&
               units[index] != '\0') {
            bounded_units[index] = units[index];
            ++index;
        }
        bounded_units[index] = '\0';
    }

    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.weather = weather;
    std::memcpy(
        snapshot_.weather_units,
        bounded_units,
        sizeof(bounded_units));
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_environment(
    const pf_sensors::EnvironmentCache& environment,
    const pf_sensors::SensorStatus environment_status,
    const pf_sensors::DailyStats& environment_daily)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.environment = environment;
    snapshot_.environment_status = environment_status;
    snapshot_.environment_daily = environment_daily;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_light_and_presence(
    const pf_sensors::LightChannelState (
        &light_channels)[pf_sensors::kLightChannelCount],
    const pf_sensors::LightDecision& light_decision,
    const pf_sensors::PresenceState presence)
{
    portENTER_CRITICAL(&snapshot_lock_);
    for (std::size_t index = 0U; index < pf_sensors::kLightChannelCount;
         ++index) {
        snapshot_.light_channels[index] = light_channels[index];
    }
    snapshot_.light_decision = light_decision;
    snapshot_.light_published = true;
    snapshot_.presence = presence;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

namespace {

void copy_bounded_text(
    char* const destination,
    const std::size_t capacity,
    const char* const source)
{
    if (capacity == 0U) {
        return;
    }
    std::size_t index = 0U;
    if (source != nullptr) {
        while (index < capacity - 1U && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

}  // namespace

void RuntimeCoordinator::update_ota_check_status(
    const OtaCheckState check_state,
    const char* const latest_version,
    const std::uint64_t last_check_epoch_s)
{
    char bounded_version[kOtaVersionCapacity]{};
    copy_bounded_text(bounded_version, sizeof(bounded_version), latest_version);

    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.ota_check_state = check_state;
    std::memcpy(
        snapshot_.ota_latest_version,
        bounded_version,
        sizeof(bounded_version));
    snapshot_.ota_last_check_epoch_s = last_check_epoch_s;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_ota_update_status(
    const OtaUpdateState update_state,
    const std::uint8_t progress_percent,
    const char* const last_error)
{
    char bounded_error[kOtaErrorCapacity]{};
    copy_bounded_text(bounded_error, sizeof(bounded_error), last_error);

    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.ota_update_state = update_state;
    snapshot_.ota_update_progress_percent = progress_percent;
    std::memcpy(
        snapshot_.ota_last_error, bounded_error, sizeof(bounded_error));
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_carousel_status(
    const std::uint32_t current_image_id,
    const std::uint64_t next_due_ms)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.current_image_id = current_image_id;
    snapshot_.next_carousel_due_ms = next_due_ms;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_imagefs_used_bytes(
    const std::uint32_t used_bytes,
    const std::uint32_t generation)
{
    portENTER_CRITICAL(&snapshot_lock_);
    if (generation <= imagefs_usage_generation_) {
        portEXIT_CRITICAL(&snapshot_lock_);
        return;
    }
    snapshot_.imagefs_used_bytes = used_bytes;
    imagefs_usage_generation_ = generation;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::request_manual_carousel_activation(
    const std::uint32_t image_id)
{
    portENTER_CRITICAL(&snapshot_lock_);
    ++snapshot_.manual_activate_request_id;
    // 0 is reserved to mean "never requested" (see RuntimeSnapshot); skip
    // over it on the extremely unlikely wraparound instead of silently
    // resetting to the sentinel value.
    if (snapshot_.manual_activate_request_id == 0U) {
        ++snapshot_.manual_activate_request_id;
    }
    snapshot_.manual_activate_image_id = image_id;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::request_carousel_mode(
    const bool random,
    const std::uint32_t refresh_minutes)
{
    portENTER_CRITICAL(&snapshot_lock_);
    ++snapshot_.carousel_mode_request_id;
    if (snapshot_.carousel_mode_request_id == 0U) {
        ++snapshot_.carousel_mode_request_id;
    }
    snapshot_.carousel_random = random;
    snapshot_.carousel_refresh_minutes = refresh_minutes;
    snapshot_.carousel_mode_request_random = random;
    snapshot_.carousel_mode_request_refresh_minutes = refresh_minutes;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_display_started(
    const std::uint32_t request_id)
{
    portENTER_CRITICAL(&snapshot_lock_);
    if (snapshot_.queued_display_count > 0) {
        --snapshot_.queued_display_count;
    }
    snapshot_.display = DisplayState::refreshing;
    snapshot_.active_display_request_id = request_id;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_display_finished(
    const std::uint32_t request_id,
    const DisplayOutcome outcome,
    const std::uint8_t driver_stage)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.active_display_request_id = 0;
    snapshot_.display =
        snapshot_.queued_display_count > 0
            ? DisplayState::queued
            : (outcome == DisplayOutcome::refreshed_and_slept
                   ? DisplayState::deep_sleep
                   : DisplayState::failed);
    snapshot_.last_display_request_id = request_id;
    snapshot_.last_display_outcome = outcome;
    snapshot_.last_display_stage = driver_stage;
    if (outcome != DisplayOutcome::refreshed_and_slept) {
        record_diagnostic_event_locked(
            DiagnosticCategory::display,
            DiagnosticSeverity::warning,
            to_string(outcome));
    }
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

bool RuntimeCoordinator::lock_flash_display(
    const TickType_t wait_ticks)
{
    const bool acquired =
        flash_display_mutex_ != nullptr &&
        xSemaphoreTake(flash_display_mutex_, wait_ticks) == pdTRUE;
    if (!acquired && wait_ticks != 0) {
        // A zero-wait call is a routine non-blocking probe (HTTP handlers
        // backing off when the panel is busy); only a caller that actually
        // waited and still failed indicates real lock contention worth
        // recording.
        portENTER_CRITICAL(&snapshot_lock_);
        ++snapshot_.flash_display_lock_timeout_count;
        record_diagnostic_event_locked(
            DiagnosticCategory::lock,
            DiagnosticSeverity::warning,
            "flash_display_lock_timeout");
        ++snapshot_.sequence;
        portEXIT_CRITICAL(&snapshot_lock_);
    }
    return acquired;
}

void RuntimeCoordinator::unlock_flash_display()
{
    if (flash_display_mutex_ != nullptr) {
        xSemaphoreGive(flash_display_mutex_);
    }
}

void RuntimeCoordinator::record_diagnostic_event_locked(
    const DiagnosticCategory category,
    const DiagnosticSeverity severity,
    const char* const message)
{
    const std::uint64_t uptime_ms =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
    const std::uint32_t sequence_id = diagnostics_ring_push(
        diagnostics_, category, severity, message, uptime_ms);
    snapshot_.diagnostics_latest_sequence_id = sequence_id;
}

void RuntimeCoordinator::record_diagnostic_event(
    const DiagnosticCategory category,
    const DiagnosticSeverity severity,
    const char* const message)
{
    portENTER_CRITICAL(&snapshot_lock_);
    record_diagnostic_event_locked(category, severity, message);
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::read_diagnostics_since(
    const std::uint32_t since_sequence_id,
    DiagnosticEvent* const destination,
    const std::size_t destination_capacity,
    DiagnosticsReadResult& result) const
{
    portENTER_CRITICAL(&snapshot_lock_);
    result = diagnostics_ring_read_since(
        diagnostics_, since_sequence_id, destination, destination_capacity);
    portEXIT_CRITICAL(&snapshot_lock_);
}

RuntimeCoordinator& coordinator()
{
    static RuntimeCoordinator instance;
    return instance;
}

bool schedule_reboot(const char* const message)
{
    if (g_reboot_timer == nullptr) {
        // RuntimeCoordinator::initialize() failed to create it (rare,
        // e.g. transient allocation failure) -- reboot capability is
        // unavailable this session.
        coordinator().record_diagnostic_event(
            DiagnosticCategory::reboot,
            DiagnosticSeverity::error,
            "reboot_schedule_failed");
        return false;
    }

    const esp_err_t start_result =
        esp_timer_start_once(g_reboot_timer, kRebootDelayUs);
    // ESP_ERR_INVALID_STATE means the timer is already running -- i.e. a
    // reboot is already scheduled by a concurrent caller (e.g. the admin
    // clicking "reboot" right as an OTA update finishes). That is not a
    // failure from this caller's point of view: a reboot IS imminent
    // either way, so reporting it as one would be actively misleading.
    const bool armed =
        start_result == ESP_OK || start_result == ESP_ERR_INVALID_STATE;
    // The diagnostic event is only recorded once the outcome is known, and
    // its content reflects that outcome -- a caller-supplied "success"
    // message must never be recorded if the reboot was not actually armed,
    // which would otherwise mislead anyone polling /api/v1/events into
    // thinking a reboot is imminent when it will never happen.
    coordinator().record_diagnostic_event(
        DiagnosticCategory::reboot,
        armed ? DiagnosticSeverity::info : DiagnosticSeverity::error,
        armed ? message : "reboot_schedule_failed");
    if (armed) {
        // Publish only after the timer is confirmed armed: a caller that
        // sees reboot_pending() will stop issuing work, so claiming it
        // while no reboot is coming would quietly disable that work for
        // the rest of the session.
        g_reboot_pending.store(true, std::memory_order_release);
    }
    return armed;
}

bool reboot_pending()
{
    return g_reboot_pending.load(std::memory_order_acquire);
}

}  // namespace pf_runtime
