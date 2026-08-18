#include "pf_ota/ota_worker.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pf_ota/github_release_check.hpp"
#include "pf_runtime/firmware_version.hpp"
#include "pf_runtime/runtime_coordinator.hpp"

namespace pf_ota {
namespace {

constexpr char kTag[] = "pf_ota";

// Compile-time only, not user-configurable (see
// docs/adr/0008-ota-github-releases-and-rollback.md): the repo doesn't have
// a git remote configured yet, so this is the user's best guess for when
// they publish it -- adjust here if the actual owner/repo differs.
constexpr char kGithubOwner[] = "Ning0612";
constexpr char kGithubRepo[] = "esp32-paper-frame";
// Every GitHub Release MUST attach an asset with exactly this name (see the
// release checklist) -- the fixed-name "latest/download" redirect below
// depends on it, deliberately avoiding having to parse the release's
// assets JSON array to pick one out of possibly several.
constexpr char kReleaseAssetName[] = "paperframe-firmware.bin";

}  // namespace

esp_err_t OtaWorker::start(pf_runtime::RuntimeCoordinator& runtime)
{
    if (runtime_ != nullptr || wake_semaphore_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime_ = &runtime;

    wake_semaphore_ = xSemaphoreCreateBinaryStatic(&wake_semaphore_control_);
    if (wake_semaphore_ == nullptr) {
        runtime_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    task_start_mutex_ = xSemaphoreCreateMutexStatic(
        &task_start_mutex_control_);
    if (task_start_mutex_ == nullptr) {
        wake_semaphore_ = nullptr;
        runtime_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool OtaWorker::request_check_for_update()
{
    if (!ensure_task_started()) {
        return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        return false;
    }
    pending_command_.store(
        Command::check_for_update, std::memory_order_relaxed);
    xSemaphoreGive(wake_semaphore_);
    return true;
}

bool OtaWorker::request_update_now()
{
    if (!ensure_task_started()) {
        return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        return false;
    }
    pending_command_.store(Command::update_now, std::memory_order_relaxed);
    xSemaphoreGive(wake_semaphore_);
    return true;
}

void OtaWorker::task_entry(void* const context)
{
    static_cast<OtaWorker*>(context)->task_main();
}

bool OtaWorker::ensure_task_started()
{
    if (runtime_ == nullptr ||
        wake_semaphore_ == nullptr ||
        task_start_mutex_ == nullptr ||
        xSemaphoreTake(task_start_mutex_, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    if (task_handle_ == nullptr) {
        task_stack_ = static_cast<StackType_t*>(
            heap_caps_calloc(
                kTaskStackWords,
                sizeof(StackType_t),
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (task_stack_ == nullptr) {
            ESP_LOGE(
                kTag,
                "ota_task_stack_alloc_failed bytes=%u",
                static_cast<unsigned>(
                    kTaskStackWords * sizeof(StackType_t)));
        } else {
            task_handle_ = xTaskCreateStatic(
                &OtaWorker::task_entry,
                "OtaWorkerTask",
                kTaskStackWords,
                this,
                kTaskPriority,
                task_stack_,
                &task_control_);
            if (task_handle_ == nullptr) {
                heap_caps_free(task_stack_);
                task_stack_ = nullptr;
                ESP_LOGE(kTag, "ota_task_start_failed");
            }
        }
    }

    xSemaphoreGive(task_start_mutex_);
    return task_handle_ != nullptr;
}

std::uint64_t OtaWorker::now_ms_since_boot()
{
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
}

void OtaWorker::task_main()
{
    while (true) {
        xSemaphoreTake(wake_semaphore_, portMAX_DELAY);
        const Command command =
            pending_command_.exchange(Command::none, std::memory_order_relaxed);
        switch (command) {
            case Command::check_for_update:
                check_for_update();
                break;
            case Command::update_now:
                update_now();
                break;
            case Command::none:
                break;
        }
        ESP_LOGI(
            kTag,
            "ota_worker_stack_free_bytes=%u",
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        busy_.store(false, std::memory_order_relaxed);
    }
}

esp_err_t OtaWorker::github_api_event_handler(
    esp_http_client_event_t* const event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == nullptr) {
        return ESP_OK;
    }
    OtaWorker& self = *static_cast<OtaWorker*>(event->user_data);
    const std::size_t remaining =
        kGithubResponseBufferCapacity - 1U - self.github_response_length_;
    const std::size_t incoming = static_cast<std::size_t>(event->data_len);
    const std::size_t to_copy = remaining < incoming ? remaining : incoming;
    if (to_copy > 0U) {
        std::memcpy(
            self.github_response_buffer_ + self.github_response_length_,
            event->data,
            to_copy);
        self.github_response_length_ += to_copy;
        self.github_response_buffer_[self.github_response_length_] = '\0';
    }
    if (to_copy < incoming) {
        self.github_response_truncated_ = true;
    }
    return ESP_OK;
}

void OtaWorker::check_for_update()
{
    if (runtime_ != nullptr) {
        runtime_->update_ota_check_status(
            pf_runtime::OtaCheckState::checking, "", 0U);
    }

    github_response_length_ = 0U;
    github_response_truncated_ = false;
    github_response_buffer_[0] = '\0';

    const int url_written = std::snprintf(
        url_buffer_,
        sizeof(url_buffer_),
        "https://api.github.com/repos/%s/%s/releases/latest",
        kGithubOwner,
        kGithubRepo);
    if (url_written < 0 ||
        static_cast<std::size_t>(url_written) >= sizeof(url_buffer_)) {
        ESP_LOGW(kTag, "ota_check_url_build_failed");
        if (runtime_ != nullptr) {
            runtime_->update_ota_check_status(
                pf_runtime::OtaCheckState::check_failed, "", 0U);
        }
        return;
    }

    esp_http_client_config_t http_config{};
    http_config.url = url_buffer_;
    http_config.method = HTTP_METHOD_GET;
    http_config.timeout_ms = static_cast<int>(kGithubApiTimeoutMs);
    http_config.crt_bundle_attach = &esp_crt_bundle_attach;
    http_config.event_handler = &OtaWorker::github_api_event_handler;
    http_config.user_data = this;

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        if (runtime_ != nullptr) {
            runtime_->update_ota_check_status(
                pf_runtime::OtaCheckState::check_failed, "", 0U);
        }
        return;
    }
    // GitHub's REST API rejects requests with no User-Agent header.
    esp_http_client_set_header(client, "User-Agent", "paperframe-firmware");

    const esp_err_t perform_result = esp_http_client_perform(client);
    if (perform_result != ESP_OK) {
        ESP_LOGW(
            kTag, "ota_check_fetch_failed=%s", esp_err_to_name(perform_result));
        esp_http_client_cleanup(client);
        if (runtime_ != nullptr) {
            runtime_->update_ota_check_status(
                pf_runtime::OtaCheckState::check_failed, "", 0U);
        }
        return;
    }
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    const std::time_t wall_clock_now = std::time(nullptr);
    const std::uint64_t epoch_s =
        wall_clock_now == static_cast<std::time_t>(-1)
            ? 0U
            : static_cast<std::uint64_t>(wall_clock_now);

    if (status_code == 403) {
        // Most likely the unauthenticated GitHub API rate limit (60
        // req/hour/IP); could also be a private repo, but either way this
        // is not a transient network error and shouldn't be reported as
        // one.
        ESP_LOGW(kTag, "ota_check_rate_limited status=%d", status_code);
        if (runtime_ != nullptr) {
            runtime_->update_ota_check_status(
                pf_runtime::OtaCheckState::check_failed, "", epoch_s);
        }
        return;
    }
    if (status_code != 200) {
        ESP_LOGW(kTag, "ota_check_http_status=%d", status_code);
        if (runtime_ != nullptr) {
            runtime_->update_ota_check_status(
                pf_runtime::OtaCheckState::check_failed, "", epoch_s);
        }
        return;
    }
    const GithubTagExtractResult tag =
        extract_tag_name(github_response_buffer_, github_response_length_);
    if (!tag.ok) {
        // A truncated response is only a problem if it cut off before the
        // top-level "tag_name" field itself: GitHub's release JSON places
        // tag_name well before the large trailing fields ("assets", "body")
        // that routinely push real responses past this fixed-size buffer,
        // so truncation there is expected and harmless. Only report
        // truncation as the cause once parsing has actually failed.
        if (github_response_truncated_) {
            ESP_LOGW(kTag, "ota_check_response_truncated");
        } else {
            ESP_LOGW(kTag, "ota_check_tag_name_missing_or_malformed");
        }
        if (runtime_ != nullptr) {
            runtime_->update_ota_check_status(
                pf_runtime::OtaCheckState::check_failed, "", epoch_s);
        }
        return;
    }

    const pf_runtime::VersionCompareResult comparison =
        pf_runtime::compare_semver(pf_runtime::kFirmwareVersion, tag.tag_name);
    if (!comparison.comparable) {
        // An unparsable tag must never be silently treated as "no update
        // available" -- that would hide a real new release.
        ESP_LOGW(kTag, "ota_check_tag_unparsable=%s", tag.tag_name);
        if (runtime_ != nullptr) {
            runtime_->update_ota_check_status(
                pf_runtime::OtaCheckState::check_failed, tag.tag_name, epoch_s);
        }
        return;
    }

    if (runtime_ != nullptr) {
        runtime_->update_ota_check_status(
            comparison.order > 0 ? pf_runtime::OtaCheckState::update_available
                                 : pf_runtime::OtaCheckState::up_to_date,
            tag.tag_name,
            epoch_s);
    }
}

namespace {

// Diagnostic-only: logs internal (non-PSRAM) and DMA-capable heap
// headroom, both total-free and largest-contiguous-block, so a real
// on-device failure log tells us whether esp_https_ota is failing from
// genuine memory exhaustion (and which pool) versus fragmentation, rather
// than guessing. See docs/hardware/VALIDATION.md's 2026-08-03 OTA entry --
// this does not change any OTA behavior.
void log_heap_headroom(const char* const point)
{
    ESP_LOGI(
        kTag,
        "ota_heap_headroom point=%s internal_free=%u internal_largest=%u "
        "dma_free=%u dma_largest=%u",
        point,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
}

}  // namespace

void OtaWorker::update_now()
{
    if (runtime_ != nullptr) {
        runtime_->update_ota_update_status(
            pf_runtime::OtaUpdateState::downloading, 0U, "");
    }
    log_heap_headroom("update_now_start");

    const int url_written = std::snprintf(
        url_buffer_,
        sizeof(url_buffer_),
        "https://github.com/%s/%s/releases/latest/download/%s",
        kGithubOwner,
        kGithubRepo,
        kReleaseAssetName);
    if (url_written < 0 ||
        static_cast<std::size_t>(url_written) >= sizeof(url_buffer_)) {
        ESP_LOGW(kTag, "ota_update_url_build_failed");
        if (runtime_ != nullptr) {
            runtime_->update_ota_update_status(
                pf_runtime::OtaUpdateState::failed, 0U, "download_failed");
        }
        return;
    }

    esp_http_client_config_t http_config{};
    http_config.url = url_buffer_;
    http_config.timeout_ms = 15000;
    http_config.crt_bundle_attach = &esp_crt_bundle_attach;
    http_config.keep_alive_enable = true;
    // esp_http_client's default buffer sizes (512 bytes each for
    // buffer_size/buffer_size_tx) are sized for ordinary requests, not
    // GitHub's release-asset redirect chain: the final hop is a signed
    // Azure Blob SAS URL with an embedded JWT, whose path+query alone
    // measured 883 bytes on a real request to this repo's release --
    // "GET <path+query> HTTP/1.1" (896 bytes) is written into the
    // *outgoing* request-line buffer, sized by buffer_size_tx, not
    // buffer_size (that one covers incoming response header parsing).
    // Confirmed on real hardware 2026-08-03: first attempt raised only
    // buffer_size and still failed with "HTTP_CLIENT: Out of buffer" at
    // the exact request-line-formatting call site in esp_http_client.c
    // (see docs/hardware/VALIDATION.md) -- buffer_size_tx was the actual
    // culprit. Both are set here. Sized to 4096 (well above the measured
    // 896 bytes) rather than exactly to the observed length, since
    // GitHub/Azure's SAS signature and embedded JWT are outside this
    // project's control and could grow in length over time -- a future
    // failure here should read from a genuinely oversized URL, not one
    // that merely grew past a tightly-fitted buffer. Heap headroom
    // measured during the original failure (>85 KB free, 31 KB largest
    // contiguous block) makes the extra couple of KB a trivial cost.
    http_config.buffer_size = 4096;
    http_config.buffer_size_tx = 4096;

    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;

    log_heap_headroom("before_esp_https_ota_begin");
    esp_https_ota_handle_t handle = nullptr;
    esp_err_t result = esp_https_ota_begin(&ota_config, &handle);
    if (result != ESP_OK || handle == nullptr) {
        ESP_LOGW(kTag, "ota_update_begin_failed=%s", esp_err_to_name(result));
        log_heap_headroom("ota_update_begin_failed");
        if (runtime_ != nullptr) {
            runtime_->update_ota_update_status(
                pf_runtime::OtaUpdateState::failed, 0U, "https_error");
        }
        return;
    }

    const std::uint64_t deadline_ms =
        now_ms_since_boot() + kUpdateOverallDeadlineMs;
    result = ESP_ERR_HTTPS_OTA_IN_PROGRESS;
    while (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        result = esp_https_ota_perform(handle);

        const int image_size = esp_https_ota_get_image_size(handle);
        const int read_so_far = esp_https_ota_get_image_len_read(handle);
        std::uint8_t percent = 0U;
        if (image_size > 0 && read_so_far >= 0) {
            percent = static_cast<std::uint8_t>(
                (static_cast<std::int64_t>(read_so_far) * 100) / image_size);
        }
        if (runtime_ != nullptr) {
            runtime_->update_ota_update_status(
                pf_runtime::OtaUpdateState::writing, percent, "");
        }

        if (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS &&
            now_ms_since_boot() > deadline_ms) {
            ESP_LOGW(kTag, "ota_update_deadline_exceeded");
            esp_https_ota_abort(handle);
            if (runtime_ != nullptr) {
                runtime_->update_ota_update_status(
                    pf_runtime::OtaUpdateState::failed, percent, "timeout");
            }
            return;
        }
    }

    if (result != ESP_OK) {
        ESP_LOGW(kTag, "ota_update_perform_failed=%s", esp_err_to_name(result));
        esp_https_ota_abort(handle);
        if (runtime_ != nullptr) {
            runtime_->update_ota_update_status(
                pf_runtime::OtaUpdateState::failed, 0U, "download_failed");
        }
        return;
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGW(kTag, "ota_update_incomplete_data");
        esp_https_ota_abort(handle);
        if (runtime_ != nullptr) {
            runtime_->update_ota_update_status(
                pf_runtime::OtaUpdateState::failed, 0U, "download_failed");
        }
        return;
    }

    const esp_err_t finish_result = esp_https_ota_finish(handle);
    if (finish_result != ESP_OK) {
        ESP_LOGW(
            kTag, "ota_update_finish_failed=%s", esp_err_to_name(finish_result));
        if (runtime_ != nullptr) {
            runtime_->update_ota_update_status(
                pf_runtime::OtaUpdateState::failed, 0U, "image_invalid");
        }
        return;
    }

    // imagefs is never touched here: esp_https_ota only ever writes to the
    // OTA app partition esp_ota_get_next_update_partition() selects -- which
    // now carries the WebUI too, so the frontend cannot lag the backend.
    // esp_https_ota_finish() above has already switched the boot partition,
    // so even if scheduling the automatic reboot fails, the new firmware
    // will still boot on whatever the next reboot happens to be -- the
    // snapshot must say so rather than silently claiming "pending reboot"
    // forever while nothing is actually scheduled.
    const bool reboot_scheduled =
        pf_runtime::schedule_reboot("ota_update_complete");
    if (runtime_ != nullptr) {
        runtime_->update_ota_update_status(
            pf_runtime::OtaUpdateState::ready_pending_reboot,
            100U,
            // Must fit pf_runtime::kOtaErrorCapacity (32 bytes incl. NUL).
            reboot_scheduled ? "" : "manual_reboot_required");
    }
}

OtaWorker& ota_worker()
{
    static OtaWorker instance;
    return instance;
}

}  // namespace pf_ota
