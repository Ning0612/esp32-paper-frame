#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

namespace pf_web {

struct HealthServerAccessConfig {
    bool initial_bootstrap = false;
    bool management_password_configured = true;
};

esp_err_t start_health_server(
    httpd_handle_t* server,
    const HealthServerAccessConfig& access);

}  // namespace pf_web
