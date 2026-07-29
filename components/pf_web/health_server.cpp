#include "pf_web/health_server.hpp"

#include <cstdint>

#include "esp_timer.h"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_web/health_serializer.hpp"

namespace pf_web {
namespace {

esp_err_t health_handler(httpd_req_t* request)
{
    pf_runtime::RuntimeSnapshot snapshot{};
    const bool snapshot_valid =
        pf_runtime::coordinator().read_snapshot(snapshot);

    char response[320]{};
    const std::uint64_t uptime_ms =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
    const SerializeResult serialized =
        serialize_health(
            snapshot,
            snapshot_valid,
            uptime_ms,
            response,
            sizeof(response));
    if (!serialized.ok) {
        return httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "health serialization failed");
    }

    esp_err_t result =
        httpd_resp_set_type(request, "application/json");
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    }
    if (result == ESP_OK) {
        result = httpd_resp_send(
            request,
            response,
            static_cast<ssize_t>(serialized.length));
    }
    return result;
}

const httpd_uri_t kHealthRoute{
    .uri = "/api/v1/health",
    .method = HTTP_GET,
    .handler = health_handler,
    .user_ctx = nullptr,
};

}  // namespace

esp_err_t start_health_server(httpd_handle_t* server)
{
    if (server == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
    configuration.max_uri_handlers = 4;

    esp_err_t result = httpd_start(server, &configuration);
    if (result != ESP_OK) {
        return result;
    }

    result = httpd_register_uri_handler(*server, &kHealthRoute);
    if (result != ESP_OK) {
        httpd_stop(*server);
        *server = nullptr;
    }
    return result;
}

}  // namespace pf_web
