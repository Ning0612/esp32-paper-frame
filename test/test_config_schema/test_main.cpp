#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pf_config/schema.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_blank_storage_initializes_defaults()
{
    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(pf_config::StoredConfig{});

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::initialize_defaults),
        static_cast<int>(plan.action));
    TEST_ASSERT_TRUE(plan.write_required);
    TEST_ASSERT_EQUAL_UINT32(
        pf_config::kDefaultRefreshMinutes,
        plan.record.refresh_minutes);
    TEST_ASSERT_EQUAL_STRING(
        pf_config::kDefaultTimezone,
        plan.record.timezone);
    TEST_ASSERT_FALSE(plan.record.carousel_random);
}

void test_current_schema_is_used_without_migration()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = pf_config::kCurrentSchemaVersion;
    stored.refresh_present = true;
    stored.refresh_minutes = 45;
    stored.timezone_present = true;
    pf_config::copy_timezone(stored.timezone, "+9");
    stored.carousel_random_present = true;
    stored.carousel_random = 1U;

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::use_current),
        static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.write_required);
    TEST_ASSERT_EQUAL_UINT32(45, plan.record.refresh_minutes);
    TEST_ASSERT_EQUAL_STRING("+9", plan.record.timezone);
    TEST_ASSERT_TRUE(plan.record.carousel_random);
}

// Schema v3 changed what this field means (IANA zone name -> UTC offset
// string); a record claiming to already be current must have offset text
// that actually parses, not a leftover pre-v3 zone name.
void test_current_schema_with_non_offset_timezone_is_rejected()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = pf_config::kCurrentSchemaVersion;
    stored.refresh_present = true;
    stored.refresh_minutes = 45;
    stored.timezone_present = true;
    pf_config::copy_timezone(stored.timezone, "Asia/Tokyo");
    stored.carousel_random_present = true;
    stored.carousel_random = 1U;

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::reject_corrupt),
        static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.write_required);
}

void test_v0_migration_preserves_refresh_and_adds_timezone()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = 0;
    stored.refresh_present = true;
    stored.refresh_minutes = 60;

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::migrate_v0),
        static_cast<int>(plan.action));
    TEST_ASSERT_TRUE(plan.write_required);
    TEST_ASSERT_EQUAL_UINT32(60, plan.record.refresh_minutes);
    TEST_ASSERT_EQUAL_STRING(
        pf_config::kDefaultTimezone,
        plan.record.timezone);
    TEST_ASSERT_FALSE(plan.record.carousel_random);
}

void test_v1_migration_preserves_existing_fields_and_defaults_random_off()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = 1;
    stored.refresh_present = true;
    stored.refresh_minutes = 35;
    stored.timezone_present = true;
    pf_config::copy_timezone(stored.timezone, "Asia/Taipei");

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::migrate_v1),
        static_cast<int>(plan.action));
    TEST_ASSERT_TRUE(plan.write_required);
    TEST_ASSERT_EQUAL_UINT32(35, plan.record.refresh_minutes);
    TEST_ASSERT_FALSE(plan.record.carousel_random);
    // Pre-v3 timezone text ("Asia/Taipei") is not a valid offset under the
    // new format; migration must normalize it to the new default rather
    // than carry forward text that would now fail validation.
    TEST_ASSERT_EQUAL_STRING(
        pf_config::kDefaultTimezone,
        plan.record.timezone);
}

// The v1 fixture above has no carousel_random at all, so defaulting it off is
// correct there. This one is the shape the next schema bump will produce: a
// stored record that *does* carry the field, migrating to a newer version.
// make_startup_plan must carry such fields across, because the migration path
// sets write_required and the returned record is what gets written back to
// NVS -- anything it drops is lost permanently, not just for this boot.
void test_migration_preserves_carousel_random_when_present()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = 1;
    stored.refresh_present = true;
    stored.refresh_minutes = 35;
    stored.timezone_present = true;
    pf_config::copy_timezone(stored.timezone, "Asia/Taipei");
    stored.carousel_random_present = true;
    stored.carousel_random = 1U;

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::migrate_v1),
        static_cast<int>(plan.action));
    TEST_ASSERT_TRUE(plan.write_required);
    TEST_ASSERT_EQUAL_UINT32(35, plan.record.refresh_minutes);
    TEST_ASSERT_TRUE(plan.record.carousel_random);
    TEST_ASSERT_EQUAL_STRING(
        pf_config::kDefaultTimezone,
        plan.record.timezone);
}

// Writing back a record this firmware could not interpret would stamp its own
// kCurrentSchemaVersion onto data written by a newer build. Only the actions
// that mean "the schema was understood" may write.
void test_only_understood_schemas_may_be_written_back()
{
    TEST_ASSERT_TRUE(
        pf_config::schema_allows_write(pf_config::SchemaAction::use_current));
    TEST_ASSERT_TRUE(
        pf_config::schema_allows_write(pf_config::SchemaAction::migrate_v0));
    TEST_ASSERT_TRUE(
        pf_config::schema_allows_write(pf_config::SchemaAction::migrate_v1));
    TEST_ASSERT_TRUE(pf_config::schema_allows_write(
        pf_config::SchemaAction::initialize_defaults));

    // A rejected plan drops the fields it did parse, so the runtime carries
    // placeholders; writing that back would overwrite valid neighbouring
    // settings while "repairing" the corrupt one.
    TEST_ASSERT_FALSE(
        pf_config::schema_allows_write(pf_config::SchemaAction::reject_corrupt));

    // A future record is intact and meaningful to newer firmware; stamping
    // this build's version over it would destroy that.
    TEST_ASSERT_FALSE(
        pf_config::schema_allows_write(pf_config::SchemaAction::reject_future));
    TEST_ASSERT_FALSE(
        pf_config::schema_allows_write(pf_config::SchemaAction::unavailable));
}

void test_future_schema_is_rejected_without_write()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = pf_config::kCurrentSchemaVersion + 1;

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::reject_future),
        static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.write_required);
}

void test_incomplete_current_record_is_rejected_without_write()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = pf_config::kCurrentSchemaVersion;
    stored.refresh_present = true;
    stored.refresh_minutes = 30;
    stored.timezone_present = true;
    pf_config::copy_timezone(stored.timezone, "+8");

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::reject_corrupt),
        static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.write_required);
}

void test_invalid_v0_record_is_rejected_without_write()
{
    pf_config::StoredConfig stored{};
    stored.schema_present = true;
    stored.schema_version = 0;
    stored.refresh_present = true;
    stored.refresh_minutes = pf_config::kMinimumRefreshMinutes - 1;

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::reject_corrupt),
        static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.write_required);
}

void test_refresh_interval_bounds_are_inclusive()
{
    TEST_ASSERT_TRUE(pf_config::refresh_minutes_valid(
        pf_config::kMinimumRefreshMinutes));
    TEST_ASSERT_TRUE(pf_config::refresh_minutes_valid(
        pf_config::kMaximumRefreshMinutes));
    TEST_ASSERT_FALSE(pf_config::refresh_minutes_valid(
        pf_config::kMinimumRefreshMinutes - 1U));
    TEST_ASSERT_FALSE(pf_config::refresh_minutes_valid(
        pf_config::kMaximumRefreshMinutes + 1U));
}

void test_timezone_offset_parses_whole_and_quarter_hour_values()
{
    std::int32_t minutes = 0;

    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("+8", minutes));
    TEST_ASSERT_EQUAL_INT32(480, minutes);

    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("8", minutes));
    TEST_ASSERT_EQUAL_INT32(480, minutes);

    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("-5.5", minutes));
    TEST_ASSERT_EQUAL_INT32(-330, minutes);

    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("+5.75", minutes));
    TEST_ASSERT_EQUAL_INT32(345, minutes);

    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("+5.25", minutes));
    TEST_ASSERT_EQUAL_INT32(315, minutes);

    // Boundaries: UTC-12 (Baker Island) to UTC+14 (Kiribati).
    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("-12", minutes));
    TEST_ASSERT_EQUAL_INT32(-720, minutes);
    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("+14", minutes));
    TEST_ASSERT_EQUAL_INT32(840, minutes);
    TEST_ASSERT_TRUE(pf_config::parse_timezone_offset_minutes("0", minutes));
    TEST_ASSERT_EQUAL_INT32(0, minutes);
}

void test_timezone_offset_rejects_out_of_range_and_malformed_text()
{
    std::int32_t minutes = 0;

    // Past the real-world +14/-12 bounds.
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("+15", minutes));
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("-12.25", minutes));

    // Finer than quarter-hour has no real timezone behind it.
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("+5.1", minutes));
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("+5.10", minutes));

    // Malformed text.
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("", minutes));
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("abc", minutes));
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("++8", minutes));
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("8.", minutes));
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("8.7.5", minutes));
    TEST_ASSERT_FALSE(pf_config::parse_timezone_offset_minutes("Asia/Taipei", minutes));

    TEST_ASSERT_FALSE(pf_config::valid_timezone_offset_text("Asia/Taipei"));
    TEST_ASSERT_TRUE(pf_config::valid_timezone_offset_text(
        pf_config::kDefaultTimezone));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_storage_initializes_defaults);
    RUN_TEST(test_current_schema_is_used_without_migration);
    RUN_TEST(test_current_schema_with_non_offset_timezone_is_rejected);
    RUN_TEST(test_v0_migration_preserves_refresh_and_adds_timezone);
    RUN_TEST(test_v1_migration_preserves_existing_fields_and_defaults_random_off);
    RUN_TEST(test_migration_preserves_carousel_random_when_present);
    RUN_TEST(test_only_understood_schemas_may_be_written_back);
    RUN_TEST(test_future_schema_is_rejected_without_write);
    RUN_TEST(test_incomplete_current_record_is_rejected_without_write);
    RUN_TEST(test_invalid_v0_record_is_rejected_without_write);
    RUN_TEST(test_refresh_interval_bounds_are_inclusive);
    RUN_TEST(test_timezone_offset_parses_whole_and_quarter_hour_values);
    RUN_TEST(test_timezone_offset_rejects_out_of_range_and_malformed_text);
    return UNITY_END();
}
