#include <cinttypes>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_config/config_manager.hpp"

namespace {

constexpr char kTag[] = "paperframe";
constexpr std::uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr std::size_t kExpectedPsramBytes = 8U * 1024U * 1024U;

void log_hardware_profile()
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
}

}  // namespace

extern "C" void app_main()
{
    ESP_LOGI(kTag, "PaperFrame Phase 1 bring-up");
    log_hardware_profile();

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

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
