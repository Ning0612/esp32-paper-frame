#include "pf_web/health_server.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"
#include "pf_config/network_credentials.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_network/network_service_esp_idf.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_web/access_policy.hpp"
#include "pf_web/health_serializer.hpp"
#include "pf_web/http_receive_policy.hpp"
#include "pf_web/provisioning_form.hpp"
#include "pf_web/provisioning_service.hpp"

namespace pf_web {
namespace {

struct StaticAsset {
    const char* path;
    const char* content_type;
};

constexpr StaticAsset kIndexAsset{
    "/web/index.html",
    "text/html; charset=utf-8",
};
constexpr StaticAsset kStyleAsset{
    "/web/style.css",
    "text/css; charset=utf-8",
};
constexpr StaticAsset kScriptAsset{
    "/web/ui.js",
    "application/javascript; charset=utf-8",
};
constexpr StaticAsset kFaviconAsset{
    "/web/favicon.svg",
    "image/svg+xml",
};
constexpr std::uint32_t kMaximumBodyReceiveMs = 15000U;
constexpr std::uint8_t kMaximumBodyTimeouts = 3U;

HealthServerAccessConfig server_access_config{};

esp_err_t set_common_headers(
    httpd_req_t* const request,
    const char* const content_type)
{
    esp_err_t result =
        httpd_resp_set_type(request, content_type);
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(
            request,
            "Cache-Control",
            "no-store");
    }
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(
            request,
            "X-Content-Type-Options",
            "nosniff");
    }
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(
            request,
            "Content-Security-Policy",
            "default-src 'self'; connect-src 'self'; "
            "img-src 'self'; style-src 'self'; script-src 'self'");
    }
    return result;
}

esp_err_t send_json(
    httpd_req_t* const request,
    const char* const status,
    const char* const body)
{
    esp_err_t result =
        set_common_headers(
            request,
            "application/json; charset=utf-8");
    if (result == ESP_OK && status != nullptr) {
        result = httpd_resp_set_status(request, status);
    }
    if (result == ESP_OK) {
        result = httpd_resp_sendstr(request, body);
    }
    return result;
}

bool provisioning_mode_active()
{
    pf_runtime::RuntimeSnapshot snapshot{};
    return pf_runtime::coordinator().read_snapshot(snapshot) &&
           snapshot.wifi ==
               pf_runtime::WifiState::provisioning;
}

AccessContext current_access_context()
{
    return {
        .provisioning_ap = provisioning_mode_active(),
        .initial_bootstrap =
            server_access_config.initial_bootstrap,
        .management_password_configured =
            server_access_config.management_password_configured,
        .authenticated = false,
        .csrf_valid = false,
    };
}

esp_err_t static_asset_handler(httpd_req_t* request)
{
    const auto* const asset =
        static_cast<const StaticAsset*>(request->user_ctx);
    if (asset == nullptr) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"asset_context\"}");
    }

    std::FILE* const file =
        std::fopen(asset->path, "rb");
    if (file == nullptr) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"webfs_unavailable\"}");
    }
    esp_err_t result =
        set_common_headers(request, asset->content_type);
    char chunk[512]{};
    while (result == ESP_OK) {
        const std::size_t length =
            std::fread(chunk, 1U, sizeof(chunk), file);
        if (length == 0U) {
            break;
        }
        result = httpd_resp_send_chunk(
            request,
            chunk,
            static_cast<ssize_t>(length));
    }
    std::fclose(file);
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, nullptr, 0);
    }
    return result;
}

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
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"health_serialization\"}");
    }
    return send_json(request, nullptr, response);
}

bool query_requests_refresh(httpd_req_t* request)
{
    const std::size_t query_length =
        httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= 48U) {
        return false;
    }
    char query[48]{};
    char value[8]{};
    return httpd_req_get_url_query_str(
               request,
               query,
               sizeof(query)) == ESP_OK &&
           httpd_query_key_value(
               query,
               "refresh",
               value,
               sizeof(value)) == ESP_OK &&
           std::strcmp(value, "1") == 0;
}

bool escape_json_string(
    const char* const source,
    char* const destination,
    const std::size_t capacity)
{
    if (source == nullptr || destination == nullptr ||
        capacity == 0U) {
        return false;
    }
    std::size_t output = 0U;
    for (std::size_t input = 0U;
         source[input] != '\0';
         ++input) {
        const auto value =
            static_cast<std::uint8_t>(source[input]);
        if (value < 0x20U) {
            if (output + 6U >= capacity) {
                return false;
            }
            constexpr char kHex[] = "0123456789abcdef";
            destination[output++] = '\\';
            destination[output++] = 'u';
            destination[output++] = '0';
            destination[output++] = '0';
            destination[output++] = kHex[value >> 4U];
            destination[output++] = kHex[value & 0x0FU];
            continue;
        }
        const bool escaped = value == '"' || value == '\\';
        if (output + (escaped ? 2U : 1U) >= capacity) {
            return false;
        }
        if (escaped) {
            destination[output++] = '\\';
        }
        destination[output++] = static_cast<char>(value);
    }
    destination[output] = '\0';
    return true;
}

esp_err_t scan_handler(httpd_req_t* request)
{
    if (!wifi_scan_allowed(current_access_context())) {
        return send_json(
            request,
            "401 Unauthorized",
            "{\"ok\":false,\"error\":\"authentication_required\"}");
    }

    pf_network::ScanSnapshot snapshot{};
    if (!pf_network::network_service().scan_snapshot(snapshot)) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"scan_unavailable\"}");
    }

    if (query_requests_refresh(request) ||
        snapshot.state == pf_network::ScanState::idle ||
        snapshot.state == pf_network::ScanState::failed) {
        if (!pf_network::network_service().request_scan()) {
            return send_json(
                request,
                "503 Service Unavailable",
                "{\"ok\":false,\"error\":\"scan_queue_full\"}");
        }
        return send_json(
            request,
            "202 Accepted",
            "{\"ok\":true,\"data\":{\"state\":\"scanning\"}}");
    }
    if (snapshot.state == pf_network::ScanState::scanning) {
        return send_json(
            request,
            "202 Accepted",
            "{\"ok\":true,\"data\":{\"state\":\"scanning\"}}");
    }

    esp_err_t result =
        set_common_headers(
            request,
            "application/json; charset=utf-8");
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(
            request,
            "{\"ok\":true,\"data\":{\"state\":\"ready\","
            "\"networks\":[",
            HTTPD_RESP_USE_STRLEN);
    }
    for (std::size_t index = 0U;
         result == ESP_OK && index < snapshot.count;
         ++index) {
        char escaped_ssid[200]{};
        char item[304]{};
        if (!escape_json_string(
                snapshot.results[index].ssid,
                escaped_ssid,
                sizeof(escaped_ssid))) {
            return ESP_FAIL;
        }
        const int written = std::snprintf(
            item,
            sizeof(item),
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"security\":\"%s\"}",
            index == 0U ? "" : ",",
            escaped_ssid,
            static_cast<int>(snapshot.results[index].rssi),
            pf_network::to_string(
                snapshot.results[index].security));
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= sizeof(item)) {
            return ESP_FAIL;
        }
        result = httpd_resp_send_chunk(
            request,
            item,
            written);
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(
            request,
            "]}}",
            HTTPD_RESP_USE_STRLEN);
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, nullptr, 0);
    }
    return result;
}

esp_err_t wifi_config_handler(httpd_req_t* request)
{
    if (!wifi_config_allowed(current_access_context())) {
        return send_json(
            request,
            "401 Unauthorized",
            "{\"ok\":false,\"error\":\"authentication_required\"}");
    }
    if (request->content_len <= 0 ||
        request->content_len >= 320) {
        return send_json(
            request,
            "413 Payload Too Large",
            "{\"ok\":false,\"error\":\"invalid_body\"}");
    }

    char body[320]{};
    const pf_config::SecureZeroGuard body_guard(body);
    std::size_t received = 0U;
    std::uint8_t timeout_count = 0U;
    const std::uint64_t receive_started_ms =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
    while (received <
           static_cast<std::size_t>(request->content_len)) {
        const std::uint64_t now_ms =
            static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
        if (body_receive_deadline_expired(
                now_ms,
                receive_started_ms,
                kMaximumBodyReceiveMs)) {
            return send_json(
                request,
                "408 Request Timeout",
                "{\"ok\":false,\"error\":\"body_timeout\"}");
        }
        const int result = httpd_req_recv(
            request,
            body + received,
            request->content_len -
                static_cast<int>(received));
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            ++timeout_count;
            if (body_receive_idle_limit_reached(
                    timeout_count,
                    kMaximumBodyTimeouts)) {
                return send_json(
                    request,
                    "408 Request Timeout",
                    "{\"ok\":false,\"error\":\"body_timeout\"}");
            }
            continue;
        }
        if (result <= 0) {
            return send_json(
                request,
                "400 Bad Request",
                "{\"ok\":false,\"error\":\"body_receive_failed\"}");
        }
        const std::uint64_t received_at_ms =
            static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
        if (body_receive_deadline_expired(
                received_at_ms,
                receive_started_ms,
                kMaximumBodyReceiveMs)) {
            return send_json(
                request,
                "408 Request Timeout",
                "{\"ok\":false,\"error\":\"body_timeout\"}");
        }
        received += static_cast<std::size_t>(result);
    }

    ProvisioningForm form{};
    const pf_config::SecureZeroGuard form_guard(form);
    const ProvisioningParseStatus parsed =
        parse_provisioning_form(body, received, form);
    if (parsed != ProvisioningParseStatus::ok) {
        char response[112]{};
        std::snprintf(
            response,
            sizeof(response),
            "{\"ok\":false,\"error\":\"%s\"}",
            to_string(parsed));
        return send_json(
            request,
            "400 Bad Request",
            response);
    }

    pf_config::NetworkCredentials credentials{};
    const pf_config::SecureZeroGuard credentials_guard(credentials);
    std::memcpy(
        credentials.ssid,
        form.ssid,
        sizeof(form.ssid));
    std::memcpy(
        credentials.password,
        form.password,
        sizeof(form.password));
    const ProvisioningSubmitResult submitted =
        provisioning_service().submit(credentials);
    if (submitted.status == ProvisioningSubmitStatus::busy) {
        return send_json(
            request,
            "409 Conflict",
            "{\"ok\":false,\"error\":\"provisioning_busy\"}");
    }
    if (submitted.status != ProvisioningSubmitStatus::accepted) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    char response[160]{};
    const ProvisioningOperationStatus status{
        .request_id = submitted.request_id,
        .state = ProvisioningOperationState::saving,
    };
    if (!serialize_provisioning_status(
            status,
            response,
            sizeof(response))) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"status_serialization\"}");
    }
    return send_json(
        request,
        "202 Accepted",
        response);
}

bool query_request_id(
    httpd_req_t* const request,
    std::uint32_t& request_id)
{
    const std::size_t query_length =
        httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= 64U) {
        return false;
    }
    char query[64]{};
    char value[16]{};
    if (httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)) != ESP_OK ||
        httpd_query_key_value(
            query,
            "request_id",
            value,
            sizeof(value)) != ESP_OK ||
        value[0] == '\0') {
        return false;
    }

    std::uint32_t parsed = 0U;
    for (std::size_t index = 0U;
         value[index] != '\0';
         ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
        const std::uint32_t digit =
            static_cast<std::uint32_t>(value[index] - '0');
        if (parsed >
            (UINT32_MAX - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    request_id = parsed;
    return request_id != 0U;
}

esp_err_t wifi_config_status_handler(httpd_req_t* request)
{
    if (!wifi_scan_allowed(current_access_context())) {
        return send_json(
            request,
            "401 Unauthorized",
            "{\"ok\":false,\"error\":\"authentication_required\"}");
    }

    std::uint32_t request_id = 0U;
    if (!query_request_id(request, request_id)) {
        return send_json(
            request,
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"invalid_request_id\"}");
    }
    ProvisioningOperationStatus status{};
    if (!provisioning_service().status(status)) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    if (!provisioning_status_matches(status, request_id)) {
        return send_json(
            request,
            "404 Not Found",
            "{\"ok\":false,\"error\":\"request_not_found\"}");
    }

    char response[160]{};
    if (!serialize_provisioning_status(
            status,
            response,
            sizeof(response))) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"status_serialization\"}");
    }
    const char* const http_status =
        status.state == ProvisioningOperationState::saving
            ? "202 Accepted"
            : status.state == ProvisioningOperationState::failed
                  ? "500 Internal Server Error"
                  : nullptr;
    const esp_err_t result =
        send_json(request, http_status, response);
    if (result == ESP_OK &&
        (status.state ==
             ProvisioningOperationState::committed ||
         status.state ==
             ProvisioningOperationState::failed)) {
        provisioning_service().acknowledge_terminal(
            request_id);
    }
    return result;
}

const httpd_uri_t kHealthRoute{
    .uri = "/api/v1/health",
    .method = HTTP_GET,
    .handler = health_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kScanRoute{
    .uri = "/api/v1/wifi/scan",
    .method = HTTP_GET,
    .handler = scan_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kWifiConfigRoute{
    .uri = "/api/v1/wifi/config",
    .method = HTTP_POST,
    .handler = wifi_config_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kWifiConfigStatusRoute{
    .uri = "/api/v1/wifi/config/status",
    .method = HTTP_GET,
    .handler = wifi_config_status_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kIndexRoute{
    .uri = "/",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kIndexAsset),
};
const httpd_uri_t kStyleRoute{
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kStyleAsset),
};
const httpd_uri_t kScriptRoute{
    .uri = "/ui.js",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kScriptAsset),
};
const httpd_uri_t kFaviconRoute{
    .uri = "/favicon.svg",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kFaviconAsset),
};

}  // namespace

esp_err_t start_health_server(
    httpd_handle_t* server,
    const HealthServerAccessConfig& access)
{
    if (server == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
    configuration.max_uri_handlers = 9;
    configuration.recv_wait_timeout = 5;
    server_access_config = access;

    esp_err_t result = httpd_start(server, &configuration);
    if (result != ESP_OK) {
        return result;
    }

    const httpd_uri_t* const routes[] = {
        &kHealthRoute,
        &kScanRoute,
        &kWifiConfigRoute,
        &kWifiConfigStatusRoute,
        &kIndexRoute,
        &kStyleRoute,
        &kScriptRoute,
        &kFaviconRoute,
    };
    for (const httpd_uri_t* const route : routes) {
        result = httpd_register_uri_handler(*server, route);
        if (result != ESP_OK) {
            httpd_stop(*server);
            *server = nullptr;
            return result;
        }
    }
    return ESP_OK;
}

}  // namespace pf_web
