// SPDX-License-Identifier: Apache-2.0
#include "url_playback_tracker.h"
#include <algorithm>
#include <chrono>

namespace send_airplay2::detail {
namespace {
constexpr const char* playing_state = "playing";

[[nodiscard]] double seconds_between(std::chrono::steady_clock::time_point from,
                                     std::chrono::steady_clock::time_point to) {
    return std::max(0.0, std::chrono::duration<double>(to - from).count());
}
} // namespace

bool UrlPlaybackTracker::advancing() const noexcept {
    return position_seconds_ && state_ == playing_state && playback_rate_ && *playback_rate_ > 0;
}

void UrlPlaybackTracker::rebase(std::chrono::steady_clock::time_point now) {
    if (advancing()) {
        *position_seconds_ += seconds_between(position_time_, now) * *playback_rate_;
    }
    position_time_ = now;
}

void UrlPlaybackTracker::observe(const SessionEvent& event,
                                 std::chrono::steady_clock::time_point received) {
    ++events_;
    rebase(received);
    if (event.playback_state) {
        state_ = *event.playback_state;
        played_ = played_ || state_ == playing_state;
    }
    if (event.duration_seconds) {
        duration_seconds_ = event.duration_seconds;
    }
    if (event.playback_rate) {
        playback_rate_ = event.playback_rate;
    }
    if (event.position_seconds) {
        reported_position_seconds_ = event.position_seconds;
        position_seconds_ = event.position_seconds;
    }
}

MrpPlaybackStatus UrlPlaybackTracker::status(std::chrono::steady_clock::time_point now) const {
    MrpPlaybackStatus result;
    result.owned = played_;
    result.state = state_;
    result.duration_seconds = duration_seconds_;
    result.playback_rate = playback_rate_;
    result.reported_position_seconds = reported_position_seconds_;
    result.messages = events_;
    const bool halted = state_ == "paused" || state_ == "stopped";
    const bool timed = reported_position_seconds_ && duration_seconds_ && *duration_seconds_ > 0;
    result.at_end = timed && halted && *reported_position_seconds_ >= *duration_seconds_;
    result.near_end =
        timed && halted &&
        *reported_position_seconds_ >= *duration_seconds_ - near_end_tolerance_seconds;
    if (position_seconds_) {
        auto position = *position_seconds_;
        if (advancing()) {
            position += seconds_between(position_time_, now) * *playback_rate_;
        }
        position = std::max(0.0, position);
        if (duration_seconds_) {
            position = std::min(position, *duration_seconds_);
        }
        result.position_seconds = position;
    }
    return result;
}
} // namespace send_airplay2::detail
