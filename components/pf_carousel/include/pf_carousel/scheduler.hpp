#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "pf_config/schema.hpp"

namespace pf_carousel {

enum class CarouselMode : std::uint8_t {
    sequential,
    random,
};

struct CarouselConfig {
    std::uint32_t interval_minutes = pf_config::kDefaultRefreshMinutes;
    CarouselMode mode = CarouselMode::sequential;
    std::uint32_t random_seed = 0x9E3779B9U;
};

struct CarouselItem {
    std::uint32_t image_id;
    bool enabled;
    bool valid;
    bool shown_once;
};

// How long the *first* failed welcome refresh waits before being retried,
// instead of the full carousel interval. Roughly one panel refresh (a full
// e-paper refresh measures ~31 s): short enough that a transient failure
// does not strand a blank or known-wrong panel.
//
// Each consecutive failure doubles the wait, capped at the normal carousel
// interval. That cap matters more than it looks: an outcome that is not
// refreshed_and_slept can mean the frame *did* reach the panel and only the
// deep-sleep step failed, in which case every retry is a full refresh of a
// picture that is already correct. A fixed short retry would turn that into
// roughly 1,400 refreshes a day against a panel with a finite refresh life;
// backing off to the carousel interval bounds a persistent fault at the
// cadence the device would have used anyway, while still recovering from a
// one-off failure in 30 seconds.
inline constexpr std::uint64_t kWelcomeRetryMs = 30U * 1000U;

enum class DecisionKind : std::uint8_t {
    wait,
    display_image,
    display_welcome,
};

enum class DecisionReason : std::uint8_t {
    none,
    initial,
    interval_elapsed,
    pending_first_display,
    manual,
    library_empty,
    invalid_input,
};

struct CarouselDecision {
    DecisionKind kind = DecisionKind::wait;
    DecisionReason reason = DecisionReason::none;
    std::uint32_t image_id = 0;
    std::uint64_t not_before_ms = 0;
    std::uint32_t sequence = 0;
};

class CarouselScheduler {
public:
    CarouselScheduler() = default;

    explicit CarouselScheduler(const CarouselConfig config)
    {
        configure(config);
    }

    bool configure(const CarouselConfig config)
    {
        if (config.interval_minutes < pf_config::kMinimumRefreshMinutes ||
            config.interval_minutes > pf_config::kMaximumRefreshMinutes ||
            (config.mode != CarouselMode::sequential &&
             config.mode != CarouselMode::random) ||
            in_flight_) {
            return false;
        }

        interval_minutes_ = config.interval_minutes;
        mode_ = config.mode;
        random_state_ =
            config.random_seed == 0U ? 0x9E3779B9U : config.random_seed;
        return true;
    }

    std::uint32_t interval_minutes() const
    {
        return interval_minutes_;
    }

    CarouselMode mode() const
    {
        return mode_;
    }

    std::uint64_t next_due_ms() const
    {
        return next_due_ms_;
    }

    bool in_flight() const
    {
        return in_flight_;
    }

    bool request_manual(
        const std::uint32_t image_id,
        const CarouselItem* const items,
        const std::size_t item_count)
    {
        if (!valid_items_pointer(items, item_count) ||
            find_eligible(image_id, items, item_count) == nullptr) {
            return false;
        }
        manual_pending_ = true;
        manual_image_id_ = image_id;
        return true;
    }

    CarouselDecision poll(
        const std::uint64_t now_ms,
        const CarouselItem* const items,
        const std::size_t item_count)
    {
        if (!valid_items_pointer(items, item_count)) {
            return wait_decision(
                now_ms,
                DecisionReason::invalid_input);
        }
        if (in_flight_) {
            return wait_decision(next_due_ms_);
        }
        if (now_ms < next_due_ms_) {
            return wait_decision(next_due_ms_);
        }

        if (manual_pending_) {
            const CarouselItem* const manual =
                find_eligible(manual_image_id_, items, item_count);
            if (manual != nullptr) {
                return issue_image(
                    now_ms,
                    *manual,
                    DecisionReason::manual);
            }
            manual_pending_ = false;
        }

        const CarouselItem* const selected =
            select_automatic(items, item_count);
        if (selected != nullptr) {
            const DecisionReason reason =
                !selected->shown_once
                    ? DecisionReason::pending_first_display
                    : (has_current_
                           ? DecisionReason::interval_elapsed
                           : DecisionReason::initial);
            return issue_image(now_ms, *selected, reason);
        }

        if (has_current_ && current_is_welcome_ && !welcome_stale_) {
            return wait_decision(
                std::numeric_limits<std::uint64_t>::max());
        }
        return issue(
            now_ms,
            DecisionKind::display_welcome,
            DecisionReason::library_empty,
            0U);
    }

    bool complete(
        const CarouselDecision decision,
        const bool succeeded,
        const std::uint64_t completed_at_ms)
    {
        if (!matches_active(decision)) {
            return false;
        }

        const bool was_welcome =
            active_decision_.kind == DecisionKind::display_welcome;
        in_flight_ = false;
        if (succeeded) {
            has_current_ = true;
            current_is_welcome_ = was_welcome;
            if (current_is_welcome_) {
                // Whatever made the previous welcome frame stale is now on
                // the panel.
                welcome_stale_ = false;
            }
            if (!current_is_welcome_) {
                current_image_id_ = active_decision_.image_id;
            }
            if (active_decision_.reason == DecisionReason::manual) {
                manual_pending_ = false;
            }
            welcome_retry_backoff_ = false;
            welcome_failure_count_ = 0U;
            next_due_ms_ = add_interval(completed_at_ms);
            return true;
        }

        // A failed welcome refresh leaves the panel showing either nothing
        // (boot, before anything has been drawn) or a frame already known
        // to be wrong -- a stale address, or the blank frame an away
        // transition put there. Waiting out a whole carousel interval, up
        // to 24 h at the configured maximum, would strand it in that state,
        // so a failed welcome retries on a short fixed delay instead.
        //
        // A failed image is deliberately left on the normal interval: the
        // previous image is still on the panel and perfectly readable, so
        // there is nothing to rescue and retrying hard would only burn
        // refreshes against a panel that just failed one.
        welcome_retry_backoff_ = was_welcome;
        if (was_welcome) {
            if (welcome_failure_count_ <
                std::numeric_limits<std::uint32_t>::max()) {
                ++welcome_failure_count_;
            }
            next_due_ms_ = add_welcome_retry(completed_at_ms);
        } else {
            next_due_ms_ = add_interval(completed_at_ms);
        }
        return true;
    }

    bool abandon(
        const CarouselDecision decision,
        const std::uint64_t retry_at_ms)
    {
        if (!matches_active(decision)) {
            return false;
        }
        in_flight_ = false;
        next_due_ms_ = retry_at_ms;
        return true;
    }

    // Resets the deadline so the next poll() no longer waits on the old
    // interval, without touching in_flight_/manual_pending_/current-image
    // state (used when presence returns from away: "輪播計時重新開始…
    // 不使用離席前殘留的刷新 deadline", Guild.md 4.9). Returns false
    // without effect while a decision is still in flight, so a caller
    // can never use this to bypass the in-flight guard that complete()/
    // abandon() rely on; retry after the in-flight decision resolves.
    bool force_immediate(const std::uint64_t now_ms)
    {
        if (in_flight_) {
            return false;
        }
        next_due_ms_ = now_ms;
        return true;
    }

    // Marks the welcome frame currently on the panel as stale so the next
    // poll() issues it again.
    //
    // While the library is empty the welcome frame is drawn exactly once
    // and poll() then parks on an infinite deadline -- correct for its
    // static content, wrong for the status bar overlaid on it. At boot that
    // frame is drawn before Wi-Fi has an address, so without a way to
    // redraw it a device with no images would never show the IP its owner
    // needs to reach the WebUI and upload the first image.
    //
    // Refused while a decision is in flight, for the same reason
    // force_immediate() is: complete()/abandon() rely on that guard, so the
    // caller must retry once the in-flight decision resolves. Also refused
    // when a real image is on the panel -- that image carries a status bar
    // rendered at its own refresh, so there is nothing here to fix.
    //
    // Deliberately allowed when nothing has been displayed yet: that is the
    // state after the very first welcome frame failed to refresh, and it is
    // exactly when the panel most needs another attempt.
    bool request_welcome_redraw(const std::uint64_t now_ms)
    {
        if (in_flight_ || (has_current_ && !current_is_welcome_)) {
            return false;
        }
        welcome_stale_ = true;
        // Never pull a failure backoff forward. The caller re-evaluates its
        // condition every tick, so without this a permanently failing panel
        // would have the kWelcomeRetryMs deadline reset to "now" one tick
        // after every failure -- turning the backoff into a continuous
        // refresh loop. A deadline that is merely the ordinary carousel
        // interval is still pulled forward: that is the whole point of the
        // redraw when the address changes right after a successful frame.
        if (!welcome_retry_backoff_ || now_ms >= next_due_ms_) {
            next_due_ms_ = now_ms;
        }
        return true;
    }

private:
    static bool valid_items_pointer(
        const CarouselItem* const items,
        const std::size_t item_count)
    {
        return item_count == 0U || items != nullptr;
    }

    static bool eligible(const CarouselItem& item)
    {
        return item.enabled && item.valid;
    }

    static const CarouselItem* find_eligible(
        const std::uint32_t image_id,
        const CarouselItem* const items,
        const std::size_t item_count)
    {
        if (!valid_items_pointer(items, item_count)) {
            return nullptr;
        }
        for (std::size_t index = 0; index < item_count; ++index) {
            if (items[index].image_id == image_id &&
                eligible(items[index])) {
                return &items[index];
            }
        }
        return nullptr;
    }

    CarouselDecision wait_decision(
        const std::uint64_t not_before_ms,
        const DecisionReason reason = DecisionReason::none) const
    {
        return {
            DecisionKind::wait,
            reason,
            0U,
            not_before_ms,
            active_sequence_,
        };
    }

    CarouselDecision issue_image(
        const std::uint64_t now_ms,
        const CarouselItem& item,
        const DecisionReason reason)
    {
        return issue(
            now_ms,
            DecisionKind::display_image,
            reason,
            item.image_id);
    }

    CarouselDecision issue(
        const std::uint64_t now_ms,
        const DecisionKind kind,
        const DecisionReason reason,
        const std::uint32_t image_id)
    {
        ++active_sequence_;
        if (active_sequence_ == 0U) {
            ++active_sequence_;
        }
        active_decision_ = {
            kind,
            reason,
            image_id,
            now_ms,
            active_sequence_,
        };
        in_flight_ = true;
        // The backoff has done its job once the retry is actually issued;
        // complete() sets it again if this attempt also fails.
        welcome_retry_backoff_ = false;
        return active_decision_;
    }

    bool matches_active(const CarouselDecision& decision) const
    {
        return in_flight_ &&
               decision.sequence == active_decision_.sequence &&
               decision.kind == active_decision_.kind &&
               decision.reason == active_decision_.reason &&
               decision.image_id == active_decision_.image_id &&
               decision.not_before_ms ==
                   active_decision_.not_before_ms;
    }

    std::uint64_t interval_ms() const
    {
        return static_cast<std::uint64_t>(interval_minutes_) *
               60U * 1000U;
    }

    std::uint64_t add_interval(
        const std::uint64_t timestamp_ms) const
    {
        const std::uint64_t duration = interval_ms();
        return timestamp_ms >
                       (std::numeric_limits<std::uint64_t>::max() - duration)
                   ? std::numeric_limits<std::uint64_t>::max()
                   : timestamp_ms + duration;
    }

    // kWelcomeRetryMs doubled once per consecutive failure already counted,
    // capped at the normal carousel interval so a persistent fault settles
    // at the cadence the device would have refreshed at anyway.
    std::uint64_t welcome_retry_delay_ms() const
    {
        const std::uint64_t cap = interval_ms();
        std::uint64_t delay = kWelcomeRetryMs;
        for (std::uint32_t doubled = 1U;
             doubled < welcome_failure_count_ && delay < cap;
             ++doubled) {
            delay *= 2U;
        }
        return delay > cap ? cap : delay;
    }

    std::uint64_t add_welcome_retry(const std::uint64_t timestamp_ms) const
    {
        const std::uint64_t delay = welcome_retry_delay_ms();
        return timestamp_ms >
                       (std::numeric_limits<std::uint64_t>::max() - delay)
                   ? std::numeric_limits<std::uint64_t>::max()
                   : timestamp_ms + delay;
    }

    std::size_t sequential_start(
        const CarouselItem* const items,
        const std::size_t item_count) const
    {
        if (!has_current_ || current_is_welcome_) {
            return 0U;
        }
        for (std::size_t index = 0; index < item_count; ++index) {
            if (items[index].image_id == current_image_id_) {
                return (index + 1U) % item_count;
            }
        }
        return 0U;
    }

    const CarouselItem* select_sequential(
        const CarouselItem* const items,
        const std::size_t item_count,
        const bool unseen_only) const
    {
        if (item_count == 0U) {
            return nullptr;
        }
        const std::size_t start = sequential_start(items, item_count);
        for (std::size_t offset = 0; offset < item_count; ++offset) {
            const CarouselItem& candidate =
                items[(start + offset) % item_count];
            if (eligible(candidate) &&
                (!unseen_only || !candidate.shown_once)) {
                return &candidate;
            }
        }
        return nullptr;
    }

    std::uint32_t next_random()
    {
        std::uint32_t value = random_state_;
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        random_state_ = value;
        return value;
    }

    const CarouselItem* select_random(
        const CarouselItem* const items,
        const std::size_t item_count,
        const bool unseen_only)
    {
        std::size_t candidate_count = 0U;
        for (std::size_t index = 0; index < item_count; ++index) {
            if (eligible(items[index]) &&
                (!unseen_only || !items[index].shown_once)) {
                ++candidate_count;
            }
        }
        if (candidate_count == 0U) {
            return nullptr;
        }

        const bool exclude_current =
            candidate_count > 1U && has_current_ && !current_is_welcome_;
        if (exclude_current) {
            for (std::size_t index = 0; index < item_count; ++index) {
                if (eligible(items[index]) &&
                    (!unseen_only || !items[index].shown_once) &&
                    items[index].image_id == current_image_id_) {
                    --candidate_count;
                    break;
                }
            }
        }

        const std::size_t selected_index =
            static_cast<std::size_t>(next_random()) % candidate_count;
        std::size_t seen = 0U;
        for (std::size_t index = 0; index < item_count; ++index) {
            const CarouselItem& candidate = items[index];
            if (!eligible(candidate) ||
                (unseen_only && candidate.shown_once) ||
                (exclude_current &&
                 candidate.image_id == current_image_id_)) {
                continue;
            }
            if (seen == selected_index) {
                return &candidate;
            }
            ++seen;
        }
        return nullptr;
    }

    const CarouselItem* select_automatic(
        const CarouselItem* const items,
        const std::size_t item_count)
    {
        bool has_unseen = false;
        for (std::size_t index = 0; index < item_count; ++index) {
            if (eligible(items[index]) && !items[index].shown_once) {
                has_unseen = true;
                break;
            }
        }

        if (mode_ == CarouselMode::random) {
            return select_random(items, item_count, has_unseen);
        }
        return select_sequential(items, item_count, has_unseen);
    }

    std::uint32_t interval_minutes_ = pf_config::kDefaultRefreshMinutes;
    CarouselMode mode_ = CarouselMode::sequential;
    std::uint32_t random_state_ = 0x9E3779B9U;
    std::uint64_t next_due_ms_ = 0U;
    bool has_current_ = false;
    bool current_is_welcome_ = false;
    // Set by request_welcome_redraw(); cleared once the replacement welcome
    // frame is actually on the panel.
    bool welcome_stale_ = false;
    // True while next_due_ms_ holds a kWelcomeRetryMs backoff from a failed
    // welcome refresh, as opposed to an ordinary carousel interval. Only
    // the former is protected from being pulled forward by a redraw
    // request; see request_welcome_redraw().
    bool welcome_retry_backoff_ = false;
    // Consecutive failed welcome refreshes; drives the doubling in
    // welcome_retry_delay_ms(). Reset by any successful refresh.
    std::uint32_t welcome_failure_count_ = 0U;
    std::uint32_t current_image_id_ = 0U;
    bool manual_pending_ = false;
    std::uint32_t manual_image_id_ = 0U;
    bool in_flight_ = false;
    std::uint32_t active_sequence_ = 0U;
    CarouselDecision active_decision_{};
};

}  // namespace pf_carousel
