#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_config {

inline constexpr std::uint32_t kCurrentSchemaVersion = 1;
inline constexpr std::uint32_t kDefaultRefreshMinutes = 30;
inline constexpr std::uint32_t kMinimumRefreshMinutes = 5;
inline constexpr std::size_t kTimezoneCapacity = 48;
inline constexpr char kDefaultTimezone[] = "Asia/Taipei";

enum class SchemaAction {
    initialize_defaults,
    use_current,
    migrate_v0,
    reject_future,
    reject_corrupt,
    unavailable,
};

struct ConfigRecord {
    std::uint32_t refresh_minutes = 0;
    char timezone[kTimezoneCapacity]{};
};

struct StoredConfig {
    bool schema_present = false;
    std::uint32_t schema_version = 0;
    bool refresh_present = false;
    std::uint32_t refresh_minutes = 0;
    bool timezone_present = false;
    char timezone[kTimezoneCapacity]{};
};

struct StartupPlan {
    SchemaAction action = SchemaAction::reject_corrupt;
    bool write_required = false;
    ConfigRecord record{};
};

constexpr bool refresh_minutes_valid(const std::uint32_t value)
{
    return value >= kMinimumRefreshMinutes;
}

inline bool copy_timezone(
    char (&destination)[kTimezoneCapacity],
    const char* source)
{
    if (source == nullptr || source[0] == '\0') {
        return false;
    }

    std::size_t index = 0;
    for (; index + 1 < kTimezoneCapacity && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    if (source[index] != '\0') {
        destination[0] = '\0';
        return false;
    }
    destination[index] = '\0';
    return true;
}

inline StartupPlan make_startup_plan(const StoredConfig& stored)
{
    StartupPlan plan{};

    if (!stored.schema_present) {
        plan.action = SchemaAction::initialize_defaults;
        plan.write_required = true;
        plan.record.refresh_minutes = kDefaultRefreshMinutes;
        copy_timezone(plan.record.timezone, kDefaultTimezone);
        return plan;
    }

    if (stored.schema_version > kCurrentSchemaVersion) {
        plan.action = SchemaAction::reject_future;
        return plan;
    }

    if (stored.schema_version == 0) {
        if (!stored.refresh_present ||
            !refresh_minutes_valid(stored.refresh_minutes)) {
            plan.action = SchemaAction::reject_corrupt;
            return plan;
        }
        plan.action = SchemaAction::migrate_v0;
        plan.write_required = true;
        plan.record.refresh_minutes = stored.refresh_minutes;
        copy_timezone(plan.record.timezone, kDefaultTimezone);
        return plan;
    }

    if (!stored.refresh_present ||
        !refresh_minutes_valid(stored.refresh_minutes) ||
        !stored.timezone_present ||
        !copy_timezone(plan.record.timezone, stored.timezone)) {
        plan.action = SchemaAction::reject_corrupt;
        return plan;
    }

    plan.action = SchemaAction::use_current;
    plan.record.refresh_minutes = stored.refresh_minutes;
    return plan;
}

constexpr const char* to_string(const SchemaAction action)
{
    switch (action) {
        case SchemaAction::initialize_defaults:
            return "initialize_defaults";
        case SchemaAction::use_current:
            return "use_current";
        case SchemaAction::migrate_v0:
            return "migrate_v0";
        case SchemaAction::reject_future:
            return "reject_future";
        case SchemaAction::reject_corrupt:
            return "reject_corrupt";
        case SchemaAction::unavailable:
            return "unavailable";
    }
    return "unknown";
}

}  // namespace pf_config
