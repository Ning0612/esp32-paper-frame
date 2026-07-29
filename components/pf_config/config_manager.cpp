#include "pf_config/config_manager.hpp"

#include <cstdint>

#include "nvs.h"
#include "nvs_flash.h"

namespace pf_config {
namespace {

constexpr char kNamespace[] = "pf_config";
constexpr char kSchemaKey[] = "schema_ver";
constexpr char kRefreshMinutesKey[] = "refresh_min";
constexpr char kTimezoneKey[] = "timezone";

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
        return {SchemaAction::unavailable, result};
    }

    nvs_handle_t handle = 0;
    result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return {SchemaAction::unavailable, result};
    }

    StoredConfig stored{};
    const esp_err_t version_result =
        nvs_get_u32(handle, kSchemaKey, &stored.schema_version);
    stored.schema_present = version_result == ESP_OK;
    if (!stored.schema_present &&
        version_result != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return {SchemaAction::reject_corrupt, version_result};
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
            return {SchemaAction::unavailable, result};
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
    return {plan.action, result};
}

}  // namespace pf_config
