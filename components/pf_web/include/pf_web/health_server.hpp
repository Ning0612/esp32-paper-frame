#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

namespace pf_web {

esp_err_t start_health_server(httpd_handle_t* server);

}  // namespace pf_web
