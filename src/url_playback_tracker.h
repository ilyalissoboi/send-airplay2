// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_URL_PLAYBACK_TRACKER_H
#define SEND_AIRPLAY2_URL_PLAYBACK_TRACKER_H

#include "mrp_messages.h"
#include "session_messages.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace send_airplay2::detail {
/**
 * Progress of our URL item from the URL session's own events, for receivers
 * that refuse the remote-control session and so have no MRP (macOS, D62).
 *
 * Every event on the URL event channel concerns the item this session queued,
 * so `owned` means only that the receiver has reported it playing; no other
 * player's state is visible here. The position is the last receiver-reported
 * position, advanced by `rate` x the local steady time since it arrived while
 * the item plays, and clamped to [0, duration]. `at_end` and `near_end` use
 * the receiver-reported position only, as the MRP tracker does, so a pause
 * just before the end never becomes EOF through extrapolation.
 *
 * Not synchronized: the session calls it under its state mutex.
 */
class UrlPlaybackTracker {
public:
    /// Apply one decoded event that arrived at `received` (monotonic).
    void observe(const SessionEvent& event, std::chrono::steady_clock::time_point received);
    /// The status at `now`; `now` must not precede the last observed event.
    [[nodiscard]] MrpPlaybackStatus status(std::chrono::steady_clock::time_point now) const;

private:
    [[nodiscard]] bool advancing() const noexcept;
    /// Fold the elapsed play time into the base position at `now`, so a later
    /// state or rate change extrapolates from where the item had reached.
    void rebase(std::chrono::steady_clock::time_point now);

    std::string state_; // Fixed playbackState label, empty before the first.
    std::optional<double> duration_seconds_;
    std::optional<double> playback_rate_;
    std::optional<double> reported_position_seconds_; // Last receiver value, unextrapolated.
    std::optional<double> position_seconds_;          // Extrapolation base.
    std::chrono::steady_clock::time_point position_time_{};
    bool played_ = false;
    std::uint64_t events_ = 0;
};
} // namespace send_airplay2::detail
#endif
