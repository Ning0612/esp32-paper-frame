#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_config {

inline constexpr std::uint32_t kCurrentSchemaVersion = 3;
inline constexpr std::uint32_t kDefaultRefreshMinutes = 30;
inline constexpr std::uint32_t kMinimumRefreshMinutes = 10;
inline constexpr std::uint32_t kMaximumRefreshMinutes = 24U * 60U;
inline constexpr std::size_t kTimezoneCapacity = 48;
// Schema v3: this field changed meaning from an IANA zone name (e.g.
// "Asia/Taipei") to a plain UTC offset string ("+8", "-5.5", "+5.75") that
// app_main actually applies to the displayed clock -- versions before v3
// stored a name here but nothing ever read it back for time conversion
// (see docs/adr/0005-weather-worker-and-status-bar.md). "+8" is the same
// real-world offset the old default name represented.
inline constexpr char kDefaultTimezone[] = "+8";
// UTC-12 (Baker Island) to UTC+14 (Kiribati's Line Islands): the real-world
// range of standard time offsets in use today.
inline constexpr std::int32_t kMinimumTimezoneOffsetMinutes = -720;
inline constexpr std::int32_t kMaximumTimezoneOffsetMinutes = 840;

enum class SchemaAction {
    initialize_defaults,
    use_current,
    migrate_v0,
    migrate_v1,
    reject_future,
    reject_corrupt,
    unavailable,
};

struct ConfigRecord {
    std::uint32_t refresh_minutes = 0;
    char timezone[kTimezoneCapacity]{};
    bool carousel_random = false;
};

struct StoredConfig {
    bool schema_present = false;
    std::uint32_t schema_version = 0;
    bool refresh_present = false;
    std::uint32_t refresh_minutes = 0;
    bool timezone_present = false;
    char timezone[kTimezoneCapacity]{};
    bool carousel_random_present = false;
    std::uint8_t carousel_random = 0U;
};

struct StartupPlan {
    SchemaAction action = SchemaAction::reject_corrupt;
    bool write_required = false;
    ConfigRecord record{};
};

constexpr bool refresh_minutes_valid(const std::uint32_t value)
{
    return value >= kMinimumRefreshMinutes &&
           value <= kMaximumRefreshMinutes;
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

// Parses a UTC offset string ("+8", "-5.5", "+5.75", "8") into whole
// minutes. The fractional part (if present) must resolve to a quarter-hour
// step (.00/.25/.50/.75) since every real-world half- and 45-minute-offset
// zone (e.g. Chatham Islands +12:45, Nepal +5:45, India +5:30) lands on one;
// anything finer has no real timezone behind it and is rejected rather than
// silently truncated.
inline bool parse_timezone_offset_minutes(
    const char* const text,
    std::int32_t& minutes_out)
{
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    std::size_t index = 0;
    bool negative = false;
    if (text[0] == '+' || text[0] == '-') {
        negative = text[0] == '-';
        index = 1;
    }

    if (text[index] < '0' || text[index] > '9') {
        return false;
    }
    std::uint32_t hours = 0;
    std::size_t hour_digits = 0;
    while (text[index] >= '0' && text[index] <= '9') {
        if (hour_digits >= 2U) {
            return false;
        }
        hours = hours * 10U + static_cast<std::uint32_t>(text[index] - '0');
        ++index;
        ++hour_digits;
    }

    std::uint32_t frac_hundredths = 0;
    if (text[index] == '.') {
        ++index;
        std::size_t frac_digits = 0;
        while (text[index] >= '0' && text[index] <= '9') {
            if (frac_digits >= 2U) {
                return false;
            }
            frac_hundredths = frac_hundredths * 10U +
                               static_cast<std::uint32_t>(text[index] - '0');
            ++index;
            ++frac_digits;
        }
        if (frac_digits == 0U) {
            return false;
        }
        if (frac_digits == 1U) {
            frac_hundredths *= 10U;
        }
    }

    if (text[index] != '\0' || frac_hundredths % 25U != 0U) {
        return false;
    }

    const std::uint32_t frac_minutes = (frac_hundredths * 60U) / 100U;
    const std::uint32_t total_minutes = hours * 60U + frac_minutes;
    const std::uint32_t limit = negative
                                     ? static_cast<std::uint32_t>(
                                           -kMinimumTimezoneOffsetMinutes)
                                     : static_cast<std::uint32_t>(
                                           kMaximumTimezoneOffsetMinutes);
    if (total_minutes > limit) {
        return false;
    }

    minutes_out = negative ? -static_cast<std::int32_t>(total_minutes)
                            : static_cast<std::int32_t>(total_minutes);
    return true;
}

inline bool valid_timezone_offset_text(const char* const text)
{
    std::int32_t minutes = 0;
    return parse_timezone_offset_minutes(text, minutes);
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

    plan.record.refresh_minutes = stored.refresh_minutes;

    // Carry over every field the stored record already has *before* the
    // migration early-returns. That path sets write_required, so the record
    // returned here is what gets written back to NVS -- anything left at its
    // default is lost permanently, not just for this boot. A record from a
    // version that predates a field simply has *_present false and keeps the
    // default, which is the intended behaviour; an out-of-range value is
    // treated the same way rather than rejected, because failing a migration
    // closed would discard the entire configuration over one bad byte.
    if (stored.carousel_random_present && stored.carousel_random <= 1U) {
        plan.record.carousel_random = stored.carousel_random != 0U;
    }

    if (stored.schema_version < kCurrentSchemaVersion) {
        // Schema v3 changed what this field means (IANA zone name -> UTC
        // offset string); a pre-v3 record's timezone text was never a valid
        // offset under the new format (and nothing ever read it back
        // anyway, see kDefaultTimezone above), so migration resets it to
        // the new default rather than carrying forward text that would now
        // fail validation or silently mean something else.
        copy_timezone(plan.record.timezone, kDefaultTimezone);
        plan.action = SchemaAction::migrate_v1;
        plan.write_required = true;
        return plan;
    }

    // The current schema is stricter than a migration: a record claiming to be
    // current must have every field present and valid.
    if (!stored.carousel_random_present || stored.carousel_random > 1U ||
        !valid_timezone_offset_text(plan.record.timezone)) {
        plan.action = SchemaAction::reject_corrupt;
        return plan;
    }

    plan.action = SchemaAction::use_current;
    return plan;
}

// Whether this firmware may write the central record back. A schema it could
// not interpret must be left alone: overwriting it would stamp this build's
// kCurrentSchemaVersion onto a record written by newer firmware, silently
// downgrading it. Refusing to write costs the user one unsaved setting change;
// writing costs the newer firmware its configuration.
constexpr bool schema_allows_write(const SchemaAction action)
{
    // Only a record this firmware actually parsed may be written back.
    //
    // reject_corrupt is excluded even though that blocks the obvious repair
    // route, because the save path cannot repair anything: a rejected plan
    // reports record_available=false, so the fields it *did* parse are
    // dropped and the runtime carries placeholders instead ("unknown" for
    // timezone). Saving then writes those placeholders over settings that
    // were perfectly valid -- fixing one corrupt field by destroying its
    // neighbours, invisibly, with no UI to put them back.
    //
    // Repairing genuinely corrupt data needs a deliberate reset that starts
    // from defaults rather than from a half-populated runtime; until such a
    // path exists the central record stays read-only, which loses a setting
    // change rather than a setting.
    //
    // reject_future is excluded for the opposite reason: that record parsed
    // fine for newer firmware and stamping this build's version over it would
    // downgrade it.
    return action == SchemaAction::use_current ||
           action == SchemaAction::migrate_v0 ||
           action == SchemaAction::migrate_v1 ||
           action == SchemaAction::initialize_defaults;
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
        case SchemaAction::migrate_v1:
            return "migrate_v1";
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
