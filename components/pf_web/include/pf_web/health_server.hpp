#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"
#include "pf_config/schema.hpp"
#include "pf_config/sensor_settings.hpp"
#include "pf_config/weather_settings.hpp"

namespace pf_storage {
class StorageWorker;
}

namespace pf_web {

struct HealthServerAccessConfig {
    bool initial_bootstrap = false;
    bool password_bootstrap = false;
    bool wifi_configured = false;
    bool wifi_password_configured = false;
    bool management_password_configured = false;
    std::uint32_t refresh_minutes = 0U;
    char timezone[pf_config::kTimezoneCapacity]{};
    bool weather_configured = false;
    pf_config::WeatherSettings weather_settings{};
    pf_config::SensorSettings sensor_settings{};
    pf_storage::StorageWorker* storage_worker = nullptr;
};

esp_err_t start_health_server(
    httpd_handle_t* server,
    const HealthServerAccessConfig& access);

}  // namespace pf_web
