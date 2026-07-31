#include "pf_config/config_manager.hpp"

#include <cstdint>

#include "nvs.h"
#include "nvs_flash.h"
#include "pf_config/secure_memory.hpp"

namespace pf_config {
namespace {

constexpr char kNamespace[] = "pf_config";
constexpr char kSchemaKey[] = "schema_ver";
constexpr char kRefreshMinutesKey[] = "refresh_min";
constexpr char kTimezoneKey[] = "timezone";
constexpr char kNetworkNamespace[] = "pf_wifi";
constexpr char kNetworkCredentialKey[] = "credentials";
constexpr char kAuthenticationNamespace[] = "pf_auth";
constexpr char kManagementPasswordHashKey[] = "password_hash";
constexpr char kWeatherNamespace[] = "pf_weather";
constexpr char kWeatherSettingsKey[] = "settings";
constexpr char kSensorNamespace[] = "pf_sensors";
constexpr char kSensorSettingsKey[] = "settings";

esp_err_t initialize_nvs()
{
    return nvs_flash_init();
}

esp_err_t write_v1_record(
    const nvs_handle_t handle,
    const ConfigRecord& record)
{
    esp_err_t result =
        nvs_set_u32(handle, kRefreshMinutesKey, record.refresh_minutes);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_set_str(handle, kTimezoneKey, record.timezone);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_set_u32(handle, kSchemaKey, kCurrentSchemaVersion);
    if (result != ESP_OK) {
        return result;
    }
    return nvs_commit(handle);
}

esp_err_t read_optional_u32(
    const nvs_handle_t handle,
    const char* key,
    bool& present,
    std::uint32_t& value)
{
    const esp_err_t result = nvs_get_u32(handle, key, &value);
    present = result == ESP_OK;
    if (present || result == ESP_ERR_NVS_NOT_FOUND ||
        result == ESP_ERR_NVS_TYPE_MISMATCH) {
        return ESP_OK;
    }
    return result;
}

esp_err_t read_optional_timezone(
    const nvs_handle_t handle,
    bool& present,
    char (&value)[kTimezoneCapacity])
{
    std::size_t length = kTimezoneCapacity;
    const esp_err_t result =
        nvs_get_str(handle, kTimezoneKey, value, &length);
    present = result == ESP_OK;
    if (present || result == ESP_ERR_NVS_NOT_FOUND ||
        result == ESP_ERR_NVS_TYPE_MISMATCH ||
        result == ESP_ERR_NVS_INVALID_LENGTH) {
        return ESP_OK;
    }
    return result;
}

}  // namespace

StartupResult initialize()
{
    esp_err_t result = initialize_nvs();
    if (result != ESP_OK) {
        return {SchemaAction::unavailable, result, false, {}};
    }

    nvs_handle_t handle = 0;
    result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return {SchemaAction::unavailable, result, false, {}};
    }

    StoredConfig stored{};
    const esp_err_t version_result =
        nvs_get_u32(handle, kSchemaKey, &stored.schema_version);
    stored.schema_present = version_result == ESP_OK;
    if (!stored.schema_present &&
        version_result != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return {
            SchemaAction::reject_corrupt,
            version_result,
            false,
            {},
        };
    }

    if (stored.schema_present &&
        stored.schema_version <= kCurrentSchemaVersion) {
        result = read_optional_u32(
            handle,
            kRefreshMinutesKey,
            stored.refresh_present,
            stored.refresh_minutes);
        if (result == ESP_OK && stored.schema_version == kCurrentSchemaVersion) {
            result = read_optional_timezone(
                handle,
                stored.timezone_present,
                stored.timezone);
        }
        if (result != ESP_OK) {
            nvs_close(handle);
            return {SchemaAction::unavailable, result, false, {}};
        }
    }

    const StartupPlan plan = make_startup_plan(stored);
    if (plan.write_required) {
        result = write_v1_record(handle, plan.record);
    } else if (plan.action == SchemaAction::reject_future) {
        result = ESP_ERR_NOT_SUPPORTED;
    } else if (plan.action == SchemaAction::reject_corrupt) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = ESP_OK;
    }
    nvs_close(handle);
    return {
        plan.action,
        result,
        result == ESP_OK,
        result == ESP_OK ? plan.record : ConfigRecord{},
    };
}

NetworkCredentialLoadResult load_network_credentials()
{
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kNetworkNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return {ESP_OK, false, {}};
    }
    if (result != ESP_OK) {
        return {result, false, {}};
    }

    NetworkCredentialBlob blob{};
    std::size_t length = sizeof(blob);
    result = nvs_get_blob(
        handle,
        kNetworkCredentialKey,
        &blob,
        &length);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        secure_zero(blob);
        return {ESP_OK, false, {}};
    }
    if (result != ESP_OK || length != sizeof(blob)) {
        secure_zero(blob);
        return {
            result == ESP_OK ? ESP_ERR_INVALID_SIZE : result,
            false,
            {},
        };
    }

    NetworkCredentials credentials{};
    const bool decoded =
        decode_network_credentials(blob, credentials);
    secure_zero(blob);
    if (!decoded) {
        return {ESP_ERR_INVALID_CRC, false, {}};
    }
    return {ESP_OK, true, credentials};
}

esp_err_t save_network_credentials(
    const NetworkCredentials& credentials)
{
    NetworkCredentialBlob blob{};
    if (!encode_network_credentials(credentials, blob)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kNetworkNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        secure_zero(blob);
        return result;
    }
    result = nvs_set_blob(
        handle,
        kNetworkCredentialKey,
        &blob,
        sizeof(blob));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    secure_zero(blob);
    return result;
}

ManagementPasswordStatus management_password_status()
{
    const ManagementPasswordLoadResult loaded =
        load_management_password();
    ManagementPasswordHash record = loaded.record;
    secure_zero(record);
    return {loaded.error, loaded.configured};
}

ManagementPasswordLoadResult load_management_password()
{
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kAuthenticationNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return {ESP_OK, false, {}};
    }
    if (result != ESP_OK) {
        return {result, false, {}};
    }

    ManagementPasswordHash record{};
    std::size_t length = sizeof(record);
    result = nvs_get_blob(
        handle,
        kManagementPasswordHashKey,
        &record,
        &length);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        secure_zero(record);
        return {ESP_OK, false, {}};
    }
    if (result != ESP_OK || length != sizeof(record)) {
        secure_zero(record);
        return {
            result == ESP_OK ? ESP_ERR_INVALID_SIZE : result,
            false,
            {},
        };
    }
    if (!management_password_hash_valid(record)) {
        secure_zero(record);
        return {ESP_ERR_INVALID_CRC, false, {}};
    }
    return {ESP_OK, true, record};
}

esp_err_t save_management_password(
    const ManagementPasswordHash& record)
{
    if (!management_password_hash_valid(record)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kAuthenticationNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(
        handle,
        kManagementPasswordHashKey,
        &record,
        sizeof(record));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

WeatherSettingsLoadResult load_weather_settings()
{
    WeatherSettings defaults{};
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kWeatherNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return {ESP_OK, false, defaults};
    }
    if (result != ESP_OK) {
        return {result, false, {}};
    }

    WeatherSettingsBlob blob{};
    std::size_t length = sizeof(blob);
    result = nvs_get_blob(handle, kWeatherSettingsKey, &blob, &length);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        secure_zero(blob);
        return {ESP_OK, false, defaults};
    }
    if (result != ESP_OK || length != sizeof(blob)) {
        secure_zero(blob);
        return {
            result == ESP_OK ? ESP_ERR_INVALID_SIZE : result,
            false,
            {},
        };
    }

    WeatherSettings settings{};
    const bool decoded = decode_weather_settings(blob, settings);
    secure_zero(blob);
    if (!decoded) {
        return {ESP_ERR_INVALID_CRC, false, {}};
    }
    return {ESP_OK, true, settings};
}

esp_err_t save_weather_settings(const WeatherSettings& settings)
{
    WeatherSettingsBlob blob{};
    if (!encode_weather_settings(settings, blob)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kWeatherNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        secure_zero(blob);
        return result;
    }
    result = nvs_set_blob(
        handle,
        kWeatherSettingsKey,
        &blob,
        sizeof(blob));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    secure_zero(blob);
    return result;
}

SensorSettingsLoadResult load_sensor_settings()
{
    SensorSettings defaults{};
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kSensorNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return {ESP_OK, false, defaults};
    }
    if (result != ESP_OK) {
        return {result, false, {}};
    }

    SensorSettingsBlob blob{};
    std::size_t length = sizeof(blob);
    result = nvs_get_blob(handle, kSensorSettingsKey, &blob, &length);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return {ESP_OK, false, defaults};
    }
    if (result != ESP_OK || length != sizeof(blob)) {
        return {
            result == ESP_OK ? ESP_ERR_INVALID_SIZE : result,
            false,
            {},
        };
    }

    SensorSettings settings{};
    if (!decode_sensor_settings(blob, settings)) {
        return {ESP_ERR_INVALID_CRC, false, {}};
    }
    return {ESP_OK, true, settings};
}

esp_err_t save_sensor_settings(const SensorSettings& settings)
{
    SensorSettingsBlob blob{};
    if (!encode_sensor_settings(settings, blob)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open(kSensorNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(
        handle,
        kSensorSettingsKey,
        &blob,
        sizeof(blob));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

}  // namespace pf_config
