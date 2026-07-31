#include "pf_network/network_service_esp_idf.hpp"

#include <cstdio>
#include <cstring>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "pf_config/config_manager.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_network/access_point_credentials.hpp"
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
    const pf_config::NetworkCredentials& credentials,
    const bool configured,
    const AccessPointPresenter presenter,
    void* const presenter_context)
{
    if (task_handle_ != nullptr || event_queue_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (configured &&
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
    configured_ = configured;
    presenter_ = presenter;
    presenter_context_ = presenter_context;
    if (!build_access_point_info()) {
        runtime_ = nullptr;
        credentials_ = {};
        configured_ = false;
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
        configured_ = false;
        presenter_ = nullptr;
        presenter_context_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    scan_mutex_ =
        xSemaphoreCreateMutexStatic(&scan_mutex_control_);
    if (scan_mutex_ == nullptr) {
        event_queue_ = nullptr;
        runtime_ = nullptr;
        credentials_ = {};
        configured_ = false;
        presenter_ = nullptr;
        presenter_context_ = nullptr;
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
        scan_mutex_ = nullptr;
        runtime_ = nullptr;
        credentials_ = {};
        presenter_ = nullptr;
        presenter_context_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool NetworkService::request_scan()
{
    if (scan_mutex_ == nullptr ||
        xSemaphoreTake(
            scan_mutex_,
            pdMS_TO_TICKS(50U)) != pdTRUE) {
        return false;
    }
    const bool enqueue =
        should_enqueue_scan_request(
            scan_request_pending_,
            scan_snapshot_.state == ScanState::scanning);
    if (!enqueue) {
        xSemaphoreGive(scan_mutex_);
        return true;
    }
    scan_request_pending_ = true;
    xSemaphoreGive(scan_mutex_);

    if (enqueue_event(NetworkEvent::scan_requested)) {
        return true;
    }
    if (xSemaphoreTake(
            scan_mutex_,
            pdMS_TO_TICKS(50U)) == pdTRUE) {
        scan_request_pending_ = false;
        xSemaphoreGive(scan_mutex_);
    }
    return false;
}

bool NetworkService::report_internet_state(const bool reachable)
{
    return enqueue_event(
        reachable ? NetworkEvent::internet_reachable
                  : NetworkEvent::internet_unreachable);
}

bool NetworkService::scan_snapshot(
    ScanSnapshot& destination)
{
    if (scan_mutex_ == nullptr ||
        xSemaphoreTake(
            scan_mutex_,
            pdMS_TO_TICKS(50U)) != pdTRUE) {
        return false;
    }
    destination = scan_snapshot_;
    xSemaphoreGive(scan_mutex_);
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
        state_machine_.start(configured_);
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

        if (event == NetworkEvent::scan_requested) {
            begin_scan();
            continue;
        }
        if (event == NetworkEvent::sntp_time_synced) {
            time_sync_state_ = next_time_sync_state(
                time_sync_state_,
                TimeSyncEvent::sntp_synced);
            publish_state();
            continue;
        }
        if (event == NetworkEvent::sta_got_ip) {
            time_sync_state_ = next_time_sync_state(
                time_sync_state_,
                TimeSyncEvent::wifi_connected);
            maybe_start_sntp();
        } else if (
            event == NetworkEvent::sta_disconnected ||
            event == NetworkEvent::sta_connect_timeout) {
            time_sync_state_ = next_time_sync_state(
                time_sync_state_,
                TimeSyncEvent::wifi_disconnected);
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
    presentation_confirmed_ = false;
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
    if (presenter_ != nullptr && !presentation_confirmed_) {
        const esp_err_t present_result =
            presenter_(
                presenter_context_,
                access_point_);
        if (present_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "provisioning_screen_failed=%s; AP start deferred",
                esp_err_to_name(present_result));
            return present_result;
        }
        presentation_confirmed_ = true;
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

    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_APSTA);
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

void NetworkService::begin_scan()
{
    if (!scan_allowed_in_mode(state_machine_.mode())) {
        fail_scan(ESP_ERR_INVALID_STATE);
        return;
    }

    if (scan_mutex_ != nullptr &&
        xSemaphoreTake(scan_mutex_, portMAX_DELAY) == pdTRUE) {
        if (scan_snapshot_.state == ScanState::scanning) {
            scan_request_pending_ = false;
            xSemaphoreGive(scan_mutex_);
            return;
        }
        scan_request_pending_ = false;
        scan_snapshot_.state = ScanState::scanning;
        scan_snapshot_.error = ESP_OK;
        scan_snapshot_.count = 0U;
        xSemaphoreGive(scan_mutex_);
    }

    const esp_err_t result =
        esp_wifi_scan_start(nullptr, true);
    if (result != ESP_OK) {
        fail_scan(result);
        return;
    }
    collect_scan_results();
}

void NetworkService::collect_scan_results()
{
    constexpr std::size_t kDriverRecordCapacity = 32U;
    wifi_ap_record_t driver_records[kDriverRecordCapacity]{};
    std::uint16_t count =
        static_cast<std::uint16_t>(kDriverRecordCapacity);
    const esp_err_t result =
        esp_wifi_scan_get_ap_records(
            &count,
            driver_records);

    RawScanRecord raw[kDriverRecordCapacity]{};
    if (result == ESP_OK) {
        for (std::size_t index = 0U;
             index < count;
             ++index) {
            std::memcpy(
                raw[index].ssid,
                driver_records[index].ssid,
                sizeof(driver_records[index].ssid));
            raw[index].ssid[
                sizeof(raw[index].ssid) - 1U] = '\0';
            raw[index].rssi = driver_records[index].rssi;
            switch (driver_records[index].authmode) {
                case WIFI_AUTH_OPEN:
                    raw[index].security = WifiSecurity::open;
                    break;
                case WIFI_AUTH_WEP:
                    raw[index].security = WifiSecurity::wep;
                    break;
                case WIFI_AUTH_WPA_PSK:
                    raw[index].security = WifiSecurity::wpa;
                    break;
                case WIFI_AUTH_WPA2_PSK:
                case WIFI_AUTH_WPA_WPA2_PSK:
                    raw[index].security = WifiSecurity::wpa2;
                    break;
                case WIFI_AUTH_WPA3_PSK:
                case WIFI_AUTH_WPA2_WPA3_PSK:
                    raw[index].security = WifiSecurity::wpa3;
                    break;
                case WIFI_AUTH_WPA2_ENTERPRISE:
                    raw[index].security = WifiSecurity::enterprise;
                    break;
                default:
                    raw[index].security = WifiSecurity::unknown;
                    break;
            }
        }
    }

    if (scan_mutex_ == nullptr ||
        xSemaphoreTake(scan_mutex_, portMAX_DELAY) != pdTRUE) {
        return;
    }
    scan_snapshot_.error = result;
    scan_snapshot_.count =
        result == ESP_OK
            ? normalize_scan_results(
                  raw,
                  count,
                  scan_snapshot_.results,
                  kMaximumScanResults)
            : 0U;
    scan_snapshot_.state =
        result == ESP_OK ? ScanState::ready
                         : ScanState::failed;
    scan_request_pending_ = false;
    ++scan_snapshot_.generation;
    xSemaphoreGive(scan_mutex_);
}

void NetworkService::fail_scan(const esp_err_t error)
{
    if (scan_mutex_ == nullptr ||
        xSemaphoreTake(
            scan_mutex_,
            portMAX_DELAY) != pdTRUE) {
        return;
    }
    scan_snapshot_.state = ScanState::failed;
    scan_snapshot_.error = error;
    scan_snapshot_.count = 0U;
    scan_request_pending_ = false;
    ++scan_snapshot_.generation;
    xSemaphoreGive(scan_mutex_);
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

    // Authoritative fallback: publish_state() runs on every processed
    // network event, so polling here (cheap, non-blocking) catches a
    // completed sync even if the notification callback fired inside the
    // narrow window before it was registered, or its queued event was
    // dropped because the event queue was momentarily full.
    if (sntp_started_ &&
        time_sync_state_ != TimeSyncState::synced &&
        sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_sync_state_ = TimeSyncState::synced;
    }

    pf_runtime::TimeSyncState time_sync =
        pf_runtime::TimeSyncState::unsynced;
    switch (time_sync_state_) {
        case TimeSyncState::unsynced:
            time_sync = pf_runtime::TimeSyncState::unsynced;
            break;
        case TimeSyncState::syncing:
            time_sync = pf_runtime::TimeSyncState::syncing;
            break;
        case TimeSyncState::synced:
            time_sync = pf_runtime::TimeSyncState::synced;
            break;
    }
    runtime_->update_time_sync(time_sync);
}

void NetworkService::maybe_start_sntp()
{
    if (sntp_started_) {
        return;
    }

    std::strncpy(
        sntp_server_buffer_,
        "pool.ntp.org",
        sizeof(sntp_server_buffer_) - 1U);
    sntp_server_buffer_[sizeof(sntp_server_buffer_) - 1U] = '\0';

    const pf_config::WeatherSettingsLoadResult settings_result =
        pf_config::load_weather_settings();
    if (settings_result.error == ESP_OK &&
        settings_result.settings.ntp_server[0] != '\0') {
        std::strncpy(
            sntp_server_buffer_,
            settings_result.settings.ntp_server,
            sizeof(sntp_server_buffer_) - 1U);
        sntp_server_buffer_[sizeof(sntp_server_buffer_) - 1U] = '\0';
    }

    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(sntp_server_buffer_);
    const esp_err_t init_result = esp_netif_sntp_init(&config);
    if (init_result != ESP_OK) {
        ESP_LOGW(
            kTag,
            "sntp_init_failed=%s; will retry on next sta_got_ip",
            esp_err_to_name(init_result));
        return;
    }
    sntp_started_ = true;
    sntp_set_time_sync_notification_cb(
        &NetworkService::sntp_synced_callback);
}

void NetworkService::sntp_synced_callback(struct timeval*)
{
    if (!network_service().enqueue_event(NetworkEvent::sntp_time_synced)) {
        ESP_LOGW(
            kTag,
            "sntp_synced_event_queue_full; time_sync snapshot may lag "
            "until the next network event");
    }
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
    std::uint8_t password_entropy[kAccessPointEntropyBytes]{};
    bootloader_random_enable();
    esp_fill_random(
        password_entropy,
        sizeof(password_entropy));
    bootloader_random_disable();
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
    const bool password_ready =
        format_access_point_password(
            password_entropy,
            sizeof(password_entropy),
            access_point_.password,
            sizeof(access_point_.password));
    pf_config::secure_zero(
        password_entropy,
        sizeof(password_entropy));
    const bool access_point_info_ready =
        suffix_written == 4 &&
        ssid_written > 0 &&
        static_cast<std::size_t>(ssid_written) <
            sizeof(access_point_.ssid) &&
        password_ready;
    return access_point_info_ready;
}

NetworkService& network_service()
{
    static NetworkService instance;
    return instance;
}

}  // namespace pf_network
