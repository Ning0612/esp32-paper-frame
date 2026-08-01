#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_ota {

// Checks GitHub Releases for a newer firmware build and, when explicitly
// told to, downloads and flashes it into the inactive OTA (ota_0/ota_1)
// partition via esp_https_ota. Admin-triggered only -- there is no
// background polling; the WebUI's system page calls
// request_check_for_update()/request_update_now(). webfs/imagefs are never
// touched by this worker. See
// docs/adr/0008-ota-github-releases-and-rollback.md.
class OtaWorker {
public:
    esp_err_t start(pf_runtime::RuntimeCoordinator& runtime);

    // Returns false if a check or update is already in flight; callers
    // must surface that as a rejection (e.g. HTTP 409/503), never silently
    // drop or queue a second request behind the first.
    bool request_check_for_update();
    bool request_update_now();

private:
    enum class Command : std::uint8_t {
        none,
        check_for_update,
        update_now,
    };

    static constexpr std::uint32_t kTaskStackWords = 24576U;
    static constexpr UBaseType_t kTaskPriority = 3U;
    static constexpr std::uint32_t kGithubApiTimeoutMs = 10000U;
    static constexpr std::size_t kGithubResponseBufferCapacity = 8192U;
    static constexpr std::size_t kUrlCapacity = 192U;
    static constexpr std::uint64_t kUpdateOverallDeadlineMs =
        5U * 60U * 1000U;

    static void task_entry(void* context);
    static esp_err_t github_api_event_handler(
        esp_http_client_event_t* event);

    void task_main();
    void check_for_update();
    void update_now();
    static std::uint64_t now_ms_since_boot();

    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    StaticTask_t task_control_{};
    StackType_t task_stack_[kTaskStackWords]{};
    SemaphoreHandle_t wake_semaphore_ = nullptr;
    StaticSemaphore_t wake_semaphore_control_{};
    std::atomic<Command> pending_command_{Command::none};
    // Authoritative "request already in flight" guard: task_main only ever
    // processes one command at a time, and this compare-and-swap is what
    // actually rejects an overlapping second request (rather than a
    // separately-read, potentially-stale RuntimeSnapshot field).
    std::atomic<bool> busy_{false};

    // Deliberately internal DRAM, not PSRAM: a codex-cowork review of the
    // 2026-08-01 AP-start crash fix flagged that this buffer's task also
    // performs esp_https_ota flash writes elsewhere (update_now()), and
    // PSRAM access safety around flash cache-disable windows on ESP32-S3
    // is exactly the kind of subtle, hard-to-verify-without-hardware
    // territory this project has been burned by before (repeated stack-
    // overflow incidents from under-reasoned memory placement). Even
    // though this specific buffer is only touched from check_for_update(),
    // never concurrently with update_now()'s flash writes, that safety
    // argument was judged not worth the risk for a fix that doesn't
    // require it -- RAM pressure should be addressed by measurement-driven
    // reductions elsewhere, not a PSRAM move nobody has verified on real
    // hardware. See docs/hardware/VALIDATION.md 2026-08-01 for the
    // decision record.
    char github_response_buffer_[kGithubResponseBufferCapacity]{};
    std::size_t github_response_length_ = 0U;
    bool github_response_truncated_ = false;
    char url_buffer_[kUrlCapacity]{};
};

OtaWorker& ota_worker();

}  // namespace pf_ota
