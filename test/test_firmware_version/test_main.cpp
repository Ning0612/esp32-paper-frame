#include <unity.h>

#include "pf_runtime/firmware_version.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_runtime::compare_semver;
using pf_runtime::VersionCompareResult;

void test_equal_versions_compare_equal()
{
    const VersionCompareResult result = compare_semver("v1.2.3", "v1.2.3");
    TEST_ASSERT_TRUE(result.comparable);
    TEST_ASSERT_EQUAL_INT(0, result.order);
}

void test_missing_v_prefix_is_tolerated()
{
    const VersionCompareResult result = compare_semver("v1.2.3", "1.2.3");
    TEST_ASSERT_TRUE(result.comparable);
    TEST_ASSERT_EQUAL_INT(0, result.order);
}

void test_newer_major_minor_patch_each_detected()
{
    TEST_ASSERT_EQUAL_INT(1, compare_semver("v1.2.3", "v2.0.0").order);
    TEST_ASSERT_EQUAL_INT(1, compare_semver("v1.2.3", "v1.3.0").order);
    TEST_ASSERT_EQUAL_INT(1, compare_semver("v1.2.3", "v1.2.4").order);
}

void test_older_candidate_detected()
{
    const VersionCompareResult result = compare_semver("v1.2.3", "v1.2.2");
    TEST_ASSERT_TRUE(result.comparable);
    TEST_ASSERT_EQUAL_INT(-1, result.order);
}

void test_prerelease_has_lower_precedence_than_same_numbered_release()
{
    // SemVer 2.0.0 §11.4: v1.2.3-rc1 < v1.2.3. A same-numbered pre-release
    // must never be reported as an update over the plain release it
    // precedes.
    const VersionCompareResult older_candidate =
        compare_semver("v1.2.3", "v1.2.3-rc1");
    TEST_ASSERT_TRUE(older_candidate.comparable);
    TEST_ASSERT_EQUAL_INT(-1, older_candidate.order);

    // The reverse: a plain release is a real update over the pre-release
    // it followed.
    const VersionCompareResult newer_candidate =
        compare_semver("v1.2.3-rc1", "v1.2.3");
    TEST_ASSERT_TRUE(newer_candidate.comparable);
    TEST_ASSERT_EQUAL_INT(1, newer_candidate.order);
}

void test_two_prereleases_with_same_numbers_compare_equal()
{
    // Simplification documented in ParsedVersion::has_prerelease: this
    // project doesn't compare pre-release identifiers field-by-field, only
    // whether one is present.
    const VersionCompareResult result =
        compare_semver("v1.2.3-rc1", "v1.2.3-rc2");
    TEST_ASSERT_TRUE(result.comparable);
    TEST_ASSERT_EQUAL_INT(0, result.order);
}

void test_build_metadata_suffix_is_ignored_for_comparison()
{
    const VersionCompareResult result =
        compare_semver("v1.2.3", "v1.2.4+build.5");
    TEST_ASSERT_TRUE(result.comparable);
    TEST_ASSERT_EQUAL_INT(1, result.order);
}

void test_malformed_candidate_is_not_comparable()
{
    // An unparsable tag must never silently look like "no update available"
    // (order == 0 with comparable == false is meaningless and callers must
    // check comparable first) -- surface it as check_failed instead.
    TEST_ASSERT_FALSE(compare_semver("v1.2.3", "not-a-version").comparable);
    TEST_ASSERT_FALSE(compare_semver("v1.2.3", "v1.2").comparable);
    TEST_ASSERT_FALSE(compare_semver("v1.2.3", "v1.2.3abc").comparable);
    TEST_ASSERT_FALSE(compare_semver("v1.2.3", "").comparable);
    TEST_ASSERT_FALSE(compare_semver("v1.2.3", nullptr).comparable);
}

void test_malformed_running_is_not_comparable()
{
    TEST_ASSERT_FALSE(compare_semver("phase3-dev", "v1.2.3").comparable);
}

void test_overflowing_numeric_component_is_not_comparable()
{
    // A component too large for unsigned int must fail the whole parse,
    // not silently wrap into a small, wrong version number.
    TEST_ASSERT_FALSE(
        compare_semver("v1.2.3", "v99999999999.0.0").comparable);
    TEST_ASSERT_FALSE(
        compare_semver("v1.2.3", "v1.99999999999.0").comparable);
    TEST_ASSERT_FALSE(
        compare_semver("v1.2.3", "v1.2.99999999999").comparable);
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_equal_versions_compare_equal);
    RUN_TEST(test_missing_v_prefix_is_tolerated);
    RUN_TEST(test_newer_major_minor_patch_each_detected);
    RUN_TEST(test_older_candidate_detected);
    RUN_TEST(test_prerelease_has_lower_precedence_than_same_numbered_release);
    RUN_TEST(test_two_prereleases_with_same_numbers_compare_equal);
    RUN_TEST(test_build_metadata_suffix_is_ignored_for_comparison);
    RUN_TEST(test_malformed_candidate_is_not_comparable);
    RUN_TEST(test_malformed_running_is_not_comparable);
    RUN_TEST(test_overflowing_numeric_component_is_not_comparable);
    return UNITY_END();
}
