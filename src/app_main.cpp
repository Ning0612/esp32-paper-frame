#include <cinttypes>
#include <algorithm>
#include <cstring>
#include <ctime>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pf_auth/auth_service.hpp"
#include "pf_carousel/scheduler.hpp"
#include "pf_carousel/image_frame.hpp"
#include "pf_carousel/welcome_frame.hpp"
#include "pf_config/config_manager.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_display/display_task_esp_idf.hpp"
#include "pf_display/status_bar_renderer.hpp"
#include "pf_network/network_service_esp_idf.hpp"
#include "pf_network/ap_screen.hpp"
#include "pf_network/provisioning_service.hpp"
#include "pf_ota/ota_worker.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_sensors/presence.hpp"
#include "pf_sensors/sensor_task.hpp"
#include "pf_storage/filesystem_manager.hpp"
#include "pf_storage/catalog.hpp"
#include "pf_storage/littlefs_backend.hpp"
#include "pf_storage/storage_worker.hpp"
#include "pf_weather/weather_worker.hpp"
#include "pf_web/health_server.hpp"

namespace {

constexpr char kTag[] = "paperframe";
constexpr std::uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr std::size_t kExpectedPsramBytes = 8U * 1024U * 1024U;
constexpr std::uint64_t kCarouselRetryMs = 1000U;
constexpr std::size_t kCarouselPayloadBytes =
    pf_image::kPfr1MaxPayloadBytes;
// Upper bound on how long the first real carousel image waits for NTP
// time sync and one weather attempt before rendering anyway with
// whatever is currently available. Measured from boot_ms, which is
// captured right before the main loop starts (i.e. after core subsystem
// init, not from device power-on) -- this bounds the wait itself, not
// total time-from-power-on, matching the confirmed design: subsystem
// init time is orthogonal to this budget.
constexpr std::uint64_t kFirstImageReadyTimeoutMs = 60U * 1000U;

bool first_image_ready_or_timed_out(
    const std::uint64_t now_ms,
    const std::uint64_t boot_ms)
{
    if (now_ms - boot_ms >= kFirstImageReadyTimeoutMs) {
        return true;
    }
    pf_runtime::RuntimeSnapshot snapshot{};
    if (!pf_runtime::coordinator().read_snapshot(snapshot)) {
        return false;
    }
    if (snapshot.time_sync != pf_runtime::TimeSyncState::synced) {
        return false;
    }
    // "Ready" means the guaranteed first weather attempt (WeatherWorker
    // fetches once as soon as time sync completes) has resolved one way
    // or the other -- a successful observation or a recorded failure
    // (including an empty/invalid API key) both count; only "no attempt
    // has completed yet" keeps this waiting.
    return snapshot.weather.has_observation ||
           snapshot.weather.last_failure != pf_weather::Failure::none;
}

struct CarouselShownState {
    std::uint32_t image_id = 0U;
    bool shown_once = false;
};

struct CarouselCatalogContext {
    pf_carousel::CarouselItem* items = nullptr;
    std::size_t capacity = 0U;
    std::size_t count = 0U;
    const CarouselShownState* shown = nullptr;
    std::size_t shown_count = 0U;
};

struct HardwareProfile {
    bool flash_ready;
    bool psram_ready;
    std::uint32_t flash_bytes;
    std::size_t psram_bytes;
};

struct AccessPointPresenterContext {
    bool display_started = false;
    pf_network::AccessPointScreenCache screen_cache{};
    StaticSemaphore_t display_submission_mutex_control{};
    SemaphoreHandle_t display_submission_mutex = nullptr;
};

// `ap_screen_owns_panel` is the caller's view of whether the AP instruction
// page still has the panel -- i.e. should_hold_access_point_screen() is
// still true. Gating on that rather than on "is Wi-Fi in AP mode at all"
// matters: with no credentials stored the device stays in AP mode forever,
// so the broader test made the five-minute grace window unreachable. The
// window would expire, ap_mode_display_window_expired would be logged, and
// every following tick would fail this gate -- leaving the panel on the AP
// page and emitting a carousel_*_submission_gate_busy warning every second
// for as long as the device ran (240 of them in the run that found this).
bool try_lock_carousel_submission_gate(
    AccessPointPresenterContext& presenter,
    const bool ap_screen_owns_panel)
{
    if (presenter.display_submission_mutex == nullptr ||
        xSemaphoreTake(presenter.display_submission_mutex, 0) != pdTRUE) {
        return false;
    }

    // A snapshot that cannot be read means the carousel cannot tell whether
    // the panel is free, so it must not take it.
    pf_runtime::RuntimeSnapshot snapshot{};
    if (!pf_runtime::coordinator().read_snapshot(snapshot) ||
        ap_screen_owns_panel) {
        xSemaphoreGive(presenter.display_submission_mutex);
        return false;
    }
    return true;
}

HardwareProfile log_hardware_profile()
{
    esp_chip_info_t chip_info{};
    esp_chip_info(&chip_info);

    std::uint32_t flash_bytes = 0;
    const esp_err_t flash_result =
        esp_flash_get_physical_size(esp_flash_default_chip, &flash_bytes);

    const bool psram_ready = esp_psram_is_initialized();
    const std::size_t psram_bytes =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(
        kTag,
        "chip=ESP32-S3 cores=%d revision=%d.%d",
        chip_info.cores,
        chip_info.revision / 100,
        chip_info.revision % 100);

    if (flash_result == ESP_OK) {
        ESP_LOGI(
            kTag,
            "flash_bytes=%" PRIu32 " expected=%" PRIu32 " status=%s",
            flash_bytes,
            kExpectedFlashBytes,
            flash_bytes == kExpectedFlashBytes ? "ready" : "mismatch");
    } else {
        ESP_LOGE(kTag, "flash_probe_failed=%s", esp_err_to_name(flash_result));
    }

    ESP_LOGI(
        kTag,
        "psram_initialized=%s psram_bytes=%u expected=%u status=%s",
        psram_ready ? "true" : "false",
        static_cast<unsigned>(psram_bytes),
        static_cast<unsigned>(kExpectedPsramBytes),
        psram_ready && psram_bytes >= kExpectedPsramBytes ? "ready" : "degraded");

    return {
        flash_result == ESP_OK && flash_bytes == kExpectedFlashBytes,
        psram_ready && psram_bytes >= kExpectedPsramBytes,
        flash_bytes,
        psram_bytes,
    };
}

pf_runtime::ServiceState state_from_error(const esp_err_t error)
{
    return error == ESP_OK
               ? pf_runtime::ServiceState::ready
               : pf_runtime::ServiceState::degraded;
}

pf_runtime::ServiceState state_from_filesystem(
    const pf_storage::FileSystemStatus& status)
{
    return status.mounted &&
                   status.mount_error == ESP_OK &&
                   status.info_error == ESP_OK
               ? pf_runtime::ServiceState::ready
               : pf_runtime::ServiceState::degraded;
}

std::uint64_t monotonic_ms()
{
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
}

bool collect_carousel_item(
    void* const context,
    const pf_storage::CatalogEntry& entry)
{
    auto* const catalog = static_cast<CarouselCatalogContext*>(context);
    if (catalog == nullptr || catalog->items == nullptr ||
        catalog->count >= catalog->capacity) {
        return false;
    }

    bool shown_once = false;
    for (std::size_t index = 0U; index < catalog->shown_count; ++index) {
        if (catalog->shown[index].image_id == entry.id) {
            shown_once = catalog->shown[index].shown_once;
            break;
        }
    }

    catalog->items[catalog->count++] = {
        entry.id,
        (entry.flags & pf_storage::kCatalogEnabled) != 0U,
        (entry.flags & pf_storage::kCatalogCorrupt) == 0U,
        shown_once,
    };
    return true;
}

void mark_carousel_image_shown(
    CarouselShownState* const shown,
    std::size_t& shown_count,
    const std::uint32_t image_id)
{
    if (shown == nullptr || image_id == 0U) {
        return;
    }
    for (std::size_t index = 0U; index < shown_count; ++index) {
        if (shown[index].image_id == image_id) {
            shown[index].shown_once = true;
            return;
        }
    }
    if (shown_count < pf_storage::kCatalogMaxEntries) {
        shown[shown_count++] = {image_id, true};
    }
}

bool feed_carousel_pfr1(
    void* const context,
    const std::uint8_t* const data,
    const std::size_t length)
{
    auto* const decoder = static_cast<pf_carousel::Pfr1FrameDecoder*>(context);
    return decoder != nullptr && decoder->feed(data, length);
}

pf_display::StatusBarContent build_status_bar_content()
{
    pf_display::StatusBarContent content{};

    pf_runtime::RuntimeSnapshot snapshot{};
    if (!pf_runtime::coordinator().read_snapshot(snapshot)) {
        return content;
    }

    if (snapshot.time_sync == pf_runtime::TimeSyncState::synced) {
        // SNTP itself only ever yields UTC; shifting the epoch by the
        // configured offset before gmtime_r() is what turns that into local
        // wall-clock date/weekday fields (see
        // docs/adr/0005-weather-worker-and-status-bar.md Update 2026-08-21).
        const std::time_t local_now = std::time(nullptr) +
            static_cast<std::time_t>(snapshot.timezone_offset_minutes) * 60;
        std::tm local{};
        if (gmtime_r(&local_now, &local) != nullptr) {
            content.time_valid = true;
            content.year = static_cast<std::uint16_t>(local.tm_year + 1900);
            content.month = static_cast<std::uint8_t>(local.tm_mon + 1);
            content.day = static_cast<std::uint8_t>(local.tm_mday);
            content.iso_weekday = static_cast<std::uint8_t>(
                local.tm_wday == 0 ? 7 : local.tm_wday);
        }
    }

    content.device_ip_available = snapshot.ip_address[0] != '\0';
    if (content.device_ip_available) {
        std::memcpy(
            content.device_ip,
            snapshot.ip_address,
            sizeof(content.device_ip));
        content.device_ip[sizeof(content.device_ip) - 1U] = '\0';
    }

    content.weather_available = snapshot.weather.has_observation;
    if (content.weather_available) {
        content.weather_stale = pf_weather::stale(
            snapshot.weather,
            static_cast<std::uint64_t>(std::time(nullptr)));
        const float temperature = snapshot.weather.observation.temperature;
        content.temperature_rounded = static_cast<int>(
            temperature >= 0.0F ? temperature + 0.5F : temperature - 0.5F);
        std::memcpy(
            content.icon_code,
            snapshot.weather.observation.icon,
            sizeof(content.icon_code));
        content.icon_code[sizeof(content.icon_code) - 1U] = '\0';
    }

    content.indoor_available =
        snapshot.environment_status == pf_sensors::SensorStatus::online &&
        snapshot.environment.has_reading;
    if (content.indoor_available) {
        const float indoor_temperature =
            snapshot.environment.reading.temperature_c;
        content.indoor_temperature_rounded = static_cast<int>(
            indoor_temperature >= 0.0F
                ? indoor_temperature + 0.5F
                : indoor_temperature - 0.5F);
        const float indoor_humidity =
            snapshot.environment.reading.humidity_percent;
        content.indoor_humidity_rounded = static_cast<int>(
            indoor_humidity >= 0.0F
                ? indoor_humidity + 0.5F
                : indoor_humidity - 0.5F);
    }

    return content;
}

bool render_carousel_image(
    pf_storage::StorageWorker& storage_worker,
    const pf_storage::CatalogEntry& entry,
    std::uint8_t* const payload,
    std::uint8_t* const status,
    std::uint8_t* const frame,
    const std::size_t frame_length,
    const pf_image::Pfr1InflateBuffers* const inflate_buffers)
{
    if (payload == nullptr || status == nullptr || frame == nullptr ||
        entry.name_length == 0U ||
        entry.name_length >= sizeof(entry.name)) {
        return false;
    }

    pf_carousel::Pfr1FrameDecoder decoder(
        payload, kCarouselPayloadBytes, inflate_buffers);
    const pf_storage::ImageStreamResult stream = storage_worker.stream_image(
        entry.name,
        entry.name_length,
        feed_carousel_pfr1,
        &decoder);
    if (!stream.ok() || stream.bytes_sent != entry.file_bytes) {
        ESP_LOGW(
            kTag,
            "carousel_image_stream_failed id=%" PRIu32 " error=%s",
            entry.id,
            pf_storage::to_string(stream.error));
        return false;
    }
    if (!decoder.finish_and_compose(
            status,
            pf_display::kLandscapeStatusBytes,
            frame,
            frame_length,
            build_status_bar_content())) {
        ESP_LOGW(
            kTag,
            "carousel_image_decode_failed id=%" PRIu32 " error=%u",
            entry.id,
            static_cast<unsigned>(decoder.error()));
        return false;
    }
    return true;
}

esp_err_t present_access_point_screen(
    void* const context,
    const pf_network::AccessPointInfo& info)
{
    auto* const presenter =
        static_cast<AccessPointPresenterContext*>(context);
    if (presenter == nullptr || !presenter->display_started) {
        return ESP_ERR_INVALID_STATE;
    }

    pf_network::AccessPointScreenPayload payload{};
    const pf_config::SecureZeroGuard payload_guard(payload);
    if (!pf_network::build_access_point_screen_payload(
            info.ssid,
            info.password,
            info.device_suffix,
            payload)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (presenter->screen_cache.shows(payload)) {
        ESP_LOGI(
            kTag,
            "provisioning_screen_unchanged refresh_skipped=true");
        return ESP_OK;
    }

    if (presenter->display_submission_mutex == nullptr ||
        xSemaphoreTake(
            presenter->display_submission_mutex,
            portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const auto release_submission_gate = [&]() {
        xSemaphoreGive(presenter->display_submission_mutex);
    };

    pf_display::FrameWriteLease frame =
        pf_display::display_task().try_acquire_frame();
    if (!frame.valid()) {
        release_submission_gate();
        return ESP_ERR_TIMEOUT;
    }
    if (!pf_network::render_access_point_screen(
            frame.data(),
            frame.size(),
            payload)) {
        release_submission_gate();
        return ESP_FAIL;
    }
    const std::uint32_t request_id =
        pf_runtime::coordinator().allocate_request_id();
    const pf_display::SubmitStatus submitted =
        pf_display::display_task().try_submit_refresh(
            request_id,
            frame);
    if (submitted != pf_display::SubmitStatus::accepted) {
        release_submission_gate();
        return ESP_ERR_TIMEOUT;
    }

    while (true) {
        pf_runtime::RuntimeResult result{};
        if (pf_runtime::coordinator().try_take_terminal_result(
                request_id,
                result)) {
            // Deliberately still the full outcome, not
            // frame_on_panel (ADR-0019): PROVISIONING.md requires the AP
            // radio to start only once the panel reports
            // refreshed_and_slept, and the payload cache below cannot be
            // separated from that gate -- a cache hit returns ESP_OK
            // early and skips the refresh, so caching a displayed-but-not
            // -slept frame would let the next call start the radio
            // without a slept panel ever having been confirmed.
            if (result.display_outcome ==
                pf_runtime::DisplayOutcome::refreshed_and_slept) {
                presenter->screen_cache.mark_displayed(payload);
                ESP_LOGI(
                    kTag,
                    "provisioning_screen_ready request=%" PRIu32,
                    request_id);
                release_submission_gate();
                return ESP_OK;
            }
            ESP_LOGE(
                kTag,
                "provisioning_screen_terminal_failure request=%" PRIu32
                " outcome=%u stage=%u",
                request_id,
                static_cast<unsigned>(result.display_outcome),
                static_cast<unsigned>(result.driver_stage));
            release_submission_gate();
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

}  // namespace

extern "C" void app_main()
{
    // The managed QR encoder logs its input at INFO; that input contains the
    // generated AP password. Keep credentials out of the serial log before
    // any provisioning screen can be rendered.
    esp_log_level_set("QRCODE", ESP_LOG_WARN);
    ESP_LOGI(kTag, "PaperFrame Phase 3 provisioning runtime");
    const HardwareProfile hardware = log_hardware_profile();

    const pf_config::StartupResult config_result = pf_config::initialize();
    if (config_result.error == ESP_OK) {
        ESP_LOGI(
            kTag,
            "config_schema=%" PRIu32 " action=%s",
            pf_config::kCurrentSchemaVersion,
            pf_config::to_string(config_result.action));
    } else {
        ESP_LOGE(
            kTag,
            "config_init_failed=%s action=%s; continuing degraded",
            esp_err_to_name(config_result.error),
            pf_config::to_string(config_result.action));
    }

    // The schema governs the pf_config namespace only. Wi-Fi credentials, the
    // management password and the weather/sensor settings each live in their
    // own NVS namespace with their own validation, so a schema version this
    // firmware cannot interpret says nothing about whether they are readable.
    // Gate them on whether NVS itself came up -- not on the schema, and not on
    // SchemaAction::unavailable, which also fires when merely the pf_config
    // namespace cannot be opened. Gating on the schema turned an OTA rollback
    // onto firmware predating a schema bump into what looked like a factory
    // reset: the device came up in the provisioning AP with every setting
    // apparently gone, while all of it sat intact in NVS.
    const bool nvs_available = config_result.nvs_initialized;

    pf_config::NetworkCredentialLoadResult stored_credentials =
        nvs_available
            ? pf_config::load_network_credentials()
            : pf_config::NetworkCredentialLoadResult{
                  config_result.error,
                  false,
                  {},
              };
    if (stored_credentials.error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "network_credential_load_failed=%s; using provisioning AP",
            esp_err_to_name(stored_credentials.error));
    } else {
        ESP_LOGI(
            kTag,
            "network_credentials_configured=%s",
            stored_credentials.configured ? "true" : "false");
    }
    const pf_config::ManagementPasswordStatus password_status =
        nvs_available
            ? pf_config::management_password_status()
            : pf_config::ManagementPasswordStatus{
                  config_result.error,
                  false,
              };
    if (password_status.error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "management_password_status_failed=%s; "
            "bootstrap access disabled",
            esp_err_to_name(password_status.error));
    } else {
        ESP_LOGI(
            kTag,
            "management_password_configured=%s",
            password_status.configured ? "true" : "false");
    }
    pf_config::WeatherSettingsLoadResult weather_settings_result =
        nvs_available
            ? pf_config::load_weather_settings()
            : pf_config::WeatherSettingsLoadResult{
                  config_result.error,
                  false,
                  {},
              };
    if (weather_settings_result.error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "weather_settings_load_failed=%s; using defaults",
            esp_err_to_name(weather_settings_result.error));
    } else {
        ESP_LOGI(
            kTag,
            "weather_settings_configured=%s api_key_set=%s",
            weather_settings_result.configured ? "true" : "false",
            weather_settings_result.settings.api_key[0] != '\0'
                ? "true"
                : "false");
    }
    pf_config::SensorSettingsLoadResult sensor_settings_result =
        nvs_available
            ? pf_config::load_sensor_settings()
            : pf_config::SensorSettingsLoadResult{
                  config_result.error,
                  false,
                  {},
              };
    if (sensor_settings_result.error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "sensor_settings_load_failed=%s; using defaults",
            esp_err_to_name(sensor_settings_result.error));
    }

    const pf_storage::FileSystemSnapshot filesystem_snapshot =
        pf_storage::mount_all();
    static pf_storage::LittleFsStorageFileSystem imagefs_filesystem(
        "imagefs",
        "/images");
    static pf_storage::StorageWorker storage_worker(imagefs_filesystem);
    pf_storage::StorageWorkerResult storage_startup{};
    if (filesystem_snapshot.imagefs.mounted) {
        storage_startup = storage_worker.start();
        if (!storage_startup.ok()) {
            ESP_LOGE(
                kTag,
                "storage_worker_start_failed=%s recovery=%s action=%s",
                pf_storage::to_string(storage_startup.error),
                pf_storage::to_string(storage_startup.recovery.error),
                pf_storage::to_string(storage_startup.recovery.action));
            if (storage_startup.error ==
                pf_storage::StorageWorkerError::
                    recovery_workspace_alloc_failed) {
                // Distinguishes a boot-time transient allocation failure
                // (needs a contiguous internal-RAM block of
                // sizeof(RecoveryWorkspace) bytes, logged below -- scales
                // with kCatalogMaxEntries) from every other startup
                // failure, with the diagnostic detail that actually
                // explains why:
                // requested size versus the largest contiguous internal
                // block actually available at that moment.
                ESP_LOGE(
                    kTag,
                    "recovery_workspace_alloc_failed requested_bytes=%u "
                    "internal_largest_free_block=%u",
                    static_cast<unsigned>(sizeof(pf_storage::RecoveryWorkspace)),
                    static_cast<unsigned>(
                        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
            }
        } else {
            ESP_LOGI(
                kTag,
                "storage_worker_ready recovery=%s action=%s",
                pf_storage::to_string(storage_startup.recovery.error),
                pf_storage::to_string(storage_startup.recovery.action));
            if (!storage_worker.compression_supported()) {
                ESP_LOGW(
                    kTag,
                    "storage_worker_compression_unavailable; compressed "
                    "PFR1 uploads will be rejected, uncompressed uploads "
                    "unaffected");
            }
        }
    }
    // mount_all() samples imagefs usage before storage_worker.start() runs
    // its crash-recovery pass (rolling back an interrupted .part/.bak/
    // marker can itself change what's on disk), so that first sample can
    // already be stale by the time the initial snapshot is built. Boot is
    // single-threaded up to this point (no concurrent mutation tasks are
    // running yet), so a plain re-query -- without the generation-guarded
    // publish path used post-boot -- is sufficient here.
    std::uint32_t imagefs_used_bytes_at_boot =
        static_cast<std::uint32_t>(filesystem_snapshot.imagefs.used_bytes);
    if (storage_startup.ok()) {
        const std::uint64_t free_bytes = storage_worker.free_bytes();
        if (free_bytes != 0U &&
            free_bytes <= filesystem_snapshot.imagefs.total_bytes) {
            imagefs_used_bytes_at_boot = static_cast<std::uint32_t>(
                filesystem_snapshot.imagefs.total_bytes - free_bytes);
        }
    }
    const bool wifi_password_configured =
        stored_credentials.error == ESP_OK &&
        stored_credentials.configured &&
        stored_credentials.credentials.password[0] != '\0';
    const auto log_filesystem = [](const char* label,
                                   const pf_storage::FileSystemStatus& status) {
        ESP_LOGI(
            kTag,
            "filesystem=%s mounted=%s total=%u used=%u mount_status=%s "
            "info_status=%s",
            label,
            status.mounted ? "true" : "false",
            static_cast<unsigned>(status.total_bytes),
            static_cast<unsigned>(status.used_bytes),
            esp_err_to_name(status.mount_error),
            esp_err_to_name(status.info_error));
    };
    log_filesystem("imagefs", filesystem_snapshot.imagefs);

    std::int32_t initial_timezone_offset_minutes = 0;
    if (config_result.record_available) {
        // A record this firmware could parse always carries validated
        // offset text (make_startup_plan() rejects or normalizes anything
        // else), so this cannot fail in practice; falling back to UTC
        // rather than asserting keeps startup non-fatal either way.
        pf_config::parse_timezone_offset_minutes(
            config_result.record.timezone,
            initial_timezone_offset_minutes);
    }

    const pf_runtime::RuntimeSnapshot initial_snapshot{
        .sequence = 1,
        .flash =
            hardware.flash_ready ? pf_runtime::ServiceState::ready
                                 : pf_runtime::ServiceState::degraded,
        .psram =
            hardware.psram_ready ? pf_runtime::ServiceState::ready
                                 : pf_runtime::ServiceState::degraded,
        .config = state_from_error(config_result.error),
        .imagefs =
            storage_startup.ok()
                ? state_from_filesystem(filesystem_snapshot.imagefs)
                : pf_runtime::ServiceState::degraded,
        .wifi = pf_runtime::WifiState::unknown,
        .internet = pf_runtime::InternetState::unknown,
        .time_sync = pf_runtime::TimeSyncState::unsynced,
        .display = pf_runtime::DisplayState::unknown,
        .active_display_request_id = 0,
        .queued_display_count = 0,
        .last_display_request_id = 0,
        .last_display_outcome = pf_runtime::DisplayOutcome::none,
        .last_display_stage = 0,
        .flash_bytes = hardware.flash_bytes,
        .psram_bytes = static_cast<std::uint32_t>(hardware.psram_bytes),
        .imagefs_total_bytes = static_cast<std::uint32_t>(
            filesystem_snapshot.imagefs.total_bytes),
        .imagefs_used_bytes = imagefs_used_bytes_at_boot,
        .carousel_refresh_minutes = config_result.record_available
                                         ? config_result.record.refresh_minutes
                                         : 0U,
        .carousel_random = config_result.record_available &&
                           config_result.record.carousel_random,
        .timezone_offset_minutes = initial_timezone_offset_minutes,
        // Named explicitly because RuntimeSnapshot deliberately has no
        // default member initialiser on this array (see
        // runtime_snapshot.hpp); without it the firmware build fails
        // on -Werror=missing-field-initializers.
        .light_channels = {},
        .reboot_reason = pf_runtime::classify_reset_reason(
            static_cast<int>(esp_reset_reason())),
    };
    ESP_LOGI(
        kTag,
        "reboot_reason=%s",
        pf_runtime::to_string(initial_snapshot.reboot_reason));
    const esp_err_t runtime_result =
        pf_runtime::coordinator().initialize(initial_snapshot);
    bool display_started = false;
    if (runtime_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "runtime_coordinator_init_failed=%s; continuing without queues",
            esp_err_to_name(runtime_result));
    } else {
        const esp_err_t display_result =
            pf_display::display_task().start(
                pf_runtime::coordinator());
        if (display_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "display_task_start_failed=%s; continuing degraded",
                esp_err_to_name(display_result));
        } else {
            display_started = true;
        }
    }

    const esp_err_t network_stack_result = esp_netif_init();
    if (network_stack_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "network_stack_init_failed=%s; health server disabled",
            esp_err_to_name(network_stack_result));
    }

    pf_config::NetworkCredentials network_credentials{};
    const bool password_bootstrap =
        password_status.error == ESP_OK &&
        !password_status.configured;
    const bool network_configured =
        stored_credentials.error == ESP_OK &&
        stored_credentials.configured &&
        !password_bootstrap;
    if (network_configured) {
        network_credentials = stored_credentials.credentials;
    } else if (
        stored_credentials.error == ESP_OK &&
        stored_credentials.configured &&
        password_bootstrap) {
        ESP_LOGW(
            kTag,
            "management_password_missing; retaining saved Wi-Fi "
            "credentials and starting local bootstrap AP");
    }
    pf_config::secure_zero(stored_credentials.credentials);
    AccessPointPresenterContext ap_presenter{
        .display_started = display_started,
    };
    ap_presenter.display_submission_mutex =
        xSemaphoreCreateMutexStatic(
            &ap_presenter.display_submission_mutex_control);
    const esp_err_t network_service_result =
        network_stack_result == ESP_OK &&
                runtime_result == ESP_OK
            ? pf_network::network_service().start(
                  pf_runtime::coordinator(),
                  network_credentials,
                  network_configured,
                  &present_access_point_screen,
                  &ap_presenter)
            : ESP_ERR_INVALID_STATE;
    pf_config::secure_zero(network_credentials);
    if (network_service_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "network_service_start_failed=%s; continuing degraded",
            esp_err_to_name(network_service_result));
    }

    const esp_err_t weather_worker_result =
        network_service_result == ESP_OK && runtime_result == ESP_OK
            ? pf_weather::weather_worker().start(
                  pf_runtime::coordinator(),
                  pf_network::network_service())
            : ESP_ERR_INVALID_STATE;
    if (weather_worker_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "weather_worker_start_failed=%s; weather stays unavailable",
            esp_err_to_name(weather_worker_result));
    }

    const esp_err_t sensor_task_result =
        runtime_result == ESP_OK
            ? pf_sensors::sensor_task().start(pf_runtime::coordinator())
            : ESP_ERR_INVALID_STATE;
    if (sensor_task_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "sensor_task_start_failed=%s; sensors stay unavailable",
            esp_err_to_name(sensor_task_result));
    }

    // Provisioning stores Wi-Fi credentials in pf_wifi, not in the central
    // record, so an uninterpretable schema must not stop it starting.
    const esp_err_t provisioning_store_result =
        !nvs_available
            ? config_result.error
            : runtime_result != ESP_OK
                  ? runtime_result
                  : pf_network::provisioning_service().start(
                        pf_runtime::coordinator());
    if (provisioning_store_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "provisioning_store_start_failed=%s",
            esp_err_to_name(provisioning_store_result));
    }

    // Likewise the management password: it lives in its own namespace. Leaving
    // AuthService unstarted makes every login return 503 and, because
    // authenticate_request() fails closed with password_configured=true when
    // uninitialised, it also makes the device *look* like the password is
    // fine -- which is exactly how this was missed the first time round.
    const esp_err_t authentication_result =
        !nvs_available
            ? config_result.error
            : runtime_result != ESP_OK
                  ? runtime_result
                  : pf_auth::auth_service().start(
                        pf_runtime::coordinator());
    if (authentication_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "authentication_service_start_failed=%s; "
            "management access fails closed",
            esp_err_to_name(authentication_result));
    }

    httpd_handle_t health_server = nullptr;
    const bool initial_bootstrap =
        stored_credentials.error == ESP_OK &&
        !stored_credentials.configured &&
        password_status.error == ESP_OK &&
        !password_status.configured;
    pf_web::HealthServerAccessConfig web_access{
        .initial_bootstrap = initial_bootstrap,
        .password_bootstrap = password_bootstrap,
        .wifi_configured = stored_credentials.configured,
        .wifi_password_configured = wifi_password_configured,
        .management_password_configured =
            password_status.error == ESP_OK && password_status.configured,
        .refresh_minutes = config_result.record_available
                               ? config_result.record.refresh_minutes
                               : 0U,
        .carousel_random = config_result.record_available &&
                           config_result.record.carousel_random,
        .weather_configured = weather_settings_result.error == ESP_OK &&
                              weather_settings_result.configured,
        .weather_settings = weather_settings_result.error == ESP_OK
                                ? weather_settings_result.settings
                                : pf_config::WeatherSettings{},
        .sensor_settings = sensor_settings_result.error == ESP_OK
                                ? sensor_settings_result.settings
                                : pf_config::SensorSettings{},
    };
    const char* const timezone = config_result.record_available
                                     ? config_result.record.timezone
                                     : "unknown";
    std::strncpy(
        web_access.timezone,
        timezone,
        sizeof(web_access.timezone) - 1U);
    web_access.timezone[sizeof(web_access.timezone) - 1U] = '\0';
    web_access.storage_worker = &storage_worker;
    const esp_err_t health_result =
        network_stack_result == ESP_OK
            ? pf_web::start_health_server(
                  &health_server,
                  web_access)
            : network_stack_result;
    if (health_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "health_server_start_failed=%s; continuing degraded",
            esp_err_to_name(health_result));
    } else {
        ESP_LOGI(
            kTag,
            "health_server_ready route=/api/v1/health");
    }
    pf_config::secure_zero(weather_settings_result.settings);
    pf_config::secure_zero(web_access.weather_settings);

    const esp_err_t ota_worker_result =
        runtime_result == ESP_OK
            ? pf_ota::ota_worker().start(pf_runtime::coordinator())
            : ESP_ERR_INVALID_STATE;
    if (ota_worker_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "ota_worker_start_failed=%s; firmware updates unavailable",
            esp_err_to_name(ota_worker_result));
    }

    // Every core service above has attempted to start by this point;
    // confirm this boot as valid so the bootloader does not roll back to
    // the previous OTA slot on the next boot attempt. If this firmware
    // crash-loops before reaching this line, CONFIG_BOOTLOADER_APP_ROLLBACK
    // _ENABLE (sdkconfig.defaults) reverts to the previous slot instead.
    // See docs/adr/0008-ota-github-releases-and-rollback.md.
    const esp_err_t rollback_confirm_result =
        esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(
        kTag,
        "rollback_confirmed=%s",
        esp_err_to_name(rollback_confirm_result));

    const std::uint32_t refresh_minutes =
        config_result.record_available
            ? config_result.record.refresh_minutes
            : pf_config::kDefaultRefreshMinutes;
    pf_carousel::CarouselScheduler carousel{
        pf_carousel::CarouselConfig{
            refresh_minutes,
            config_result.record_available &&
                    config_result.record.carousel_random
                ? pf_carousel::CarouselMode::random
                : pf_carousel::CarouselMode::sequential,
            0x50465231U,
        },
    };
    pf_carousel::CarouselDecision active_carousel_decision{};
    std::uint32_t active_carousel_request_id = 0U;
    std::uint32_t active_blank_request_id = 0U;
    // The first real (non-welcome) image after boot gets a bounded chance
    // for NTP time sync and one weather attempt to resolve first, so it
    // doesn't render with stale/unknown status-bar data for a whole
    // carousel interval when both would have finished a few seconds
    // later anyway. Outside the AP display hold below, welcome frames
    // (empty library) still render immediately, as before.
    bool first_real_image_pending = true;
    bool ap_mode_active = false;
    bool ap_mode_ready = false;
    std::uint64_t ap_mode_started_ms = 0U;
    bool ap_screen_hold_active = false;
    // Last observed "there is something the carousel could show". The
    // authoritative value is recomputed from the catalog further down the
    // loop, but the AP-ownership question is asked earlier than that (by the
    // presence path) and by the submission gate, so the previous tick's value
    // is what they can see. The catalog only changes on an upload/delete, so
    // being one tick behind cannot flip the answer in practice.
    bool last_has_displayable_image = false;
    // The device IP that the welcome frame currently on the panel was drawn
    // with (empty when it was drawn before Wi-Fi had an address, which is
    // what happens at boot). Compared against the live address to decide
    // whether that frame still tells the truth.
    //
    // Promoted from submitted_welcome_ip only once the panel refresh has
    // actually succeeded: a submission that is accepted and then fails
    // leaves the old frame on the panel, and recording the new address at
    // submit time would make the redraw condition go false while the stale
    // frame is still displayed -- the retry would then have to wait out a
    // full carousel interval.
    char welcome_frame_ip[pf_runtime::kIpAddressCapacity]{};
    char submitted_welcome_ip[pf_runtime::kIpAddressCapacity]{};
    const std::uint64_t boot_ms = monotonic_ms();
    pf_sensors::PresenceState previous_presence =
        pf_sensors::PresenceState::unknown;
    std::uint32_t previous_manual_activate_request_id = 0U;
    std::uint32_t previous_carousel_mode_request_id = 0U;
    bool pending_manual_activate = false;
    bool pending_manual_activate_warned = false;
    std::uint32_t pending_manual_activate_image_id = 0U;
    bool pending_presence_force_immediate = false;
    bool pending_presence_away_blank = false;
    // Which submitted frame is on the panel, and whether it was the away
    // blank. Presence starts at `unknown` and converges to `present` a few
    // seconds into every boot; without tracking what the panel shows, that
    // first transition was treated as a return from away and spent a full
    // ~31 s refresh restoring a panel that had never been blanked.
    //
    // A plain boolean was not enough. Terminal results are consumed in a
    // fixed order every tick -- the blank first, then the carousel -- which
    // is not the order the panel displayed them: a carousel refresh
    // submitted before an away transition lands first but is consumed
    // second, and would then clear a blank that really is on the panel.
    // Request ids are allocated monotonically, so comparing them lets the
    // frame that actually landed last win regardless of consumption order.
    //
    // The AP instruction screen is submitted from another task and its
    // result is not consumed here; presence deliberately does not blank
    // while AP mode owns the panel, so it never competes for this state.
    std::uint32_t panel_frame_request_id = 0U;
    bool panel_shows_blank = false;
    static CarouselShownState carousel_shown[pf_storage::kCatalogMaxEntries]{};
    std::size_t carousel_shown_count = 0U;
    static pf_carousel::CarouselItem carousel_items[pf_storage::kCatalogMaxEntries]{};
    std::uint8_t* carousel_status = static_cast<std::uint8_t*>(
        heap_caps_malloc(
            pf_display::kLandscapeStatusBytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (carousel_status == nullptr) {
        carousel_status = static_cast<std::uint8_t*>(
            heap_caps_malloc(
                pf_display::kLandscapeStatusBytes, MALLOC_CAP_8BIT));
    }
    if (carousel_status == nullptr) {
        ESP_LOGE(
            kTag,
            "carousel_status_alloc_failed bytes=%u; image display disabled",
            static_cast<unsigned>(pf_display::kLandscapeStatusBytes));
    }
    std::uint8_t* carousel_payload = static_cast<std::uint8_t*>(
        heap_caps_malloc(
            kCarouselPayloadBytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (carousel_payload == nullptr) {
        carousel_payload = static_cast<std::uint8_t*>(
            heap_caps_malloc(kCarouselPayloadBytes, MALLOC_CAP_8BIT));
    }
    if (carousel_payload == nullptr) {
        ESP_LOGE(
            kTag,
            "carousel_payload_alloc_failed bytes=%u; image display disabled",
            static_cast<unsigned>(kCarouselPayloadBytes));
    }

    // Scratch for decoding a compressed PFR1 payload (Pfr1Flags::
    // kCompressed) on the carousel display path; see StorageWorker's
    // matching allocation for the ingest/recovery side. A failure here only
    // means compressed images can't be displayed (falls back to whatever
    // Pfr1FrameDecoder does without inflate_buffers -- fails closed with
    // unsupported_compression for that one image), not that carousel
    // display stops working for uncompressed images.
    pf_image::Pfr1InflateBuffers carousel_inflate_buffers{};
    std::uint8_t* carousel_inflate_compressed = static_cast<std::uint8_t*>(
        heap_caps_malloc(
            pf_image::kPfr1MaxPayloadBytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (carousel_inflate_compressed == nullptr) {
        carousel_inflate_compressed = static_cast<std::uint8_t*>(
            heap_caps_malloc(pf_image::kPfr1MaxPayloadBytes, MALLOC_CAP_8BIT));
    }
    std::uint8_t* carousel_inflate_output = static_cast<std::uint8_t*>(
        heap_caps_malloc(
            pf_image::kPfr1MaxPayloadBytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (carousel_inflate_output == nullptr) {
        carousel_inflate_output = static_cast<std::uint8_t*>(
            heap_caps_malloc(pf_image::kPfr1MaxPayloadBytes, MALLOC_CAP_8BIT));
    }
    if (carousel_inflate_compressed == nullptr ||
        carousel_inflate_output == nullptr) {
        // Don't leak the other buffer if only one of the two allocations
        // failed -- there's no partial-scratch use case, so free whichever
        // one succeeded rather than holding it for the rest of the program.
        heap_caps_free(carousel_inflate_compressed);
        heap_caps_free(carousel_inflate_output);
        carousel_inflate_compressed = nullptr;
        carousel_inflate_output = nullptr;
        ESP_LOGW(
            kTag,
            "carousel_inflate_scratch_alloc_failed; compressed PFR1 images "
            "will not display, uncompressed images unaffected");
    } else {
        carousel_inflate_buffers.compressed = carousel_inflate_compressed;
        carousel_inflate_buffers.compressed_capacity =
            pf_image::kPfr1MaxPayloadBytes;
        carousel_inflate_buffers.output = carousel_inflate_output;
        carousel_inflate_buffers.output_capacity =
            pf_image::kPfr1MaxPayloadBytes;
    }

    // Guild.md 4.9: a single all-white refresh while away, then panel
    // sleep (refresh already sleeps afterward). This bypasses the
    // carousel scheduler entirely -- it is not tracked as an
    // active_carousel_decision, but its request_id still reserves a
    // RuntimeCoordinator terminal-result slot (the same refresh_display
    // path carousel decisions use) and must be drained once the refresh
    // completes, or repeated away/present cycles exhaust the fixed-size
    // reservation pool. Returns true if a blank refresh is already
    // in flight or was just accepted (nothing left to retry); false if
    // the attempt failed and should be retried on a later tick.
    // Whether the AP instruction page owns the panel at `now`. Every consumer
    // asks this rather than "is Wi-Fi in AP mode": a device with no stored
    // credentials stays in AP mode forever, so that broader question would
    // never let the panel be handed back once the grace window expired.
    // Evaluated per call rather than read from ap_screen_hold_active, which
    // is only refreshed near the end of the loop -- the presence path and the
    // submission gate run before that and would otherwise act on the previous
    // tick's answer.
    const auto ap_screen_owns_panel = [&](const std::uint64_t now) {
        return ap_mode_active &&
               (!ap_mode_ready ||
                pf_network::should_hold_access_point_screen(
                    true,
                    last_has_displayable_image,
                    now,
                    ap_mode_started_ms));
    };

    // Records a frame that reached the panel. Older frames are ignored,
    // so the caller does not have to care which result it consumed first.
    const auto note_frame_on_panel =
        [&](const std::uint32_t request_id, const bool is_blank) {
        // Wrap-safe: request ids restart at 1 after UINT32_MAX, so a
        // plain `<=` would ignore every result from then on.
        if (static_cast<std::int32_t>(request_id - panel_frame_request_id) <=
            0) {
            return;
        }
        panel_frame_request_id = request_id;
        panel_shows_blank = is_blank;
        if (!panel_shows_blank) {
            // A real frame is on the panel, so any restore still owed is
            // settled; leaving the flag armed would spend another full
            // refresh putting an image on a panel that already has one.
            pending_presence_force_immediate = false;
            if (previous_presence == pf_sensors::PresenceState::away) {
                // ...but the device is away, so this frame must not be
                // what stays there. submit_presence_away_blank() treats an
                // already-in-flight blank as "handled", which is wrong
                // when a real frame was queued behind it: that frame lands
                // last. Re-arm the blank instead of leaving the panel
                // showing content while nobody is there. Reachable with
                // the shortest configurable debounces (10 s away, 1 s
                // return) against a ~31 s refresh.
                pending_presence_away_blank = true;
            }
        }
    };

    const auto submit_presence_away_blank =
        [&](const std::uint64_t now) -> bool {
        if (active_blank_request_id != 0U) {
            return true;
        }
        if (!try_lock_carousel_submission_gate(
                ap_presenter, ap_screen_owns_panel(now))) {
            return false;
        }
        pf_display::FrameWriteLease blank_frame =
            pf_display::display_task().try_acquire_frame();
        if (!blank_frame.valid() ||
            !pf_carousel::render_blank_frame(
                blank_frame.data(), blank_frame.size())) {
            ESP_LOGW(kTag, "presence_away_blank_frame_unavailable");
            xSemaphoreGive(ap_presenter.display_submission_mutex);
            return false;
        }
        const std::uint32_t blank_request_id =
            pf_runtime::coordinator().allocate_request_id();
        const pf_display::SubmitStatus blank_submit =
            pf_display::display_task().try_submit_refresh(
                blank_request_id, blank_frame);
        if (blank_submit == pf_display::SubmitStatus::accepted) {
            // Accepted means this frame is going to take the panel. The AP
            // screen's claim has to drop now, not when the result is
            // consumed a refresh later: a new AP session starting inside
            // that window would see a stale claim, skip its refresh and
            // bring the radio up over a blank panel.
            ap_presenter.screen_cache.mark_superseded();
        }
        if (blank_submit != pf_display::SubmitStatus::accepted) {
            xSemaphoreGive(ap_presenter.display_submission_mutex);
            ESP_LOGW(
                kTag,
                "presence_away_blank_submit_failed status=%u",
                static_cast<unsigned>(blank_submit));
            return false;
        }
        active_blank_request_id = blank_request_id;
        ESP_LOGI(
            kTag,
            "presence_away_blank_queued request=%" PRIu32,
            blank_request_id);
        xSemaphoreGive(ap_presenter.display_submission_mutex);
        return true;
    };

    while (true) {
        const std::uint64_t now_ms = monotonic_ms();

        // A failed snapshot read carries no presence information -- it
        // must not be treated as an observed "unknown" transition (that
        // would flip away/unknown/away and re-trigger blank-frame
        // submission every failed read). previous_presence is only ever
        // updated from a successful read, and doubles as the "last known
        // presence" used by the carousel-pause gate below.
        pf_runtime::RuntimeSnapshot presence_snapshot{};
        const bool presence_snapshot_read =
            pf_runtime::coordinator().read_snapshot(presence_snapshot);
        if (presence_snapshot_read) {
            const bool ap_mode =
                presence_snapshot.wifi ==
                    pf_runtime::WifiState::starting_ap ||
                presence_snapshot.wifi ==
                    pf_runtime::WifiState::provisioning;
            const bool ap_ready =
                presence_snapshot.wifi ==
                pf_runtime::WifiState::provisioning;
            if (ap_mode && !ap_mode_active) {
                ap_mode_active = true;
                ap_mode_ready = ap_ready;
                ap_mode_started_ms = ap_ready ? now_ms : 0U;
                if (ap_ready) {
                    ESP_LOGI(kTag, "ap_mode_display_window_started");
                } else {
                    ESP_LOGI(kTag, "ap_mode_display_waiting_for_ready");
                }
            } else if (ap_mode && ap_ready && !ap_mode_ready) {
                ap_mode_ready = true;
                ap_mode_started_ms = now_ms;
                ESP_LOGI(kTag, "ap_mode_display_window_started");
            } else if (!ap_mode && ap_mode_active) {
                ap_mode_active = false;
                ap_mode_ready = false;
                ap_mode_started_ms = 0U;
                ap_screen_hold_active = false;
                ESP_LOGI(kTag, "ap_mode_display_window_ended");
            }
        }
        if (presence_snapshot_read &&
            presence_snapshot.presence != previous_presence) {
            const pf_sensors::PresenceState current_presence =
                presence_snapshot.presence;
            // Default to "nothing pending" on every observed transition;
            // only the branches below that actually need a retry set
            // their flag back to true. This also covers presence
            // collapsing to `unknown` (e.g. sensor error mid-debounce),
            // which previously left a stale pending_presence_force_immediate
            // from an earlier `present` transition armed indefinitely.
            pending_presence_force_immediate = false;
            pending_presence_away_blank = false;
            const pf_sensors::PresencePanelAction presence_action =
                pf_sensors::presence_panel_action(
                    previous_presence,
                    current_presence,
                    panel_shows_blank);
            if (presence_action ==
                    pf_sensors::PresencePanelAction::blank_for_away &&
                display_started) {
                if (ap_mode_active) {
                    pending_presence_away_blank = true;
                } else {
                    pending_presence_away_blank =
                        !submit_presence_away_blank(now_ms);
                }
            } else if (presence_action ==
                       pf_sensors::PresencePanelAction::restore_after_away) {
                // A return only needs a redraw if the away blank is what
                // the panel is currently showing. The unknown -> present
                // transition every boot performs is not a return: nothing
                // was blanked, and forcing a refresh there costs ~31 s and
                // leaves the panel showing a fresh carousel pick instead
                // of the stored current image. away -> unknown -> present
                // (sensor fault mid-debounce) still redraws, because the
                // panel really is blank in that path.
                if (ap_mode_active) {
                    // AP instructions own the panel until the AP grace
                    // window ends; presence must not blank or reschedule it.
                    pending_presence_force_immediate = false;
                } else if (carousel.force_immediate(now_ms)) {
                    // With an empty library the panel is showing a welcome
                    // frame, and the away transition replaced it with a
                    // blank one. Resetting the deadline alone would leave
                    // it blank forever: poll() short-circuits on "welcome
                    // already displayed" regardless of the deadline. Mark
                    // that frame stale so the return actually redraws it.
                    carousel.request_welcome_redraw(now_ms);
                    ESP_LOGI(kTag, "presence_return_deadline_reset");
                } else {
                    pending_presence_force_immediate = true;
                }
            }
            previous_presence = current_presence;
        }

        // A transient failure at the moment of the away transition (frame
        // pool busy, submit rejected) must not permanently strand the
        // panel showing its pre-away content: retry every tick while
        // still away and nothing is currently in flight.
        if (pending_presence_away_blank &&
            previous_presence == pf_sensors::PresenceState::away) {
            pending_presence_away_blank = !submit_presence_away_blank(now_ms);
        }

        if (active_blank_request_id != 0U) {
            pf_runtime::RuntimeResult blank_result{};
            if (pf_runtime::coordinator().try_take_terminal_result(
                    active_blank_request_id,
                    blank_result)) {
                ESP_LOGI(
                    kTag,
                    "presence_away_blank_request=%" PRIu32 " outcome=%u",
                    active_blank_request_id,
                    static_cast<unsigned>(blank_result.display_outcome));
                const std::uint32_t completed_blank_id =
                    active_blank_request_id;
                active_blank_request_id = 0U;
                if (blank_result.frame_on_panel) {
                    // The panel is blank now. It may still have failed to
                    // sleep afterwards (ADR-0019), which is a power
                    // concern, not a reason to redraw the same blank.
                    note_frame_on_panel(completed_blank_id, true);
                    if (panel_shows_blank &&
                        previous_presence ==
                            pf_sensors::PresenceState::present) {
                        // The user came back while this blank was still in
                        // flight -- plausible, since the return debounce
                        // (30 s) and a full refresh (~31 s) are the same
                        // order. That transition already happened and saw
                        // panel_shows_blank == false, so it did not
                        // schedule a restore. Arm one now, or the panel
                        // stays blank until the next carousel interval --
                        // and with an empty library the scheduler parks
                        // forever, so it would stay blank indefinitely.
                        pending_presence_force_immediate = true;
                    }
                } else if (previous_presence ==
                           pf_sensors::PresenceState::away) {
                    // Accepted into the queue but the frame never reached
                    // the panel (e.g. transport error, or a BUSY timeout
                    // during the refresh itself): retry rather than
                    // silently giving up for the rest of this away period.
                    pending_presence_away_blank = true;
                }
            }
        }

        if (carousel.in_flight() &&
            active_carousel_request_id != 0U) {
            pf_runtime::RuntimeResult terminal_result{};
            if (pf_runtime::coordinator().try_take_terminal_result(
                    active_carousel_request_id,
                    terminal_result)) {
                // Everything below asks "is this frame what the panel
                // is showing" -- whether the panel also slept afterwards
                // does not change the answer, and treating a failed sleep
                // as a failed refresh made the scheduler redraw a picture
                // that was already correct (ADR-0019).
                const bool succeeded = terminal_result.frame_on_panel;
                if (succeeded) {
                    note_frame_on_panel(active_carousel_request_id, false);
                }
                carousel.complete(
                    active_carousel_decision,
                    succeeded,
                    now_ms);
                if (succeeded &&
                    active_carousel_decision.kind ==
                        pf_carousel::DecisionKind::display_welcome) {
                    // The frame is genuinely on the panel now, so what it
                    // says about the address becomes the value the redraw
                    // condition compares against. A failed refresh
                    // deliberately leaves welcome_frame_ip alone, so the
                    // condition stays true and the next tick retries.
                    std::memcpy(
                        welcome_frame_ip,
                        submitted_welcome_ip,
                        sizeof(welcome_frame_ip));
                }
                if (succeeded &&
                    active_carousel_decision.kind ==
                        pf_carousel::DecisionKind::display_image) {
                    mark_carousel_image_shown(
                        carousel_shown,
                        carousel_shown_count,
                        active_carousel_decision.image_id);
                    pf_storage::CatalogEntry displayed_entry{};
                    if (storage_worker.find_catalog_entry_by_id(
                            active_carousel_decision.image_id,
                            displayed_entry) &&
                        (displayed_entry.flags & pf_storage::kCatalogCurrent) ==
                            0U) {
                        const pf_storage::ImageStoreResult activate_result =
                            storage_worker.activate_image(
                                active_carousel_decision.image_id);
                        if (!activate_result.ok()) {
                            ESP_LOGW(
                                kTag,
                                "carousel_current_persist_failed id=%" PRIu32
                                " error=%s",
                                active_carousel_decision.image_id,
                                pf_storage::to_string(activate_result.error));
                        }
                    }
                }
                ESP_LOGI(
                    kTag,
                    "carousel_request=%" PRIu32
                    " outcome=%u next_due_ms=%llu",
                    active_carousel_request_id,
                    static_cast<unsigned>(
                        terminal_result.display_outcome),
                    static_cast<unsigned long long>(
                        carousel.next_due_ms()));
                if (succeeded) {
                    // 0 (welcome/status frame) or a real catalogued image
                    // id, but only once the refresh actually committed --
                    // a failed refresh must not claim the panel now shows
                    // something it doesn't.
                    const std::uint32_t displayed_image_id =
                        active_carousel_decision.kind ==
                                pf_carousel::DecisionKind::display_image
                            ? active_carousel_decision.image_id
                            : 0U;
                    pf_runtime::coordinator().update_carousel_status(
                        displayed_image_id, carousel.next_due_ms());
                }
                active_carousel_request_id = 0U;
            }
        }

        if (presence_snapshot_read &&
            presence_snapshot.carousel_mode_request_id !=
                previous_carousel_mode_request_id &&
            active_carousel_request_id == 0U &&
            active_blank_request_id == 0U &&
            !carousel.in_flight()) {
            const bool mode_changed = carousel.configure(
                pf_carousel::CarouselConfig{
                    presence_snapshot.carousel_mode_request_refresh_minutes,
                    presence_snapshot.carousel_mode_request_random
                        ? pf_carousel::CarouselMode::random
                        : pf_carousel::CarouselMode::sequential,
                    0x50465231U,
                });
            if (mode_changed) {
                previous_carousel_mode_request_id =
                    presence_snapshot.carousel_mode_request_id;
                ESP_LOGI(
                    kTag,
                    "carousel_mode_applied request=%" PRIu32 " mode=%s",
                    previous_carousel_mode_request_id,
                    presence_snapshot.carousel_mode_request_random
                        ? "random"
                        : "sequential");
            }
        }

        if (pending_presence_force_immediate && !carousel.in_flight()) {
            if (carousel.force_immediate(now_ms)) {
                // Same reason as the immediate path above: an empty library
                // needs the welcome frame marked stale, or the panel stays
                // on the away blank frame indefinitely.
                carousel.request_welcome_redraw(now_ms);
                pending_presence_force_immediate = false;
                ESP_LOGI(kTag, "presence_return_deadline_reset_deferred");
            }
        }

        // Guild.md 4.9: pause carousel advancement while away (an
        // in-flight decision above still gets its completion processed;
        // this only stops new work from being issued). AP mode is the
        // explicit exception because its connection page owns the panel
        // until its image grace window is released. previous_presence is
        // the last successfully observed presence (see the snapshot read
        // above), not necessarily this tick's value.
        if (previous_presence != pf_sensors::PresenceState::away ||
            ap_screen_owns_panel(now_ms)) {
        std::size_t carousel_item_count = 0U;
        if (storage_worker.ready()) {
            CarouselCatalogContext catalog_context{
                carousel_items,
                pf_storage::kCatalogMaxEntries,
                0U,
                carousel_shown,
                carousel_shown_count,
            };
            if (!storage_worker.visit_catalog(
                    collect_carousel_item,
                    &catalog_context)) {
                vTaskDelay(pdMS_TO_TICKS(1000U));
                continue;
            }
            carousel_item_count = catalog_context.count;
        }

        bool has_displayable_image = false;
        for (std::size_t index = 0U;
             index < carousel_item_count;
             ++index) {
            if (carousel_items[index].enabled &&
                carousel_items[index].valid) {
                has_displayable_image = true;
                break;
            }
        }
        last_has_displayable_image = has_displayable_image;
        const bool hold_ap_screen =
            ap_mode_active &&
            (!ap_mode_ready ||
             pf_network::should_hold_access_point_screen(
                 true,
                 has_displayable_image,
                 now_ms,
                 ap_mode_started_ms));
        if (hold_ap_screen) {
            ap_screen_hold_active = true;
            // The network task has already rendered the AP instructions.
            // Keep the panel on that page while the AP has no usable image,
            // or until the five-minute grace window expires when one exists.
            vTaskDelay(pdMS_TO_TICKS(1000U));
            continue;
        }
        if (ap_mode_active && ap_mode_ready &&
            ap_screen_hold_active) {
            if (active_blank_request_id != 0U || carousel.in_flight() ||
                !carousel.force_immediate(now_ms)) {
                vTaskDelay(pdMS_TO_TICKS(1000U));
                continue;
            }
            ap_screen_hold_active = false;
            ESP_LOGI(kTag, "ap_mode_display_window_expired");
        }

        // A WebUI "activate this image" request only persists the new
        // current-image flag (pf_web runs in a different task); pick up
        // the change here instead of waiting for the normal rotation
        // interval. Compare request_id, not image_id, so re-activating the
        // same id is still treated as a new request. "Observed" and
        // "handed off to carousel" are tracked separately: request_manual()
        // can transiently fail (catalog not yet reflecting the write this
        // tick) and must be retried on a later iteration rather than
        // silently dropped, since this same tick already told the WebUI
        // caller the activation succeeded. A newer request replaces any
        // still-pending one (last click wins), matching how the persisted
        // "current" flag itself works.
        if (presence_snapshot.manual_activate_request_id !=
            previous_manual_activate_request_id) {
            previous_manual_activate_request_id =
                presence_snapshot.manual_activate_request_id;
            pending_manual_activate_image_id =
                presence_snapshot.manual_activate_image_id;
            pending_manual_activate = true;
            pending_manual_activate_warned = false;
        }
        if (pending_manual_activate) {
            if (carousel.request_manual(
                    pending_manual_activate_image_id,
                    carousel_items,
                    carousel_item_count)) {
                carousel.force_immediate(now_ms);
                pending_manual_activate = false;
            } else if (!pending_manual_activate_warned) {
                pending_manual_activate_warned = true;
                ESP_LOGW(
                    kTag,
                    "carousel_manual_activate_pending id=%" PRIu32,
                    pending_manual_activate_image_id);
            }
        }

        // With an empty library the welcome frame is drawn once and the
        // scheduler then parks forever, so the status bar on it keeps
        // whatever address it had when it was drawn -- at boot, none. Redraw
        // it when an address appears or changes, otherwise a device with no
        // images never shows the IP its owner needs to reach the WebUI and
        // upload the first one. Only a *present* address triggers this:
        // losing one must not spend a 31 s panel refresh on every Wi-Fi
        // hiccup, and the frame showing a since-departed address is no worse
        // than it showing none. request_welcome_redraw() refuses while a
        // decision is in flight or when a real image is on the panel, and
        // the condition simply stays true so the next tick retries.
        if (!has_displayable_image && presence_snapshot_read &&
            presence_snapshot.ip_address[0] != '\0' &&
            std::strcmp(presence_snapshot.ip_address, welcome_frame_ip) !=
                0) {
            // The condition stays true for as long as the displayed frame
            // lacks the address, so this is asked every tick. Log only when
            // the request actually brought the deadline forward, or a
            // failure backoff would fill the diagnostics ring with one
            // identical entry per second.
            const std::uint64_t due_before = carousel.next_due_ms();
            if (carousel.request_welcome_redraw(now_ms) &&
                carousel.next_due_ms() != due_before) {
                ESP_LOGI(
                    kTag,
                    "carousel_welcome_redraw_for_ip ip=%s",
                    presence_snapshot.ip_address);
            }
        }

        const pf_carousel::CarouselDecision decision =
            carousel.poll(now_ms, carousel_items, carousel_item_count);
        if (display_started &&
            decision.kind ==
                pf_carousel::DecisionKind::display_welcome) {
            if (!try_lock_carousel_submission_gate(
                    ap_presenter, ap_screen_owns_panel(now_ms))) {
                carousel.abandon(decision, now_ms + kCarouselRetryMs);
                ESP_LOGW(
                    kTag,
                    "carousel_welcome_submission_gate_busy");
            } else {
                pf_display::FrameWriteLease frame =
                    pf_display::display_task().try_acquire_frame();
                if (frame.valid() &&
                    pf_carousel::render_welcome_frame(
                        frame.data(),
                        frame.size())) {
                // Overlay the status bar onto the welcome frame's top
                // rows (the same 800x40 band image_frame.hpp composes
                // into) so date/weather show up even before any images
                // exist in the catalog.
                pf_display::PackedFramebufferView welcome_status_view(
                    frame.data(),
                    pf_display::kLandscapeStatusBytes,
                    pf_display::kPanelWidth,
                    pf_display::kStatusBarHeight);
                const pf_display::StatusBarContent welcome_status_content =
                    build_status_bar_content();
                pf_display::render_status_bar(
                    welcome_status_content, welcome_status_view);
                const std::uint32_t request_id =
                    pf_runtime::coordinator().allocate_request_id();
                const pf_display::SubmitStatus submit =
                    pf_display::display_task().try_submit_refresh(
                        request_id,
                        frame);
                if (submit == pf_display::SubmitStatus::accepted) {
                    // See submit_presence_away_blank: the AP screen's claim
                    // on the panel ends when another frame is accepted, not
                    // when that frame's result is consumed.
                    ap_presenter.screen_cache.mark_superseded();
                    active_carousel_decision = decision;
                    active_carousel_request_id = request_id;
                    // Record what this frame actually says, taken from the
                    // content just rendered rather than a second snapshot
                    // read, so an address arriving between the two could not
                    // provoke a redundant redraw. Only promoted to
                    // welcome_frame_ip once the refresh itself succeeds.
                    static_assert(
                        sizeof(submitted_welcome_ip) >=
                            pf_display::kDeviceIpCapacity,
                        "submitted_welcome_ip must hold any rendered IP");
                    if (welcome_status_content.device_ip_available) {
                        std::memcpy(
                            submitted_welcome_ip,
                            welcome_status_content.device_ip,
                            pf_display::kDeviceIpCapacity);
                    } else {
                        submitted_welcome_ip[0] = '\0';
                    }
                    submitted_welcome_ip
                        [sizeof(submitted_welcome_ip) - 1U] = '\0';
                    // No periodic weather timer (ADR-0014): kick a refresh
                    // attempt right after a real panel refresh is accepted
                    // so the status bar picks up fresher data on the
                    // following cycle. Gated on actual acceptance (not
                    // every non-wait decision) so a stuck frame
                    // pool/submit queue can't turn this into a once-a-
                    // second retry loop.
                    pf_weather::weather_worker().request_immediate_refresh();
                    ESP_LOGI(
                        kTag,
                        "carousel_welcome_queued request=%" PRIu32,
                        request_id);
                } else {
                    carousel.abandon(
                        decision,
                        now_ms + kCarouselRetryMs);
                    ESP_LOGW(
                        kTag,
                        "carousel_welcome_submit_deferred status=%u",
                        static_cast<unsigned>(submit));
                }
                } else {
                    carousel.abandon(
                        decision,
                        now_ms + kCarouselRetryMs);
                    ESP_LOGW(
                        kTag,
                        "carousel_welcome_frame_unavailable");
                }
                xSemaphoreGive(ap_presenter.display_submission_mutex);
            }
        } else if (
            display_started &&
            decision.kind == pf_carousel::DecisionKind::display_image &&
            decision.reason != pf_carousel::DecisionReason::manual &&
            first_real_image_pending &&
            !first_image_ready_or_timed_out(now_ms, boot_ms)) {
            // Not ready yet and still inside the timeout window: retry
            // shortly without disturbing scheduler state (same pattern as
            // the transient-failure retries below), instead of rendering
            // the first real image before NTP/weather had a chance to
            // resolve. A manual activate (user just picked an image in
            // the WebUI) is deliberately exempt -- this gate exists for
            // unattended startup, not to delay an explicit user action.
            carousel.abandon(decision, now_ms + kCarouselRetryMs);
        } else if (
            display_started &&
            decision.kind == pf_carousel::DecisionKind::display_image) {
            if (!try_lock_carousel_submission_gate(
                    ap_presenter, ap_screen_owns_panel(now_ms))) {
                carousel.abandon(decision, now_ms + kCarouselRetryMs);
                ESP_LOGW(
                    kTag,
                    "carousel_image_submission_gate_busy id=%" PRIu32,
                    decision.image_id);
            } else {
                pf_storage::CatalogEntry entry{};
                pf_display::FrameWriteLease frame =
                    pf_display::display_task().try_acquire_frame();
                if (frame.valid() && carousel_payload != nullptr &&
                    carousel_status != nullptr &&
                    storage_worker.find_catalog_entry_by_id(
                        decision.image_id,
                        entry) &&
                    render_carousel_image(
                        storage_worker,
                        entry,
                        carousel_payload,
                        carousel_status,
                        frame.data(),
                        frame.size(),
                        &carousel_inflate_buffers)) {
                const std::uint32_t request_id =
                    pf_runtime::coordinator().allocate_request_id();
                const pf_display::SubmitStatus submit =
                    pf_display::display_task().try_submit_refresh(
                        request_id,
                        frame);
                if (submit == pf_display::SubmitStatus::accepted) {
                    // See submit_presence_away_blank: the AP screen's claim
                    // on the panel ends when another frame is accepted, not
                    // when that frame's result is consumed.
                    ap_presenter.screen_cache.mark_superseded();
                    active_carousel_decision = decision;
                    active_carousel_request_id = request_id;
                    first_real_image_pending = false;
                    // See the matching comment on the welcome-frame path
                    // above: only trigger on an accepted submission.
                    pf_weather::weather_worker().request_immediate_refresh();
                    ESP_LOGI(
                        kTag,
                        "carousel_image_queued id=%" PRIu32
                        " request=%" PRIu32,
                        decision.image_id,
                        request_id);
                } else {
                    carousel.abandon(
                        decision,
                        now_ms + kCarouselRetryMs);
                    ESP_LOGW(
                        kTag,
                        "carousel_image_submit_deferred id=%" PRIu32
                        " status=%u",
                        decision.image_id,
                        static_cast<unsigned>(submit));
                }
                } else {
                    carousel.abandon(
                        decision,
                        now_ms + kCarouselRetryMs);
                    ESP_LOGW(
                        kTag,
                        "carousel_image_frame_unavailable id=%" PRIu32,
                        decision.image_id);
                }
                xSemaphoreGive(ap_presenter.display_submission_mutex);
            }
        } else if (
            decision.kind != pf_carousel::DecisionKind::wait) {
            carousel.abandon(
                decision,
                now_ms + kCarouselRetryMs);
        }
        }  // carousel runs unless away outside AP mode

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
