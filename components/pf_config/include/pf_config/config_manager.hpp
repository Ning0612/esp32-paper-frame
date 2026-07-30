#pragma once

#include "esp_err.h"
#include "pf_config/management_password.hpp"
#include "pf_config/network_credentials.hpp"
#include "pf_config/schema.hpp"
#include "pf_config/weather_settings.hpp"

namespace pf_config {

struct StartupResult {
    SchemaAction action;
    esp_err_t error;
    bool record_available;
    ConfigRecord record;
};

StartupResult initialize();

struct NetworkCredentialLoadResult {
    esp_err_t error;
    bool configured;
    NetworkCredentials credentials;
};

NetworkCredentialLoadResult load_network_credentials();
esp_err_t save_network_credentials(
    const NetworkCredentials& credentials);

struct ManagementPasswordStatus {
    esp_err_t error;
    bool configured;
};

ManagementPasswordStatus management_password_status();

struct ManagementPasswordLoadResult {
    esp_err_t error;
    bool configured;
    ManagementPasswordHash record;
};

ManagementPasswordLoadResult load_management_password();
esp_err_t save_management_password(
    const ManagementPasswordHash& record);

struct WeatherSettingsLoadResult {
    esp_err_t error;
    bool configured;
    WeatherSettings settings;
};

WeatherSettingsLoadResult load_weather_settings();
esp_err_t save_weather_settings(const WeatherSettings& settings);

}  // namespace pf_config
