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
    pf_config::copy_timezone(stored.timezone, "Asia/Tokyo");
    stored.carousel_random_present = true;
    stored.carousel_random = 1U;

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::use_current),
        static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.write_required);
    TEST_ASSERT_EQUAL_UINT32(45, plan.record.refresh_minutes);
    TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", plan.record.timezone);
    TEST_ASSERT_TRUE(plan.record.carousel_random);
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
    pf_config::copy_timezone(stored.timezone, "Asia/Taipei");

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

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_storage_initializes_defaults);
    RUN_TEST(test_current_schema_is_used_without_migration);
    RUN_TEST(test_v0_migration_preserves_refresh_and_adds_timezone);
    RUN_TEST(test_v1_migration_preserves_existing_fields_and_defaults_random_off);
    RUN_TEST(test_migration_preserves_carousel_random_when_present);
    RUN_TEST(test_only_understood_schemas_may_be_written_back);
    RUN_TEST(test_future_schema_is_rejected_without_write);
    RUN_TEST(test_incomplete_current_record_is_rejected_without_write);
    RUN_TEST(test_invalid_v0_record_is_rejected_without_write);
    RUN_TEST(test_refresh_interval_bounds_are_inclusive);
    return UNITY_END();
}
