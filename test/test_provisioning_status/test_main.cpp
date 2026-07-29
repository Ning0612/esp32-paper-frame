#include <cstring>

#include <unity.h>

#include "pf_web/provisioning_status.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_status_is_visible_only_to_the_matching_request()
{
    const pf_web::ProvisioningOperationStatus status{
        .request_id = 42U,
        .state = pf_web::ProvisioningOperationState::saving,
    };

    TEST_ASSERT_TRUE(
        pf_web::provisioning_status_matches(status, 42U));
    TEST_ASSERT_FALSE(
        pf_web::provisioning_status_matches(status, 41U));
    TEST_ASSERT_FALSE(
        pf_web::provisioning_status_matches(status, 0U));
}

void test_status_serializer_reports_truthful_lifecycle()
{
    char output[128]{};
    const pf_web::ProvisioningOperationStatus saving{
        .request_id = 7U,
        .state = pf_web::ProvisioningOperationState::saving,
    };
    TEST_ASSERT_TRUE(
        pf_web::serialize_provisioning_status(
            saving,
            output,
            sizeof(output)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"data\":{\"request_id\":7,\"state\":\"saving\","
        "\"rebooting\":false}}",
        output);

    const pf_web::ProvisioningOperationStatus committed{
        .request_id = 7U,
        .state = pf_web::ProvisioningOperationState::committed,
    };
    TEST_ASSERT_TRUE(
        pf_web::serialize_provisioning_status(
            committed,
            output,
            sizeof(output)));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"state\":\"committed\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"rebooting\":false"));

    const pf_web::ProvisioningOperationStatus reboot_pending{
        .request_id = 7U,
        .state = pf_web::ProvisioningOperationState::reboot_pending,
    };
    TEST_ASSERT_TRUE(
        pf_web::serialize_provisioning_status(
            reboot_pending,
            output,
            sizeof(output)));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"state\":\"reboot_pending\""));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"rebooting\":true"));

    const pf_web::ProvisioningOperationStatus failed{
        .request_id = 7U,
        .state = pf_web::ProvisioningOperationState::failed,
    };
    TEST_ASSERT_TRUE(
        pf_web::serialize_provisioning_status(
            failed,
            output,
            sizeof(output)));
    TEST_ASSERT_NOT_NULL(std::strstr(output, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(output, "\"error\":\"credential_commit_failed\""));
}

void test_status_serializer_rejects_idle_and_small_buffers()
{
    char output[24]{};
    const pf_web::ProvisioningOperationStatus idle{};
    TEST_ASSERT_FALSE(
        pf_web::serialize_provisioning_status(
            idle,
            output,
            sizeof(output)));

    const pf_web::ProvisioningOperationStatus saving{
        .request_id = 1U,
        .state = pf_web::ProvisioningOperationState::saving,
    };
    TEST_ASSERT_FALSE(
        pf_web::serialize_provisioning_status(
            saving,
            output,
            sizeof(output)));
}

void test_terminal_failure_blocks_resubmit_until_acknowledged()
{
    TEST_ASSERT_FALSE(
        pf_web::provisioning_operation_blocks_submission(
            pf_web::ProvisioningOperationState::idle));
    TEST_ASSERT_TRUE(
        pf_web::provisioning_operation_blocks_submission(
            pf_web::ProvisioningOperationState::saving));
    TEST_ASSERT_TRUE(
        pf_web::provisioning_operation_blocks_submission(
            pf_web::ProvisioningOperationState::committed));
    TEST_ASSERT_TRUE(
        pf_web::provisioning_operation_blocks_submission(
            pf_web::ProvisioningOperationState::reboot_pending));
    TEST_ASSERT_TRUE(
        pf_web::provisioning_operation_blocks_submission(
            pf_web::ProvisioningOperationState::failed));
    const auto acknowledged_failure =
        pf_web::provisioning_state_after_ack(
            pf_web::ProvisioningOperationState::failed);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_web::ProvisioningOperationState::idle),
        static_cast<int>(acknowledged_failure));
    TEST_ASSERT_FALSE(
        pf_web::provisioning_operation_blocks_submission(
            acknowledged_failure));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_web::ProvisioningOperationState::reboot_pending),
        static_cast<int>(
            pf_web::provisioning_state_after_ack(
                pf_web::ProvisioningOperationState::committed)));
}

void test_failure_ack_at_timeout_boundary_cannot_wake_next_request()
{
    pf_web::ProvisioningOperationStatus status{
        .request_id = 11U,
        .state = pf_web::ProvisioningOperationState::failed,
    };
    std::uint32_t pending_notifications = 0U;

    status.state =
        pf_web::provisioning_state_after_ack(status.state);
    ++pending_notifications;

    if (pf_web::finalize_failed_provisioning_operation(
            status,
            11U)) {
        pending_notifications = 0U;
    }
    TEST_ASSERT_EQUAL_UINT32(0U, pending_notifications);
    TEST_ASSERT_EQUAL_UINT32(0U, status.request_id);

    status = {
        .request_id = 12U,
        .state = pf_web::ProvisioningOperationState::committed,
    };
    TEST_ASSERT_EQUAL_UINT32(0U, pending_notifications);
    TEST_ASSERT_TRUE(
        pf_web::provisioning_operation_blocks_submission(
            status.state));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_is_visible_only_to_the_matching_request);
    RUN_TEST(test_status_serializer_reports_truthful_lifecycle);
    RUN_TEST(test_status_serializer_rejects_idle_and_small_buffers);
    RUN_TEST(test_terminal_failure_blocks_resubmit_until_acknowledged);
    RUN_TEST(test_failure_ack_at_timeout_boundary_cannot_wake_next_request);
    return UNITY_END();
}
