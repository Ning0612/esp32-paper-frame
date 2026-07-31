#include <cinttypes>
#include <algorithm>
#include <cstring>
#include <ctime>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
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
#include "pf_provisioning/ap_screen.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_sensor_task/sensor_task.hpp"
#include "pf_sensors/presence.hpp"
#include "pf_storage/filesystem_manager.hpp"
#include "pf_storage/catalog.hpp"
#include "pf_storage/littlefs_backend.hpp"
#include "pf_storage/storage_worker.hpp"
#include "pf_weather_worker/weather_worker.hpp"
#include "pf_web/health_server.hpp"
#include "pf_web/provisioning_service.hpp"

namespace {

constexpr char kTag[] = "paperframe";
constexpr std::uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr std::size_t kExpectedPsramBytes = 8U * 1024U * 1024U;
constexpr std::uint64_t kCarouselRetryMs = 1000U;
constexpr std::size_t kCarouselPayloadBytes =
    pf_image::kPfr1MaxPayloadBytes;

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
    bool payload_valid = false;
    pf_provisioning::AccessPointScreenPayload last_payload{};
};

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

    ESP_LOGI(
        kTag,
        "optional_sensors=not_configured gpio_probe=skipped");

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
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        // Displayed in UTC: the minimal SNTP integration in this phase
        // does not carry a configured timezone offset (see
        // docs/adr/0005-weather-worker-and-status-bar.md).
        if (gmtime_r(&now, &utc) != nullptr) {
            content.time_valid = true;
            content.year = static_cast<std::uint16_t>(utc.tm_year + 1900);
            content.month = static_cast<std::uint8_t>(utc.tm_mon + 1);
            content.day = static_cast<std::uint8_t>(utc.tm_mday);
            content.iso_weekday = static_cast<std::uint8_t>(
                utc.tm_wday == 0 ? 7 : utc.tm_wday);
        }
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

    return content;
}

bool render_carousel_image(
    pf_storage::StorageWorker& storage_worker,
    const pf_storage::CatalogEntry& entry,
    std::uint8_t* const payload,
    std::uint8_t* const status,
    std::uint8_t* const frame,
    const std::size_t frame_length)
{
    if (payload == nullptr || status == nullptr || frame == nullptr ||
        entry.name_length == 0U ||
        entry.name_length >= sizeof(entry.name)) {
        return false;
    }

    pf_carousel::Pfr1FrameDecoder decoder(payload, kCarouselPayloadBytes);
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

    pf_provisioning::AccessPointScreenPayload payload{};
    const pf_config::SecureZeroGuard payload_guard(payload);
    if (!pf_provisioning::build_access_point_screen_payload(
            info.ssid,
            info.password,
            info.device_suffix,
            payload)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (presenter->payload_valid &&
        pf_provisioning::same_access_point_screen_payload(
            presenter->last_payload,
            payload)) {
        ESP_LOGI(
            kTag,
            "provisioning_screen_unchanged refresh_skipped=true");
        return ESP_OK;
    }

    pf_display::FrameWriteLease frame =
        pf_display::display_task().try_acquire_frame();
    if (!frame.valid()) {
        return ESP_ERR_TIMEOUT;
    }
    if (!pf_provisioning::render_access_point_screen(
            frame.data(),
            frame.size(),
            payload)) {
        return ESP_FAIL;
    }
    const std::uint32_t request_id =
        pf_runtime::coordinator().allocate_request_id();
    const pf_display::SubmitStatus submitted =
        pf_display::display_task().try_submit_refresh(
            request_id,
            frame);
    if (submitted != pf_display::SubmitStatus::accepted) {
        return ESP_ERR_TIMEOUT;
    }

    while (true) {
        pf_runtime::RuntimeResult result{};
        if (pf_runtime::coordinator().try_take_terminal_result(
                request_id,
                result)) {
            if (result.display_outcome ==
                pf_runtime::DisplayOutcome::refreshed_and_slept) {
                presenter->last_payload = payload;
                presenter->payload_valid = true;
                ESP_LOGI(
                    kTag,
                    "provisioning_screen_ready request=%" PRIu32,
                    request_id);
                return ESP_OK;
            }
            ESP_LOGE(
                kTag,
                "provisioning_screen_terminal_failure request=%" PRIu32
                " outcome=%u stage=%u",
                request_id,
                static_cast<unsigned>(result.display_outcome),
                static_cast<unsigned>(result.driver_stage));
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

    pf_config::NetworkCredentialLoadResult stored_credentials =
        config_result.error == ESP_OK
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
        config_result.error == ESP_OK
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
        config_result.error == ESP_OK
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
        config_result.error == ESP_OK
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
        } else {
            ESP_LOGI(
                kTag,
                "storage_worker_ready recovery=%s action=%s",
                pf_storage::to_string(storage_startup.recovery.error),
                pf_storage::to_string(storage_startup.recovery.action));
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
    log_filesystem("webfs", filesystem_snapshot.webfs);
    log_filesystem("imagefs", filesystem_snapshot.imagefs);

    const pf_runtime::RuntimeSnapshot initial_snapshot{
        .sequence = 1,
        .flash =
            hardware.flash_ready ? pf_runtime::ServiceState::ready
                                 : pf_runtime::ServiceState::degraded,
        .psram =
            hardware.psram_ready ? pf_runtime::ServiceState::ready
                                 : pf_runtime::ServiceState::degraded,
        .config = state_from_error(config_result.error),
        .webfs = state_from_filesystem(filesystem_snapshot.webfs),
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
        .webfs_total_bytes = static_cast<std::uint32_t>(
            filesystem_snapshot.webfs.total_bytes),
        .webfs_used_bytes = static_cast<std::uint32_t>(
            filesystem_snapshot.webfs.used_bytes),
        .imagefs_total_bytes = static_cast<std::uint32_t>(
            filesystem_snapshot.imagefs.total_bytes),
        .imagefs_used_bytes = static_cast<std::uint32_t>(
            filesystem_snapshot.imagefs.used_bytes),
        .carousel_refresh_minutes = config_result.record_available
                                         ? config_result.record.refresh_minutes
                                         : 0U,
    };
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

    pf_network::NetworkCredentials network_credentials{};
    const bool password_bootstrap =
        password_status.error == ESP_OK &&
        !password_status.configured;
    if (stored_credentials.error == ESP_OK &&
        stored_credentials.configured &&
        !password_bootstrap) {
        network_credentials.configured = true;
        std::memcpy(
            network_credentials.ssid,
            stored_credentials.credentials.ssid,
            sizeof(network_credentials.ssid));
        std::memcpy(
            network_credentials.password,
            stored_credentials.credentials.password,
            sizeof(network_credentials.password));
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
    const esp_err_t network_service_result =
        network_stack_result == ESP_OK &&
                runtime_result == ESP_OK
            ? pf_network::network_service().start(
                  pf_runtime::coordinator(),
                  network_credentials,
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
            ? pf_weather_worker::weather_worker().start(
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
            ? pf_sensor_task::sensor_task().start(pf_runtime::coordinator())
            : ESP_ERR_INVALID_STATE;
    if (sensor_task_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "sensor_task_start_failed=%s; sensors stay unavailable",
            esp_err_to_name(sensor_task_result));
    }

    const esp_err_t provisioning_store_result =
        config_result.error != ESP_OK
            ? config_result.error
            : runtime_result != ESP_OK
                  ? runtime_result
                  : pf_web::provisioning_service().start(
                        pf_runtime::coordinator());
    if (provisioning_store_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "provisioning_store_start_failed=%s",
            esp_err_to_name(provisioning_store_result));
    }

    const esp_err_t authentication_result =
        config_result.error != ESP_OK
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

    const std::uint32_t refresh_minutes =
        config_result.record_available
            ? config_result.record.refresh_minutes
            : pf_config::kDefaultRefreshMinutes;
    pf_carousel::CarouselScheduler carousel{
        pf_carousel::CarouselConfig{
            refresh_minutes,
            pf_carousel::CarouselMode::sequential,
            0x50465231U,
        },
    };
    pf_carousel::CarouselDecision active_carousel_decision{};
    std::uint32_t active_carousel_request_id = 0U;
    std::uint32_t active_blank_request_id = 0U;
    pf_sensors::PresenceState previous_presence =
        pf_sensors::PresenceState::unknown;
    bool pending_presence_force_immediate = false;
    bool pending_presence_away_blank = false;
    CarouselShownState carousel_shown[pf_storage::kCatalogMaxEntries]{};
    std::size_t carousel_shown_count = 0U;
    pf_carousel::CarouselItem carousel_items[pf_storage::kCatalogMaxEntries]{};
    static std::uint8_t carousel_status[pf_display::kLandscapeStatusBytes]{};
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
    const auto submit_presence_away_blank = [&]() -> bool {
        if (active_blank_request_id != 0U) {
            return true;
        }
        pf_display::FrameWriteLease blank_frame =
            pf_display::display_task().try_acquire_frame();
        if (!blank_frame.valid() ||
            !pf_carousel::render_blank_frame(
                blank_frame.data(), blank_frame.size())) {
            ESP_LOGW(kTag, "presence_away_blank_frame_unavailable");
            return false;
        }
        const std::uint32_t blank_request_id =
            pf_runtime::coordinator().allocate_request_id();
        const pf_display::SubmitStatus blank_submit =
            pf_display::display_task().try_submit_refresh(
                blank_request_id, blank_frame);
        if (blank_submit != pf_display::SubmitStatus::accepted) {
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
        if (pf_runtime::coordinator().read_snapshot(presence_snapshot) &&
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
            if (current_presence == pf_sensors::PresenceState::away &&
                display_started) {
                pending_presence_away_blank = !submit_presence_away_blank();
            } else if (current_presence ==
                       pf_sensors::PresenceState::present) {
                // Returning from away or unknown: redraw immediately and
                // never reuse a deadline computed while away. If a
                // carousel decision is still in flight, force_immediate
                // refuses to mutate scheduler state (by contract); defer
                // and retry once that decision completes, below.
                if (carousel.force_immediate(now_ms)) {
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
            pending_presence_away_blank = !submit_presence_away_blank();
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
                active_blank_request_id = 0U;
                if (blank_result.display_outcome !=
                        pf_runtime::DisplayOutcome::refreshed_and_slept &&
                    previous_presence == pf_sensors::PresenceState::away) {
                    // Accepted into the queue but the refresh itself
                    // failed (e.g. panel/transport error): the panel
                    // never actually went blank, so retry rather than
                    // silently giving up for the rest of this away
                    // period.
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
                const bool succeeded =
                    terminal_result.display_outcome ==
                    pf_runtime::DisplayOutcome::refreshed_and_slept;
                carousel.complete(
                    active_carousel_decision,
                    succeeded,
                    now_ms);
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
                active_carousel_request_id = 0U;
            }
        }

        if (pending_presence_force_immediate && !carousel.in_flight()) {
            if (carousel.force_immediate(now_ms)) {
                pending_presence_force_immediate = false;
                ESP_LOGI(kTag, "presence_return_deadline_reset_deferred");
            }
        }

        // Guild.md 4.9: pause carousel advancement while away (an
        // in-flight decision above still gets its completion processed;
        // this only stops new work from being issued). previous_presence
        // is the last successfully observed presence (see the snapshot
        // read above), not necessarily this tick's value.
        if (previous_presence != pf_sensors::PresenceState::away) {
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

        const pf_carousel::CarouselDecision decision =
            carousel.poll(now_ms, carousel_items, carousel_item_count);
        if (display_started &&
            decision.kind ==
                pf_carousel::DecisionKind::display_welcome) {
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
                pf_display::render_status_bar(
                    build_status_bar_content(), welcome_status_view);
                const std::uint32_t request_id =
                    pf_runtime::coordinator().allocate_request_id();
                const pf_display::SubmitStatus submit =
                    pf_display::display_task().try_submit_refresh(
                        request_id,
                        frame);
                if (submit == pf_display::SubmitStatus::accepted) {
                    active_carousel_decision = decision;
                    active_carousel_request_id = request_id;
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
        } else if (
            display_started &&
            decision.kind == pf_carousel::DecisionKind::display_image) {
            pf_storage::CatalogEntry entry{};
            pf_display::FrameWriteLease frame =
                pf_display::display_task().try_acquire_frame();
            if (frame.valid() && carousel_payload != nullptr &&
                storage_worker.find_catalog_entry_by_id(
                    decision.image_id,
                    entry) &&
                render_carousel_image(
                    storage_worker,
                    entry,
                    carousel_payload,
                    carousel_status,
                    frame.data(),
                    frame.size())) {
                const std::uint32_t request_id =
                    pf_runtime::coordinator().allocate_request_id();
                const pf_display::SubmitStatus submit =
                    pf_display::display_task().try_submit_refresh(
                        request_id,
                        frame);
                if (submit == pf_display::SubmitStatus::accepted) {
                    active_carousel_decision = decision;
                    active_carousel_request_id = request_id;
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
        } else if (
            decision.kind != pf_carousel::DecisionKind::wait) {
            carousel.abandon(
                decision,
                now_ms + kCarouselRetryMs);
        }
        }  // previous_presence != away

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
