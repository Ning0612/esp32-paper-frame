#include <cinttypes>
#include <cstring>

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
#include "pf_carousel/welcome_frame.hpp"
#include "pf_config/config_manager.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_display/display_task_esp_idf.hpp"
#include "pf_network/network_service_esp_idf.hpp"
#include "pf_provisioning/ap_screen.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_storage/filesystem_manager.hpp"
#include "pf_web/health_server.hpp"
#include "pf_web/provisioning_service.hpp"

namespace {

constexpr char kTag[] = "paperframe";
constexpr std::uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr std::size_t kExpectedPsramBytes = 8U * 1024U * 1024U;
constexpr std::uint64_t kCarouselRetryMs = 1000U;

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

    const pf_storage::FileSystemSnapshot filesystem_snapshot =
        pf_storage::mount_all();
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
        .imagefs = state_from_filesystem(filesystem_snapshot.imagefs),
        .wifi = pf_runtime::WifiState::unknown,
        .internet = pf_runtime::InternetState::unknown,
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
    };
    const char* const timezone = config_result.record_available
                                     ? config_result.record.timezone
                                     : "unknown";
    std::strncpy(
        web_access.timezone,
        timezone,
        sizeof(web_access.timezone) - 1U);
    web_access.timezone[sizeof(web_access.timezone) - 1U] = '\0';
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

    while (true) {
        const std::uint64_t now_ms = monotonic_ms();
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

        pf_runtime::RuntimeSnapshot runtime_snapshot{};
        const bool normal_network_mode =
            pf_runtime::coordinator().read_snapshot(runtime_snapshot) &&
            runtime_snapshot.wifi ==
                pf_runtime::WifiState::connected;
        if (!normal_network_mode) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const pf_carousel::CarouselDecision decision =
            carousel.poll(now_ms, nullptr, 0U);
        if (display_started &&
            decision.kind ==
                pf_carousel::DecisionKind::display_welcome) {
            pf_display::FrameWriteLease frame =
                pf_display::display_task().try_acquire_frame();
            if (frame.valid() &&
                pf_carousel::render_welcome_frame(
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
            decision.kind != pf_carousel::DecisionKind::wait) {
            carousel.abandon(
                decision,
                now_ms + kCarouselRetryMs);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
