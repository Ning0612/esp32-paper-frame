#include <unity.h>

#include "pf_web/access_policy.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_first_provisioning_allows_scan_and_config_without_login()
{
    const pf_web::AccessContext context{
        .provisioning_ap = true,
        .initial_bootstrap = true,
        .password_bootstrap = true,
        .management_password_configured = false,
        .authenticated = false,
        .csrf_valid = false,
    };

    TEST_ASSERT_TRUE(pf_web::wifi_scan_allowed(context));
    TEST_ASSERT_TRUE(pf_web::wifi_config_allowed(context));
    TEST_ASSERT_TRUE(pf_web::password_setup_allowed(context));
}

void test_recovery_ap_requires_login_and_csrf_for_config()
{
    pf_web::AccessContext context{
        .provisioning_ap = true,
        .initial_bootstrap = false,
        .password_bootstrap = false,
        .management_password_configured = true,
        .authenticated = false,
        .csrf_valid = false,
    };

    TEST_ASSERT_FALSE(pf_web::wifi_scan_allowed(context));
    TEST_ASSERT_FALSE(pf_web::wifi_config_allowed(context));
    TEST_ASSERT_FALSE(pf_web::password_setup_allowed(context));

    context.authenticated = true;
    TEST_ASSERT_TRUE(pf_web::wifi_scan_allowed(context));
    TEST_ASSERT_FALSE(pf_web::wifi_config_allowed(context));

    context.csrf_valid = true;
    TEST_ASSERT_TRUE(pf_web::wifi_config_allowed(context));
}

void test_saved_wifi_without_password_only_allows_local_password_bootstrap()
{
    const pf_web::AccessContext context{
        .provisioning_ap = true,
        .initial_bootstrap = false,
        .password_bootstrap = true,
        .management_password_configured = false,
        .authenticated = false,
        .csrf_valid = false,
    };

    TEST_ASSERT_FALSE(pf_web::wifi_scan_allowed(context));
    TEST_ASSERT_FALSE(pf_web::wifi_config_allowed(context));
    TEST_ASSERT_TRUE(pf_web::password_setup_allowed(context));
}

void test_normal_mode_never_gets_the_bootstrap_exception()
{
    const pf_web::AccessContext context{
        .provisioning_ap = false,
        .initial_bootstrap = true,
        .password_bootstrap = true,
        .management_password_configured = false,
        .authenticated = false,
        .csrf_valid = false,
    };

    TEST_ASSERT_FALSE(pf_web::wifi_scan_allowed(context));
    TEST_ASSERT_FALSE(pf_web::wifi_config_allowed(context));
    TEST_ASSERT_FALSE(pf_web::password_setup_allowed(context));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_provisioning_allows_scan_and_config_without_login);
    RUN_TEST(test_recovery_ap_requires_login_and_csrf_for_config);
    RUN_TEST(
        test_saved_wifi_without_password_only_allows_local_password_bootstrap);
    RUN_TEST(test_normal_mode_never_gets_the_bootstrap_exception);
    return UNITY_END();
}
