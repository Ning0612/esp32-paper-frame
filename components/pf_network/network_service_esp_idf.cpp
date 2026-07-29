#include "pf_network/network_service_esp_idf.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "pf_runtime/runtime_coordinator.hpp"

namespace pf_network {
namespace {

constexpr char kTag[] = "pf_network";

bool copy_text(
    std::uint8_t* const destination,
    const std::size_t destination_size,
    const char* const source)
{
    if (destination == nullptr || source == nullptr) {
        return false;
    }
    const std::size_t length = std::strlen(source);
    if (length >= destination_size) {
        return false;
    }
    std::memcpy(destination, source, length + 1U);
    return true;
}

bool ignorable_stop_error(const esp_err_t error)
{
    return error == ESP_OK || error == ESP_ERR_WIFI_NOT_STARTED;
}

}  // namespace

esp_err_t NetworkService::start(
    pf_runtime::RuntimeCoordinator& runtime,
    const NetworkCredentials& credentials)
{
    if (task_handle_ != nullptr || event_queue_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (credentials.configured &&
        (credentials.ssid[0] == '\0' ||
         std::memchr(
             credentials.ssid,
             '\0',
             sizeof(credentials.ssid)) == nullptr ||
         std::memchr(
             credentials.password,
             '\0',
             sizeof(credentials.password)) == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime_ = &runtime;
    credentials_ = credentials;
    if (!build_access_point_info()) {
        runtime_ = nullptr;
        credentials_ = {};
        return ESP_FAIL;
    }

    event_queue_ = xQueueCreateStatic(
        kEventQueueLength,
        sizeof(NetworkEvent),
        event_queue_storage_,
        &event_queue_control_);
    if (event_queue_ == nullptr) {
        runtime_ = nullptr;
        credentials_ = {};
        return ESP_ERR_NO_MEM;
    }

    task_handle_ = xTaskCreateStatic(
        &NetworkService::task_entry,
        "NetworkServiceTask",
        kTaskStackWords,
        this,
        kTaskPriority,
        task_stack_,
        &task_control_);
    if (task_handle_ == nullptr) {
        event_queue_ = nullptr;
        runtime_ = nullptr;
        credentials_ = {};
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool NetworkService::access_point_info(
    AccessPointInfo& destination) const
{
    if (!access_point_info_ready_) {
        return false;
    }
    destination = access_point_;
    return true;
}

void NetworkService::task_entry(void* context)
{
    static_cast<NetworkService*>(context)->task_main();
}

void NetworkService::event_handler(
    void* context,
    const esp_event_base_t event_base,
    const std::int32_t event_id,
    void*)
{
    NetworkService& service =
        *static_cast<NetworkService*>(context);
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {
        service.enqueue_event(NetworkEvent::sta_disconnected);
    } else if (
        event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {
        service.enqueue_event(NetworkEvent::sta_got_ip);
    }
}

void NetworkService::task_main()
{
    const esp_err_t initialized = initialize_wifi();
    if (initialized != ESP_OK) {
        ESP_LOGE(
            kTag,
            "wifi_initialize_failed=%s",
            esp_err_to_name(initialized));
        state_machine_.handle(
            NetworkEvent::wifi_initialize_failed);
        publish_state();
        vTaskSuspend(nullptr);
    }

    NetworkAction action =
        state_machine_.start(credentials_.configured);
    publish_state();
    perform_action_chain(action);

    while (true) {
        NetworkEvent event{};
        const bool awaiting_station =
            state_machine_.mode() == NetworkMode::connecting_wifi ||
            state_machine_.mode() == NetworkMode::offline_retry;
        const TickType_t wait_ticks =
            awaiting_station ? kStaAttemptTimeoutTicks : portMAX_DELAY;
        if (xQueueReceive(
                event_queue_,
                &event,
                wait_ticks) != pdTRUE) {
            event = NetworkEvent::sta_connect_timeout;
        }

        action = state_machine_.handle(event);
        publish_state();
        perform_action_chain(action);
    }
}

void NetworkService::perform_action_chain(NetworkAction action)
{
    while (action != NetworkAction::none) {
        const esp_err_t result = apply(action);
        NetworkEvent completion =
            NetworkEvent::sta_connect_timeout;
        if (action == NetworkAction::start_ap) {
            completion =
                result == ESP_OK
                    ? NetworkEvent::ap_started
                    : NetworkEvent::ap_start_failed;
        } else if (result == ESP_OK) {
            return;
        }

        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "network_action_failed action=%u error=%s",
                static_cast<unsigned>(action),
                esp_err_to_name(result));
            vTaskDelay(kActionRetryDelayTicks);
        }
        action = state_machine_.handle(completion);
        publish_state();
    }
}

esp_err_t NetworkService::initialize_wifi()
{
    const esp_err_t event_loop_result =
        esp_event_loop_create_default();
    if (event_loop_result != ESP_OK &&
        event_loop_result != ESP_ERR_INVALID_STATE) {
        return event_loop_result;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr ||
        esp_netif_create_default_wifi_ap() == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t configuration = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t result = esp_wifi_init(&configuration);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &NetworkService::event_handler,
        this,
        &wifi_event_instance_);
    if (result != ESP_OK) {
        return result;
    }
    return esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &NetworkService::event_handler,
        this,
        &ip_event_instance_);
}

esp_err_t NetworkService::apply(const NetworkAction action)
{
    switch (action) {
        case NetworkAction::none:
            return ESP_OK;
        case NetworkAction::start_sta:
            return start_station();
        case NetworkAction::retry_sta:
            return esp_wifi_connect();
        case NetworkAction::start_ap:
            return start_access_point();
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t NetworkService::start_station()
{
    const esp_err_t stop_result = esp_wifi_stop();
    if (!ignorable_stop_error(stop_result)) {
        return stop_result;
    }
    wifi_config_t configuration{};
    if (!copy_text(
            configuration.sta.ssid,
            sizeof(configuration.sta.ssid),
            credentials_.ssid) ||
        !copy_text(
            configuration.sta.password,
            sizeof(configuration.sta.password),
            credentials_.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    configuration.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result == ESP_OK) {
        result = esp_wifi_set_config(
            WIFI_IF_STA,
            &configuration);
    }
    if (result == ESP_OK) {
        result = esp_wifi_start();
    }
    if (result == ESP_OK) {
        result = esp_wifi_connect();
    }
    return result;
}

esp_err_t NetworkService::start_access_point()
{
    const esp_err_t stop_result = esp_wifi_stop();
    if (!ignorable_stop_error(stop_result)) {
        return stop_result;
    }
    wifi_config_t configuration{};
    if (!copy_text(
            configuration.ap.ssid,
            sizeof(configuration.ap.ssid),
            access_point_.ssid) ||
        !copy_text(
            configuration.ap.password,
            sizeof(configuration.ap.password),
            access_point_.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    configuration.ap.ssid_len =
        static_cast<std::uint8_t>(
            std::strlen(access_point_.ssid));
    configuration.ap.channel = 1U;
    configuration.ap.max_connection = 4U;
    configuration.ap.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) {
        result = esp_wifi_set_config(
            WIFI_IF_AP,
            &configuration);
    }
    if (result == ESP_OK) {
        result = esp_wifi_start();
    }
    if (result == ESP_OK) {
        ESP_LOGI(
            kTag,
            "provisioning_ap_ready ssid=%s ip=192.168.4.1",
            access_point_.ssid);
    }
    return result;
}

void NetworkService::publish_state()
{
    if (runtime_ == nullptr) {
        return;
    }

    pf_runtime::WifiState wifi = pf_runtime::WifiState::unknown;
    switch (state_machine_.wifi_state()) {
        case WifiState::unknown:
            wifi = pf_runtime::WifiState::unknown;
            break;
        case WifiState::connecting:
            wifi = pf_runtime::WifiState::connecting;
            break;
        case WifiState::connected:
            wifi = pf_runtime::WifiState::connected;
            break;
        case WifiState::starting_ap:
            wifi = pf_runtime::WifiState::starting_ap;
            break;
        case WifiState::provisioning:
            wifi = pf_runtime::WifiState::provisioning;
            break;
        case WifiState::failed:
            wifi = pf_runtime::WifiState::failed;
            break;
    }

    pf_runtime::InternetState internet =
        pf_runtime::InternetState::unknown;
    if (state_machine_.internet_state() ==
        InternetState::reachable) {
        internet = pf_runtime::InternetState::reachable;
    } else if (
        state_machine_.internet_state() ==
        InternetState::unreachable) {
        internet = pf_runtime::InternetState::unreachable;
    }
    runtime_->update_network(wifi, internet);
}

bool NetworkService::enqueue_event(const NetworkEvent event)
{
    return event_queue_ != nullptr &&
           xQueueSend(event_queue_, &event, 0) == pdTRUE;
}

bool NetworkService::build_access_point_info()
{
    std::uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        return false;
    }
    const int suffix_written = std::snprintf(
        access_point_.device_suffix,
        sizeof(access_point_.device_suffix),
        "%02X%02X",
        mac[4],
        mac[5]);
    const int ssid_written = std::snprintf(
        access_point_.ssid,
        sizeof(access_point_.ssid),
        "PaperFrame-Setup-%s",
        access_point_.device_suffix);
    const int password_written = std::snprintf(
        access_point_.password,
        sizeof(access_point_.password),
        "Paper-%s",
        access_point_.device_suffix);
    access_point_info_ready_ =
        suffix_written == 4 &&
        ssid_written > 0 &&
        static_cast<std::size_t>(ssid_written) <
            sizeof(access_point_.ssid) &&
        password_written >= 8 &&
        static_cast<std::size_t>(password_written) <
            sizeof(access_point_.password);
    return access_point_info_ready_;
}

NetworkService& network_service()
{
    static NetworkService instance;
    return instance;
}

}  // namespace pf_network
