#include <cinttypes>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_config/config_manager.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_storage/filesystem_manager.hpp"
#include "pf_web/health_server.hpp"

namespace {

constexpr char kTag[] = "paperframe";
constexpr std::uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr std::size_t kExpectedPsramBytes = 8U * 1024U * 1024U;

struct HardwareProfile {
    bool flash_ready;
    bool psram_ready;
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

}  // namespace

extern "C" void app_main()
{
    ESP_LOGI(kTag, "PaperFrame Phase 1 bring-up");
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

    const pf_storage::FileSystemSnapshot filesystem_snapshot =
        pf_storage::mount_all();
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
    };
    const esp_err_t runtime_result =
        pf_runtime::coordinator().initialize(initial_snapshot);
    if (runtime_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "runtime_coordinator_init_failed=%s; continuing without queues",
            esp_err_to_name(runtime_result));
    }

    const esp_err_t network_stack_result = esp_netif_init();
    if (network_stack_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "network_stack_init_failed=%s; health server disabled",
            esp_err_to_name(network_stack_result));
    }

    httpd_handle_t health_server = nullptr;
    const esp_err_t health_result =
        network_stack_result == ESP_OK
            ? pf_web::start_health_server(&health_server)
            : network_stack_result;
    if (health_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "health_server_start_failed=%s; continuing degraded",
            esp_err_to_name(health_result));
    } else {
        ESP_LOGI(
            kTag,
            "health_server_ready route=/api/v1/health network=not_configured");
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
