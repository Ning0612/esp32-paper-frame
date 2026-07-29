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

    const pf_config::StartupPlan plan =
        pf_config::make_startup_plan(stored);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_config::SchemaAction::use_current),
        static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.write_required);
    TEST_ASSERT_EQUAL_UINT32(45, plan.record.refresh_minutes);
    TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", plan.record.timezone);
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

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_storage_initializes_defaults);
    RUN_TEST(test_current_schema_is_used_without_migration);
    RUN_TEST(test_v0_migration_preserves_refresh_and_adds_timezone);
    RUN_TEST(test_future_schema_is_rejected_without_write);
    RUN_TEST(test_incomplete_current_record_is_rejected_without_write);
    RUN_TEST(test_invalid_v0_record_is_rejected_without_write);
    return UNITY_END();
}
