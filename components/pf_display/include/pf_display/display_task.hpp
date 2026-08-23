#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "pf_display/epd7in3e.hpp"
#include "pf_runtime/runtime_messages.hpp"

namespace pf_display {

class DisplayFramePool;

class FrameWriteLease {
public:
    FrameWriteLease() = default;
    ~FrameWriteLease();

    FrameWriteLease(const FrameWriteLease&) = delete;
    FrameWriteLease& operator=(const FrameWriteLease&) = delete;

    FrameWriteLease(FrameWriteLease&& other) noexcept
    {
        take_from(other);
    }

    FrameWriteLease& operator=(FrameWriteLease&& other) noexcept;

    bool valid() const
    {
        return pool_ != nullptr;
    }

    std::uint8_t* data() const
    {
        return valid() ? data_ : nullptr;
    }

    std::size_t size() const
    {
        return valid() ? kFullFramebufferBytes : 0U;
    }

    pf_runtime::FrameToken token() const
    {
        return token_;
    }

    void release();

private:
    FrameWriteLease(
        DisplayFramePool& pool,
        const pf_runtime::FrameToken token,
        std::uint8_t* const data)
        : pool_(&pool),
          token_(token),
          data_(data)
    {
    }

    void take_from(FrameWriteLease& other)
    {
        pool_ = other.pool_;
        token_ = other.token_;
        data_ = other.data_;
        other.pool_ = nullptr;
        other.data_ = nullptr;
    }

    void invalidate()
    {
        pool_ = nullptr;
        data_ = nullptr;
    }

    DisplayFramePool* pool_ = nullptr;
    pf_runtime::FrameToken token_{};
    std::uint8_t* data_ = nullptr;

    friend class DisplayFramePool;
};

class FrameReadLease {
public:
    FrameReadLease() = default;
    ~FrameReadLease();

    FrameReadLease(const FrameReadLease&) = delete;
    FrameReadLease& operator=(const FrameReadLease&) = delete;

    FrameReadLease(FrameReadLease&& other) noexcept
    {
        take_from(other);
    }

    FrameReadLease& operator=(FrameReadLease&& other) noexcept;

    bool valid() const
    {
        return pool_ != nullptr;
    }

    const std::uint8_t* data() const
    {
        return valid() ? data_ : nullptr;
    }

    std::size_t size() const
    {
        return valid() ? kFullFramebufferBytes : 0U;
    }

    pf_runtime::FrameToken token() const
    {
        return token_;
    }

    void release();

private:
    FrameReadLease(
        DisplayFramePool& pool,
        const pf_runtime::FrameToken token,
        const std::uint8_t* const data)
        : pool_(&pool),
          token_(token),
          data_(data)
    {
    }

    void take_from(FrameReadLease& other)
    {
        pool_ = other.pool_;
        token_ = other.token_;
        data_ = other.data_;
        other.pool_ = nullptr;
        other.data_ = nullptr;
    }

    void invalidate()
    {
        pool_ = nullptr;
        data_ = nullptr;
    }

    DisplayFramePool* pool_ = nullptr;
    pf_runtime::FrameToken token_{};
    const std::uint8_t* data_ = nullptr;

    friend class DisplayFramePool;
};

class DisplayFramePool {
public:
    static constexpr std::size_t kSlotCount = 2U;

    bool initialize(
        std::uint8_t* const first,
        std::uint8_t* const second)
    {
        if (initialized_ || first == nullptr || second == nullptr ||
            first == second) {
            return false;
        }
        slots_[0].data = first;
        slots_[1].data = second;
        initialized_ = true;
        return true;
    }

    bool reset()
    {
        if (!initialized_) {
            return true;
        }
        for (Slot& slot : slots_) {
            if (slot.state.load(std::memory_order_acquire) !=
                SlotState::free) {
                return false;
            }
        }
        for (Slot& slot : slots_) {
            slot.data = nullptr;
            slot.generation.store(1U, std::memory_order_release);
        }
        initialized_ = false;
        return true;
    }

    FrameWriteLease try_acquire_write()
    {
        if (!initialized_) {
            return {};
        }
        for (std::uint8_t index = 0; index < kSlotCount; ++index) {
            SlotState expected = SlotState::free;
            if (slots_[index].state.compare_exchange_strong(
                    expected,
                    SlotState::writing,
                    std::memory_order_acq_rel)) {
                const pf_runtime::FrameToken token{
                    index,
                    slots_[index].generation.load(
                        std::memory_order_acquire),
                };
                return FrameWriteLease{
                    *this,
                    token,
                    slots_[index].data,
                };
            }
        }
        return {};
    }

    FrameReadLease try_begin_display(
        const pf_runtime::FrameToken token)
    {
        Slot* const slot = find_slot(token);
        if (slot == nullptr) {
            return {};
        }
        SlotState expected = SlotState::queued;
        if (!slot->state.compare_exchange_strong(
                expected,
                SlotState::displaying,
                std::memory_order_acq_rel)) {
            return {};
        }
        return FrameReadLease{*this, token, slot->data};
    }

private:
    enum class SlotState : std::uint8_t {
        free,
        writing,
        queued,
        displaying,
        releasing,
    };

    struct Slot {
        std::uint8_t* data = nullptr;
        std::atomic<SlotState> state{SlotState::free};
        std::atomic<std::uint32_t> generation{1U};
    };

    Slot* find_slot(const pf_runtime::FrameToken token)
    {
        if (!initialized_ || token.slot >= kSlotCount) {
            return nullptr;
        }
        Slot& slot = slots_[token.slot];
        return slot.generation.load(std::memory_order_acquire) ==
                       token.generation
                   ? &slot
                   : nullptr;
    }

    bool queue_for_submit(
        FrameWriteLease& lease,
        pf_runtime::FrameToken& token)
    {
        if (lease.pool_ != this) {
            return false;
        }
        Slot* const slot = find_slot(lease.token_);
        if (slot == nullptr) {
            return false;
        }
        SlotState expected = SlotState::writing;
        if (!slot->state.compare_exchange_strong(
                expected,
                SlotState::queued,
                std::memory_order_acq_rel)) {
            return false;
        }
        token = lease.token_;
        lease.invalidate();
        return true;
    }

    bool restore_write_after_submit_failure(
        const pf_runtime::FrameToken token,
        FrameWriteLease& lease)
    {
        Slot* const slot = find_slot(token);
        if (slot == nullptr || lease.valid()) {
            return false;
        }
        SlotState expected = SlotState::queued;
        if (!slot->state.compare_exchange_strong(
                expected,
                SlotState::writing,
                std::memory_order_acq_rel)) {
            return false;
        }
        lease = FrameWriteLease{*this, token, slot->data};
        return true;
    }

    void release_write(const pf_runtime::FrameToken token)
    {
        release_slot(token, SlotState::writing);
    }

    void release_read(const pf_runtime::FrameToken token)
    {
        release_slot(token, SlotState::displaying);
    }

    void release_slot(
        const pf_runtime::FrameToken token,
        const SlotState owned_state)
    {
        Slot* const slot = find_slot(token);
        if (slot == nullptr) {
            return;
        }
        SlotState expected = owned_state;
        if (!slot->state.compare_exchange_strong(
                expected,
                SlotState::releasing,
                std::memory_order_acq_rel)) {
            return;
        }
        slot->generation.fetch_add(1U, std::memory_order_release);
        slot->state.store(SlotState::free, std::memory_order_release);
    }

    Slot slots_[kSlotCount]{};
    bool initialized_ = false;

    friend class FrameWriteLease;
    friend class FrameReadLease;
    friend class DisplaySubmitter;
};

inline FrameWriteLease::~FrameWriteLease()
{
    release();
}

inline FrameWriteLease& FrameWriteLease::operator=(
    FrameWriteLease&& other) noexcept
{
    if (this != &other) {
        release();
        take_from(other);
    }
    return *this;
}

inline void FrameWriteLease::release()
{
    if (pool_ != nullptr) {
        DisplayFramePool* const pool = pool_;
        const pf_runtime::FrameToken token = token_;
        invalidate();
        pool->release_write(token);
    }
}

inline FrameReadLease::~FrameReadLease()
{
    release();
}

inline FrameReadLease& FrameReadLease::operator=(
    FrameReadLease&& other) noexcept
{
    if (this != &other) {
        release();
        take_from(other);
    }
    return *this;
}

inline void FrameReadLease::release()
{
    if (pool_ != nullptr) {
        DisplayFramePool* const pool = pool_;
        const pf_runtime::FrameToken token = token_;
        invalidate();
        pool->release_read(token);
    }
}

class DisplayCommandPublisher {
public:
    virtual ~DisplayCommandPublisher() = default;
    virtual bool try_publish(
        const pf_runtime::RuntimeCommand& command) = 0;
};

enum class SubmitStatus : std::uint8_t {
    accepted,
    not_started,
    invalid_lease,
    queue_full,
};

class DisplaySubmitter {
public:
    DisplaySubmitter(
        DisplayFramePool& pool,
        DisplayCommandPublisher& publisher)
        : pool_(pool),
          publisher_(publisher)
    {
    }

    SubmitStatus try_submit(
        const std::uint32_t request_id,
        FrameWriteLease& lease)
    {
        pf_runtime::FrameToken token{};
        if (!pool_.queue_for_submit(lease, token)) {
            return SubmitStatus::invalid_lease;
        }

        const pf_runtime::RuntimeCommand command{
            request_id,
            pf_runtime::CommandKind::refresh_display,
            token,
        };
        if (publisher_.try_publish(command)) {
            return SubmitStatus::accepted;
        }

        return pool_.restore_write_after_submit_failure(token, lease)
                   ? SubmitStatus::queue_full
                   : SubmitStatus::invalid_lease;
    }

private:
    DisplayFramePool& pool_;
    DisplayCommandPublisher& publisher_;
};

class DisplayPanel {
public:
    virtual ~DisplayPanel() = default;
    virtual DriverResult refresh_and_sleep(
        const std::uint8_t* frame,
        std::size_t length) = 0;
    virtual PanelState state() const = 0;
};

class DisplayCommandProcessor {
public:
    explicit DisplayCommandProcessor(DisplayPanel& panel)
        : panel_(panel)
    {
    }

    pf_runtime::RuntimeResult process(
        const pf_runtime::RuntimeCommand& command,
        const std::uint8_t* const frame,
        const std::size_t length)
    {
        if (command.kind != pf_runtime::CommandKind::refresh_display ||
            frame == nullptr || length != kFullFramebufferBytes) {
            return {
                command.request_id,
                pf_runtime::ResultStatus::rejected,
                pf_runtime::RuntimeError::invalid_argument,
                pf_runtime::DisplayOutcome::invalid_lease,
                static_cast<std::uint8_t>(DriverStage::validate),
                false,
            };
        }

        const DriverResult driver =
            panel_.refresh_and_sleep(frame, length);
        const std::uint8_t stage =
            static_cast<std::uint8_t>(driver.stage);
        // DISPLAY_REFRESH is step 3 of 6; the picture is on the panel once
        // it completes, and the three power-off/sleep steps after it change
        // nothing visible. Report that separately from the outcome so a
        // failure in those steps does not read as "this refresh did not
        // happen" and cost another full refresh of a correct picture. The
        // threshold is deliberately the stage *after* `refresh`: a BUSY
        // timeout during `refresh` means the panel never confirmed it
        // finished, so the frame cannot be claimed as displayed.
        // See docs/adr/0019-separate-frame-displayed-from-panel-slept.md.
        const bool frame_on_panel =
            driver.stage >= DriverStage::refresh_power_off;
        switch (driver.status) {
            case DriverStatus::ok:
                if (driver.stage == DriverStage::deep_sleep &&
                    panel_.state() == PanelState::deep_sleep) {
                    return {
                        command.request_id,
                        pf_runtime::ResultStatus::completed,
                        pf_runtime::RuntimeError::none,
                        pf_runtime::DisplayOutcome::refreshed_and_slept,
                        stage,
                        frame_on_panel,
                    };
                }
                return {
                    command.request_id,
                    pf_runtime::ResultStatus::failed,
                    pf_runtime::RuntimeError::invalid_state,
                    pf_runtime::DisplayOutcome::panel_state_error,
                    stage,
                    frame_on_panel,
                };
            case DriverStatus::invalid_frame:
                return {
                    command.request_id,
                    pf_runtime::ResultStatus::rejected,
                    pf_runtime::RuntimeError::invalid_argument,
                    pf_runtime::DisplayOutcome::invalid_lease,
                    stage,
                    frame_on_panel,
                };
            case DriverStatus::busy_timeout:
                return {
                    command.request_id,
                    pf_runtime::ResultStatus::failed,
                    pf_runtime::RuntimeError::timeout,
                    pf_runtime::DisplayOutcome::busy_timeout,
                    stage,
                    frame_on_panel,
                };
            case DriverStatus::transport_error:
                return {
                    command.request_id,
                    pf_runtime::ResultStatus::failed,
                    pf_runtime::RuntimeError::transport,
                    pf_runtime::DisplayOutcome::transport_error,
                    stage,
                    frame_on_panel,
                };
        }
        return {
            command.request_id,
            pf_runtime::ResultStatus::failed,
            pf_runtime::RuntimeError::invalid_state,
            pf_runtime::DisplayOutcome::panel_state_error,
            stage,
            frame_on_panel,
        };
    }

private:
    DisplayPanel& panel_;
};

}  // namespace pf_display
